#include "magpie_worker.h"

#include <stdatomic.h>
#include <string.h>
#include <time.h>
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

bool note4_magpie_worker_current(unsigned epoch) { return atomic_load(&s_epoch) == epoch; }
void note4_magpie_worker_published(unsigned epoch, bool accepted) {
    portENTER_CRITICAL(&s_publication_lock);
    if (note4_magpie_worker_current(epoch)) atomic_store(&s_publication, accepted ? 1 : -1);
    portEXIT_CRITICAL(&s_publication_lock);
}
static note4_magpie_handler_t s_handler;
static void *s_context;
static uint64_t now_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }

static void report(unsigned epoch, note4_magpie_phase_t phase, note4_magpie_result_t result) {
    if (atomic_load(&s_epoch) != epoch) return;
    const note4_magpie_event_t event = {.epoch = epoch, .phase = phase, .result = result};
    s_handler(&event, s_context);
}

typedef struct { unsigned epoch; const note4_magpie_poll_t *poll; bool error_page; } wait_state_t;
static void publish_wait(void *context);

static note4_magpie_result_t synchronize(unsigned epoch, wait_state_t *wait_state) {
    if (time(NULL) >= 1704067200) return NOTE4_MAGPIE_OK;
    report(epoch, NOTE4_MAGPIE_CLOCK_SYNC, NOTE4_MAGPIE_OK);
    const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    const esp_err_t initialized = esp_netif_sntp_init(&config);
    if (initialized != ESP_OK && initialized != ESP_ERR_INVALID_STATE) return NOTE4_MAGPIE_NETWORK;
    const int64_t started = esp_timer_get_time();
    while (time(NULL) < 1704067200) {
        if (atomic_load(&s_epoch) != epoch) return NOTE4_MAGPIE_CANCELLED;
        if (esp_timer_get_time() - started >= 30000000) return NOTE4_MAGPIE_TIMEOUT;
        publish_wait(wait_state);
        vTaskDelay(pdMS_TO_TICKS(50));
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
        .error_page = note4_magpie_poll_error_page(poll, now), .reading = poll->reading,
        .retry_seconds = wait > UINT32_MAX ? UINT32_MAX : (uint32_t)wait};
    return s_handler(&event, s_context);
}

static void publish_wait(void *context) {
    wait_state_t *state = context;
    const bool error_page = note4_magpie_poll_error_page(state->poll, now_ms());
    if (error_page != state->error_page) { publish(state->epoch, state->poll, NULL); state->error_page = error_page; }
}

static void worker(void *context) {
    (void)context;
    command_t command;
    note4_magpie_poll_t poll;
    note4_magpie_poll_init(&poll);
    poll.defer_publication = true;
    unsigned epoch = 0U;
    bool previous_error_page = false;
    uint64_t next_status = 0U, next_diagnostic = 0U;
    for (;;) {
        if (xQueueReceive(s_commands, &command, pdMS_TO_TICKS(50)) == pdTRUE) {
            epoch = command.epoch;
            note4_magpie_poll_configure(&poll, &command.settings, command.mode, now_ms());
            memset(&command, 0, sizeof(command));
            publish(epoch, &poll, NULL);
        }
        if (now_ms() >= next_diagnostic) {
            note4_resource_log("worker", 0U);
            next_diagnostic = now_ms() + 60000U;
        }
        if (atomic_load(&s_epoch) != epoch) continue;
        const bool error_page = note4_magpie_poll_error_page(&poll, now_ms());
        if (previous_error_page != error_page || (poll.failed && !poll.halted && now_ms() >= next_status)) {
            publish(epoch, &poll, NULL); previous_error_page = error_page;
            next_status = now_ms() + 15000U;
        }
        if (poll.mode != NOTE4_POLL_ONLINE || poll.halted || now_ms() < poll.next_due) continue;
        if (!poll.failed) report(epoch, poll.login_pending ? NOTE4_MAGPIE_SIGNING_IN : NOTE4_MAGPIE_READING, poll.result);
        wait_state_t wait_state = {.epoch = epoch, .poll = &poll, .error_page = error_page};
        if (strncmp(poll.settings.magpie.endpoint, "https://", 8U) == 0 && time(NULL) < 1704067200) {
            const note4_magpie_result_t synced = synchronize(epoch, &wait_state);
            if (synced == NOTE4_MAGPIE_CANCELLED) continue;
            if (synced != NOTE4_MAGPIE_OK) {
                note4_magpie_response_t response = {0};
                note4_magpie_poll_fail(&poll, synced, &response, now_ms(), (int64_t)time(NULL), esp_random());
                publish(epoch, &poll, NULL); continue;
            }
        }
        note4_magpie_transport_t transport;
        note4_magpie_io_t io;
        note4_magpie_transport_init(&transport, &s_epoch, epoch, &io);
        transport.on_wait = publish_wait; transport.wait_context = &wait_state;
        note4_quota_snapshot_t *snapshot = NULL;
        if (note4_magpie_poll_step(&poll, &io, (int64_t)time(NULL), esp_random(), &snapshot)) {
            atomic_store(&s_publication, 0);
            const bool queued = publish(epoch, &poll, snapshot);
            if (snapshot != NULL) {
                /* The bounded EPD operation owns completion. Do not time out here:
                 * a late accepted page must never race a subsequent candidate. */
                while (queued && note4_magpie_worker_current(epoch) && !atomic_load(&s_publication))
                    vTaskDelay(pdMS_TO_TICKS(20));
                if (atomic_load(&s_publication) == 1) {
                    note4_quota_order_clear(&poll.order); poll.order = poll.pending_order;
                    memset(&poll.pending_order, 0, sizeof(poll.pending_order));
                    poll.result = NOTE4_MAGPIE_OK; poll.failed = false; poll.failures = 0U; poll.has_snapshot = true;
                } else if (note4_magpie_worker_current(epoch)) {
                    note4_magpie_response_t response = {0};
                    note4_magpie_poll_fail(&poll, NOTE4_MAGPIE_RESOURCE_EXHAUSTED, &response, now_ms(), (int64_t)time(NULL), esp_random());
                }
                note4_quota_order_clear(&poll.pending_order);
                publish(epoch, &poll, NULL);
            }
        }
        note4_quota_snapshot_release(snapshot);
        previous_error_page = note4_magpie_poll_error_page(&poll, now_ms());
    }
}

esp_err_t note4_magpie_worker_init(note4_magpie_handler_t handler, void *context) {
    s_handler = handler; s_context = context;
    atomic_init(&s_epoch, 0U);
    s_commands = xQueueCreate(1U, sizeof(command_t));
    if (s_commands == NULL) return ESP_ERR_NO_MEM;
    /* Settings, HTTP parsing and TLS overflowed the original 16 KiB stack.
     * Share CPU time with idle during synchronous certificate verification. */
    return xTaskCreate(worker, "magpie_web", 32768U, NULL, tskIDLE_PRIORITY, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

unsigned note4_magpie_worker_update(const note4_settings_t *settings, note4_poll_mode_t mode) {
    command_t command = {.settings = *settings, .mode = mode};
    portENTER_CRITICAL(&s_publication_lock);
    command.epoch = atomic_fetch_add(&s_epoch, 1U) + 1U;
    portEXIT_CRITICAL(&s_publication_lock);
    (void)xQueueOverwrite(s_commands, &command);
    const unsigned epoch = command.epoch;
    memset(&command, 0, sizeof(command));
    return epoch;
}
