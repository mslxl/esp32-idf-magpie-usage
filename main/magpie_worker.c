#include "magpie_worker.h"

#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "magpie_transport.h"
#include "resource_budget.h"

typedef struct { note4_settings_t settings; note4_poll_mode_t mode; unsigned epoch; } command_t;
static QueueHandle_t s_commands;
static atomic_uint s_epoch;
static atomic_int s_publication;
static portMUX_TYPE s_publication_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;

bool note4_magpie_worker_current(unsigned epoch) { return atomic_load(&s_epoch) == epoch; }
void note4_magpie_worker_published(unsigned epoch, bool accepted) {
    portENTER_CRITICAL(&s_publication_lock);
    const bool current = note4_magpie_worker_current(epoch);
    if (current) atomic_store(&s_publication, accepted ? 1 : -1);
    portEXIT_CRITICAL(&s_publication_lock);
    if (current && s_task != NULL) xTaskNotifyGive(s_task);
}
static note4_magpie_handler_t s_handler;
static void *s_context;
static uint64_t now_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }

static void report(unsigned epoch, note4_magpie_phase_t phase, note4_magpie_result_t result) {
    if (atomic_load(&s_epoch) != epoch) return;
    const note4_magpie_event_t event = {.epoch = epoch, .phase = phase, .result = result};
    s_handler(&event, s_context);
}

static note4_magpie_result_t synchronize(unsigned epoch) {
    if (time(NULL) >= 1704067200) return NOTE4_MAGPIE_OK;
    report(epoch, NOTE4_MAGPIE_CLOCK_SYNC, NOTE4_MAGPIE_OK);
    const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    const esp_err_t initialized = esp_netif_sntp_init(&config);
    if (initialized != ESP_OK && initialized != ESP_ERR_INVALID_STATE) return NOTE4_MAGPIE_NETWORK;
    const int64_t started = esp_timer_get_time();
    /* Block in SNTP's own wait, in 1 s slices so cancellation is still observed. */
    while (time(NULL) < 1704067200) {
        if (atomic_load(&s_epoch) != epoch) return NOTE4_MAGPIE_CANCELLED;
        if (esp_timer_get_time() - started >= 30000000) return NOTE4_MAGPIE_TIMEOUT;
        (void)esp_netif_sntp_sync_wait(pdMS_TO_TICKS(1000));
    }
    return NOTE4_MAGPIE_OK;
}

static bool publish(unsigned epoch, const note4_magpie_poll_t *poll, note4_quota_snapshot_t *snapshot) {
    if (atomic_load(&s_epoch) != epoch || poll->mode == NOTE4_POLL_SETUP) return false;
    const uint64_t now = now_ms();
    const uint64_t wait = poll->next_due > now ? (poll->next_due - now + 999U) / 1000U : 0U;
    const note4_magpie_event_t event = {.epoch = epoch,
        .phase = poll->failed || poll->halted ? NOTE4_MAGPIE_FAILED
            : poll->has_snapshot ? NOTE4_MAGPIE_READY : NOTE4_MAGPIE_SIGNED_IN,
        .result = poll->result, .snapshot = snapshot,
        .reading = poll->reading,
        .retry_seconds = wait > UINT32_MAX ? UINT32_MAX : (uint32_t)wait};
    return s_handler(&event, s_context);
}

static void worker(void *context) {
    (void)context;
    command_t command;
    note4_magpie_poll_t poll;
    note4_magpie_poll_init(&poll);
    poll.defer_publication = true;
    unsigned epoch = 0U;
    uint64_t next_status = 0U, next_diagnostic = 0U;
    /* A keep-alive connection survives only from one step to an immediate next
     * one (sign-in followed by the first read); it is closed otherwise. */
    note4_magpie_transport_t transport;
    note4_magpie_io_t io;
    note4_magpie_transport_init(&transport, &s_epoch, 0U, &io);
    for (;;) {
        /* Sleep until the earliest deadline: next poll, failure status refresh
         * or diagnostics. Commands wake the task immediately. */
        const uint64_t now = now_ms();
        uint64_t wake = next_diagnostic;
        if (atomic_load(&s_epoch) == epoch) {
            if (poll.mode == NOTE4_POLL_ONLINE && !poll.halted && poll.next_due < wake) wake = poll.next_due;
            if (poll.failed && !poll.halted && next_status < wake) wake = next_status;
        }
        TickType_t timeout = 0;
        if (wake > now) {
            const uint64_t delay = wake - now;
            timeout = delay >= 3600000U ? pdMS_TO_TICKS(3600000U) : pdMS_TO_TICKS((uint32_t)delay);
            if (timeout == 0) timeout = 1;
        }
        if (xQueueReceive(s_commands, &command, timeout) == pdTRUE) {
            if (strcmp(command.settings.magpie.endpoint, poll.settings.magpie.endpoint) != 0) {
                note4_magpie_transport_close(&transport);
                note4_magpie_transport_forget_session();
            }
            epoch = command.epoch;
            note4_magpie_poll_configure(&poll, &command.settings, command.mode, now_ms());
            memset(&command, 0, sizeof(command));
            publish(epoch, &poll, NULL);
        }
        if (now_ms() >= next_diagnostic) {
            note4_resource_log("worker", 0U);
            next_diagnostic = now_ms() + 60000U;
        }
        if (atomic_load(&s_epoch) != epoch) { note4_magpie_transport_close(&transport); continue; }
        if (poll.failed && !poll.halted && now_ms() >= next_status) {
            publish(epoch, &poll, NULL);
            next_status = now_ms() + 15000U;
        }
        if (poll.mode != NOTE4_POLL_ONLINE || poll.halted) { note4_magpie_transport_close(&transport); continue; }
        if (now_ms() < poll.next_due) continue;
        if (!poll.failed) report(epoch, poll.login_pending ? NOTE4_MAGPIE_SIGNING_IN : NOTE4_MAGPIE_READING, poll.result);
        if (strncmp(poll.settings.magpie.endpoint, "https://", 8U) == 0 && time(NULL) < 1704067200) {
            const note4_magpie_result_t synced = synchronize(epoch);
            if (synced == NOTE4_MAGPIE_CANCELLED) continue;
            if (synced != NOTE4_MAGPIE_OK) {
                note4_magpie_response_t response = {0};
                note4_magpie_poll_fail(&poll, synced, &response, now_ms(), (int64_t)time(NULL), esp_random());
                publish(epoch, &poll, NULL); continue;
            }
        }
        transport.expected_epoch = epoch;
        transport.on_wait = NULL; transport.wait_context = NULL;
        note4_quota_snapshot_t *snapshot = NULL;
        if (note4_magpie_poll_step(&poll, &io, (int64_t)time(NULL), esp_random(), &snapshot)) {
            atomic_store(&s_publication, 0);
            const bool queued = publish(epoch, &poll, snapshot);
            if (snapshot != NULL) {
                /* Invariant: the EPD operation owns completion and always reports
                 * accepted/rejected through note4_magpie_worker_published(), which
                 * notifies this task. Bump the epoch first so a late acceptance can
                 * never race the next candidate. The 10 min bound only guards
                 * against a wedged display path. */
                const TickType_t wait_started = xTaskGetTickCount();
                while (queued && note4_magpie_worker_current(epoch) && !atomic_load(&s_publication)) {
                    const TickType_t waited = xTaskGetTickCount() - wait_started;
                    if (waited >= pdMS_TO_TICKS(600000U)) {
                        portENTER_CRITICAL(&s_publication_lock);
                        if (atomic_load(&s_publication) == 0) atomic_store(&s_publication, -1);
                        portEXIT_CRITICAL(&s_publication_lock);
                        ESP_LOGE("magpie_worker", "display publication did not complete; treating it as rejected");
                        break;
                    }
                    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(600000U) - waited);
                }
                if (atomic_load(&s_publication) == 1) {
                    note4_magpie_poll_accept(&poll, now_ms());
                } else if (note4_magpie_worker_current(epoch)) {
                    note4_magpie_response_t response = {0};
                    note4_magpie_poll_fail(&poll, NOTE4_MAGPIE_RESOURCE_EXHAUSTED, &response, now_ms(), (int64_t)time(NULL), esp_random());
                }
                note4_quota_order_clear(&poll.pending_order);
                publish(epoch, &poll, NULL);
            }
        }
        note4_quota_snapshot_release(snapshot);
        if (atomic_load(&s_epoch) != epoch || poll.next_due > now_ms() + 1000U) note4_magpie_transport_close(&transport);
    }
}

esp_err_t note4_magpie_worker_init(note4_magpie_handler_t handler, void *context) {
    s_handler = handler; s_context = context;
    atomic_init(&s_epoch, 0U);
    s_commands = xQueueCreate(1U, sizeof(command_t));
    if (s_commands == NULL) return ESP_ERR_NO_MEM;
    /* Settings, HTTP parsing and TLS overflowed the original 16 KiB stack.
     * Share CPU time with idle during synchronous certificate verification. */
    return xTaskCreate(worker, "magpie_web", 32768U, NULL, tskIDLE_PRIORITY, &s_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

unsigned note4_magpie_worker_update(const note4_settings_t *settings, note4_poll_mode_t mode) {
    command_t command = {.settings = *settings, .mode = mode};
    portENTER_CRITICAL(&s_publication_lock);
    command.epoch = atomic_fetch_add(&s_epoch, 1U) + 1U;
    portEXIT_CRITICAL(&s_publication_lock);
    (void)xQueueOverwrite(s_commands, &command);
    if (s_task != NULL) xTaskNotifyGive(s_task); /* ends a pending publication wait */
    const unsigned epoch = command.epoch;
    memset(&command, 0, sizeof(command));
    return epoch;
}
