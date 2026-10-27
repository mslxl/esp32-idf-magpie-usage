#include "note4_screen.h"

#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "note4_ui.h"
#include "zectrix_epd.h"
#include "note4_presenter.h"
#include "ble_provisioning.h"
#include "magpie_worker.h"
#include "resource_budget.h"
#include "battery.h"
#ifdef NOTE4_DEV_CAPTURE
#include "dev_capture.h"
#endif

static const char *TAG = "note4_screen";
static QueueHandle_t s_snapshots, s_buttons;
static portMUX_TYPE s_quota_lock = portMUX_INITIALIZER_UNLOCKED;
static note4_quota_snapshot_t *s_pending_quotas, *s_quotas;
static bool s_quotas_changed;
static unsigned s_pending_epoch;
static note4_screen_quota_handler_t s_pending_handler;
static void *s_pending_context;
static note4_display_prefs_t *s_pending_prefs, *s_prefs;
static bool s_prefs_changed;
static note4_screen_action_handler_t s_action_handler;
static void *s_action_context;
static note4_ui_t *s_ui, *s_candidate;
static note4_presenter_t *s_presenter;
static zectrix_epd_handle_t s_epd;
static TaskHandle_t s_task;

/* The battery module samples at this cadence; it bounds the task's sleep. */
#define BATTERY_POLL_US 5000000LL
#ifdef NOTE4_DEV_CAPTURE
/* Development builds also service USB screenshot commands. */
#define CAPTURE_POLL_US 100000LL
#endif

static void wake_screen(void) {
    if (s_task != NULL) xTaskNotifyGive(s_task);
}

typedef struct {
    const note4_quota_snapshot_t *quotas;
    const note4_display_prefs_t *prefs;
} quota_source_t;

static void source_card(const void *context, size_t card, note4_ui_card_t *out) {
    const note4_quota_snapshot_t *quotas = ((const quota_source_t *)context)->quotas;
    *out = (note4_ui_card_t){
        .provider = note4_quota_text(quotas, card, "provider"),
        .name = note4_quota_text(quotas, card, "name"),
        .user = note4_quota_text(quotas, card, "user"),
        .plan = note4_quota_text(quotas, card, "plan"),
        .alias = note4_quota_text(quotas, card, "alias"),
        .seat = note4_quota_text(quotas, card, "seat"),
        .from = note4_quota_text(quotas, card, "from"),
        .kind = note4_quota_text(quotas, card, "kind"),
        .balance = note4_quota_text(quotas, card, "balance"),
        .until = note4_quota_text(quotas, card, "until"),
        .renew = note4_quota_text(quotas, card, "renew"),
        .as_of = note4_quota_text(quotas, card, "asOf"),
        .read_at = note4_quota_text(quotas, card, "readAt"),
        .read_deferred = note4_quota_flag(quotas, card, "readDeferred"),
        /* Provider error bodies never cross into the UI. */
        .failed = note4_quota_text(quotas, card, "error")[0] != '\0',
        .windows = note4_quota_window_count(quotas, card),
        .balances = note4_quota_balance_count(quotas, card),
    };
}
static void source_window(const void *context, size_t card, size_t child, note4_ui_window_t *out) {
    const note4_quota_snapshot_t *quotas = ((const quota_source_t *)context)->quotas;
#define WINDOW_TEXT(field) note4_quota_window_text(quotas, card, child, field)
    *out = (note4_ui_window_t){
        .name = WINDOW_TEXT("name"), .display = WINDOW_TEXT("display"),
        .used = WINDOW_TEXT("used"), .remaining = WINDOW_TEXT("remaining"),
        .resets_at = WINDOW_TEXT("resetsAt"), .reset_secs = WINDOW_TEXT("resetSecs"),
        .amount = WINDOW_TEXT("amount"), .limit = WINDOW_TEXT("limit"),
        .unit = WINDOW_TEXT("unit"), .pool = WINDOW_TEXT("pool"),
        .holds_why = WINDOW_TEXT("holdsWhy"),
        .unlimited = note4_quota_window_flag(quotas, card, child, "unlimited"),
    };
#undef WINDOW_TEXT
}
static void source_balance(const void *context, size_t card, size_t part, note4_ui_balance_t *out) {
    const note4_quota_snapshot_t *quotas = ((const quota_source_t *)context)->quotas;
    *out = (note4_ui_balance_t){
        .label = note4_quota_balance_text(quotas, card, part, "label"),
        .text = note4_quota_balance_text(quotas, card, part, "text"),
        .failed = note4_quota_balance_text(quotas, card, part, "error")[0] != '\0',
    };
}
static uint32_t source_visible(const void *context, size_t card) {
    const quota_source_t *source = context;
    return note4_display_prefs_visible(source->prefs, source->quotas, card);
}
static size_t source_rank(const void *context, size_t card) {
    const quota_source_t *source = context;
    const note4_display_prefs_t *prefs = source->prefs;
    if (prefs != NULL && prefs->order == 2U) return note4_quota_smart_rank(source->quotas, card);
    const char *provider = note4_quota_text(source->quotas, card, "provider");
    const char *user = note4_quota_text(source->quotas, card, "user");
    size_t rank = note4_display_prefs_rank(prefs, provider, user);
    if (prefs != NULL && prefs->order == 1U && rank == prefs->custom_count) {
        size_t first = 0U;
        while (first < card && (strcmp(provider, note4_quota_text(source->quotas, first, "provider")) ||
            strcmp(user, note4_quota_text(source->quotas, first, "user")))) ++first;
        rank += first;
    }
    return rank;
}
static note4_ui_status_t ui_status(const note4_provisioning_snapshot_t *snapshot,
                                   uint32_t wifi_retry_seconds) {
    return (note4_ui_status_t){
        .state = snapshot->state, .slot = snapshot->slot, .round = snapshot->round,
        .storage_error = snapshot->storage_error, .pairing = snapshot->pairing,
        .passkey = snapshot->passkey, .phase = snapshot->magpie_phase,
        .result = snapshot->magpie_result, .error_page = snapshot->magpie_error_page,
        .reading = snapshot->magpie_reading, .retry_seconds = snapshot->magpie_retry_seconds,
        .layout = snapshot->display_layout, .adaptive = snapshot->adaptive_font,
        .now = (int64_t)time(NULL), .timezone = snapshot->display_timezone[0] ? snapshot->display_timezone : "UTC",
        .ble_name = note4_ble_provisioning_name(),
        .language = snapshot->display_language,
        .wifi_retry_seconds = wifi_retry_seconds,
    };
}
static esp_err_t build_layout(note4_ui_t *ui, const note4_quota_snapshot_t *quotas,
                              const note4_ui_status_t *status) {
    const quota_source_t context = {quotas, s_prefs};
    const note4_ui_source_t source = {&context, note4_quota_count(quotas), source_card,
        source_window, source_balance, source_visible, source_rank};
    return note4_ui_build(ui, &source, status) == 0 ? ESP_OK : ESP_ERR_NO_MEM;
}

static const char *mode_name(note4_present_mode_t mode) {
    return mode == NOTE4_PRESENT_FULL ? "full" : mode == NOTE4_PRESENT_PARTIAL ? "partial" : "none";
}
static esp_err_t render_frame(const note4_ui_t *ui, const note4_ui_status_t *status,
                               uint32_t page, bool available, uint32_t buttons, unsigned layout_ms) {
    const int64_t started = esp_timer_get_time();
    if (note4_ui_draw(ui, status, page, available, note4_presenter_frame(s_presenter), NOTE4_FRAME_BYTES) != 0)
        return ESP_FAIL;
    note4_rect_t damage[NOTE4_DAMAGE_MAX_RECTS];
    const size_t count = note4_presenter_damage(s_presenter, damage, NOTE4_DAMAGE_MAX_RECTS);
    const int64_t drawn = esp_timer_get_time();
    note4_present_mode_t mode = NOTE4_PRESENT_NONE;
    esp_err_t err = ESP_OK;
    if (count != 0U) {
        err = zectrix_epd_power_on(s_epd);
        if (err == ESP_OK) {
            err = note4_presenter_present(s_presenter, s_epd, damage, count, false, drawn, &mode);
            const esp_err_t off = zectrix_epd_power_off(s_epd);
            if (err == ESP_OK) err = off;
        }
    }
    ESP_LOGI(TAG, "frame_timing buttons=%u ui_ms=%u draw_ms=%u panel_ms=%u regions=%u mode=%s",
        (unsigned)buttons, layout_ms, (unsigned)((drawn - started) / 1000),
        (unsigned)((esp_timer_get_time() - drawn) / 1000), (unsigned)count, mode_name(mode));
    return err;
}
static esp_err_t create_runtime(void) {
    s_ui = heap_caps_aligned_alloc(note4_ui_align(), note4_ui_size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_candidate = heap_caps_aligned_alloc(note4_ui_align(), note4_ui_size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_ui == NULL || s_candidate == NULL) return ESP_ERR_NO_MEM;
    note4_ui_init(s_ui); note4_ui_init(s_candidate);
    esp_err_t err = note4_presenter_create(&s_presenter);
    if (err != ESP_OK) return err;
    zectrix_epd_config_t config;
    zectrix_epd_get_default_config(&config);
    ESP_LOGI(TAG, "native_ui arena_bytes=%u buffers=2", (unsigned)note4_ui_size());
    return zectrix_epd_new(&config, &s_epd);
}
/* Counts the announced Wi-Fi retry delay down from the snapshot that set it. */
typedef struct {
    uint32_t announced;
    int64_t base_us;
} wifi_retry_t;

static void wifi_retry_observe(wifi_retry_t *retry, const note4_provisioning_snapshot_t *snapshot, int64_t now_us) {
    const uint32_t announced = snapshot->state == NOTE4_PROVISIONING_FAILURE ? snapshot->wifi_retry_seconds : 0U;
    if (announced != retry->announced) {
        retry->announced = announced;
        retry->base_us = now_us;
    }
}
static int64_t wifi_retry_remaining_us(const wifi_retry_t *retry, int64_t now_us) {
    const int64_t remaining = (int64_t)retry->announced * 1000000LL - (now_us - retry->base_us);
    return remaining > 0 ? remaining : 0;
}
static bool status_page(const note4_provisioning_snapshot_t *snapshot) {
    return snapshot->state != NOTE4_PROVISIONING_CONNECTED || snapshot->magpie_phase == NOTE4_MAGPIE_FAILED;
}
/* Sleep until the next battery sample, the displayed retry minute changes or
 * the committed layout goes stale, whichever comes first. */
static TickType_t wait_ticks(const wifi_retry_t *retry, int64_t now_us, int64_t now, int64_t stale_at) {
    int64_t wait_us = BATTERY_POLL_US;
#ifdef NOTE4_DEV_CAPTURE
    wait_us = CAPTURE_POLL_US;
#endif
    const int64_t remaining = wifi_retry_remaining_us(retry, now_us);
    if (remaining > 0) {
        const int64_t minute_step = (remaining - 1) % 60000000LL + 1;
        if (minute_step < wait_us) wait_us = minute_step;
    }
    if (stale_at > now && stale_at - now < wait_us / 1000000LL) wait_us = (stale_at - now) * 1000000LL;
    const TickType_t ticks = pdMS_TO_TICKS((uint32_t)((wait_us + 999) / 1000));
    return ticks == 0U ? 1U : ticks;
}
static void screen_task(void *arg) {
    (void)arg;
    s_task = xTaskGetCurrentTaskHandle();
    ESP_ERROR_CHECK(create_runtime());
#ifdef NOTE4_DEV_CAPTURE
    ESP_ERROR_CHECK(note4_capture_init());
#endif
    note4_battery_init();
    note4_battery_t shown_battery = {0};
    note4_provisioning_snapshot_t snapshot;
    note4_provisioning_init(&snapshot);
    note4_provisioning_snapshot_t laid_out = snapshot;
    wifi_retry_t wifi_retry = {0};
    uint32_t page = 0U, shown_retry_minutes = 0U;
    bool dirty = true, layout_dirty = true;
    int64_t built_at = 0, next_diagnostic = 0;
    for (;;) {
        const int64_t now_us = esp_timer_get_time();
        if (xQueueReceive(s_snapshots, &snapshot, 0U) == pdTRUE) dirty = true;
        wifi_retry_observe(&wifi_retry, &snapshot, now_us);
        const int64_t retry_us = wifi_retry_remaining_us(&wifi_retry, now_us);
        note4_ui_status_t status = ui_status(&snapshot, (uint32_t)((retry_us + 999999) / 1000000));
        const uint32_t retry_minutes = (status.wifi_retry_seconds + 59U) / 60U;
        if (retry_minutes != shown_retry_minutes) dirty = true;
        const note4_battery_t battery = note4_battery_poll(now_us);
        status.battery_percent = battery.percent;
        status.battery_flags = (battery.valid ? 1U : 0U) | (battery.charging ? 2U : 0U) | (battery.full ? 4U : 0U);
        if (battery.valid != shown_battery.valid || battery.charging != shown_battery.charging ||
            battery.full != shown_battery.full || (battery.valid && battery.percent != shown_battery.percent)) dirty = true;
        /* Absolute times only go stale at a reset, local midnight or an offset
         * change; a clock that moved backwards invalidates them too. */
        const int64_t stale_at = note4_ui_next_change(s_ui);
        if (status_page(&laid_out) != status_page(&snapshot) || laid_out.display_layout != snapshot.display_layout ||
            laid_out.adaptive_font != snapshot.adaptive_font || laid_out.display_language != snapshot.display_language ||
            strcmp(laid_out.display_timezone, snapshot.display_timezone) ||
            status.now >= stale_at || status.now < built_at) layout_dirty = true;
        portENTER_CRITICAL(&s_quota_lock);
        note4_quota_snapshot_t *next_quotas = s_pending_quotas; s_pending_quotas = NULL;
        const bool quotas_changed = s_quotas_changed; s_quotas_changed = false;
        const unsigned epoch = s_pending_epoch;
        note4_screen_quota_handler_t handler = s_pending_handler; s_pending_handler = NULL;
        void *context = s_pending_context;
        note4_display_prefs_t *next_prefs = s_pending_prefs; s_pending_prefs = NULL;
        const bool prefs_changed = s_prefs_changed; s_prefs_changed = false;
        portEXIT_CRITICAL(&s_quota_lock);
        if (prefs_changed) {
            note4_display_prefs_t *previous = s_prefs; s_prefs = next_prefs;
            note4_display_prefs_release(previous); layout_dirty = true;
        }
        const bool current = handler == NULL || note4_magpie_worker_current(epoch);
        note4_quota_snapshot_t *quotas = quotas_changed && current ? next_quotas : s_quotas;
        bool rebuild = layout_dirty || (quotas_changed && current);
        unsigned layout_ms = 0U;
        esp_err_t err = ESP_OK;
        if (rebuild) {
            const int64_t start = esp_timer_get_time();
            err = build_layout(s_candidate, quotas, &status);
            layout_ms = (unsigned)((esp_timer_get_time() - start) / 1000);
            dirty = true;
        }
        const note4_ui_t *draw_ui = rebuild && err == ESP_OK ? s_candidate : s_ui;
        const uint32_t pages = note4_ui_pages(draw_ui);
        uint32_t next_page = pages == 0U ? 0U : page < pages ? page : pages - 1U;
        uint32_t button = UINT32_MAX;
        if (xQueueReceive(s_buttons, &button, 0U) == pdTRUE) {
            const bool paging = !snapshot.storage_error && !snapshot.pairing &&
                                snapshot.state != NOTE4_PROVISIONING_CONFIGURING;
            if (button == NOTE4_BUTTON_OK || snapshot.state == NOTE4_PROVISIONING_DISCARD_DIALOG) {
                if (s_action_handler != NULL) s_action_handler(button == NOTE4_BUTTON_OK
                    ? NOTE4_PROVISIONING_EVENT_OK : NOTE4_PROVISIONING_EVENT_CANCEL, s_action_context);
            } else if (paging && button == NOTE4_BUTTON_UP && next_page > 0U) --next_page;
            else if (paging && button == NOTE4_BUTTON_DOWN && next_page + 1U < pages) ++next_page;
            dirty = true;
        }
        if (dirty && err == ESP_OK) err = render_frame(draw_ui, &status, next_page, quotas != NULL,
            button == UINT32_MAX ? 0U : button == NOTE4_BUTTON_UP ? 16U : button == NOTE4_BUTTON_DOWN ? 64U : 8192U, layout_ms);
        bool accepted = err == ESP_OK;
        if (quotas_changed) {
            accepted = accepted && current && (handler == NULL || note4_magpie_worker_current(epoch));
            if (handler != NULL) accepted = handler(next_quotas, epoch, accepted, context);
            if (accepted) {
                note4_quota_snapshot_t *previous = s_quotas; s_quotas = next_quotas;
                note4_quota_snapshot_release(previous);
            } else {
                note4_quota_snapshot_release(next_quotas); dirty = true; layout_dirty = true;
                /* A coordinator veto can occur after presentation; restore the committed page. */
                if (current && err == ESP_OK) (void)render_frame(s_ui, &status, page, s_quotas != NULL, 0U, 0U);
            }
            ESP_LOGI(TAG, "quota_publication accepted=%u materialized=%d error=%d cards=%u",
                accepted ? 1U : 0U, err == ESP_OK ? 1 : -1, (int)err, (unsigned)note4_quota_count(s_quotas));
        }
        if (accepted) {
            if (rebuild) {
                note4_ui_t *previous = s_ui; s_ui = s_candidate; s_candidate = previous;
                built_at = status.now;
            }
            page = next_page; laid_out = snapshot;
            shown_battery = battery; shown_retry_minutes = retry_minutes;
            dirty = false; layout_dirty = false;
        }
        if (esp_timer_get_time() >= next_diagnostic) {
            note4_resource_log("screen", note4_ui_used(s_ui));
            ESP_LOGI(TAG, "native_ui pages=%u page=%u layout_bytes=%u cards=%u", (unsigned)note4_ui_pages(s_ui),
                (unsigned)page, (unsigned)note4_ui_used(s_ui), (unsigned)note4_quota_count(s_quotas));
            next_diagnostic = esp_timer_get_time() + 60000000;
        }
        if (err != ESP_OK) { ESP_LOGE(TAG, "Rust render failed: %s", esp_err_to_name(err)); vTaskDelay(pdMS_TO_TICKS(1000)); }
#ifdef NOTE4_DEV_CAPTURE
        note4_capture_poll(note4_presenter_shown(s_presenter), page, note4_ui_pages(s_ui),
            accepted && s_quotas != NULL && status.state == NOTE4_PROVISIONING_CONNECTED &&
            !status.pairing && !status.storage_error && !status.error_page && status.phase != NOTE4_MAGPIE_FAILED);
#endif
        /* Pending work (a vetoed publication, a failed render or queued
         * buttons) loops again at once; otherwise sleep until woken. */
        if (dirty || layout_dirty || uxQueueMessagesWaiting(s_buttons) != 0U) continue;
        (void)ulTaskNotifyTake(pdTRUE, wait_ticks(&wifi_retry, esp_timer_get_time(),
            (int64_t)time(NULL), note4_ui_next_change(s_ui)));
    }
}
esp_err_t note4_screen_init(note4_screen_action_handler_t handler, void *context) {
    s_action_handler = handler; s_action_context = context;
    s_snapshots = xQueueCreate(1U, sizeof(note4_provisioning_snapshot_t));
    s_buttons = xQueueCreate(8U, sizeof(uint32_t));
    if (s_snapshots == NULL || s_buttons == NULL) return ESP_ERR_NO_MEM;
    return xTaskCreateWithCaps(screen_task, "note4_screen", 32768U, NULL, tskIDLE_PRIORITY, &s_task,
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
esp_err_t note4_screen_render(const note4_provisioning_snapshot_t *snapshot) {
    if (snapshot == NULL || s_snapshots == NULL) return ESP_ERR_INVALID_STATE;
    if (xQueueOverwrite(s_snapshots, snapshot) != pdTRUE) return ESP_FAIL;
    wake_screen();
    return ESP_OK;
}
void note4_screen_stage_quotas(note4_quota_snapshot_t *snapshot, unsigned epoch,
    note4_screen_quota_handler_t handler, void *context) {
    note4_quota_snapshot_retain(snapshot);
    portENTER_CRITICAL(&s_quota_lock);
    note4_quota_snapshot_t *previous = s_pending_quotas;
    note4_screen_quota_handler_t previous_handler = s_pending_handler;
    void *previous_context = s_pending_context;
    const unsigned previous_epoch = s_pending_epoch;
    s_pending_quotas = snapshot; s_pending_epoch = epoch;
    s_pending_handler = handler; s_pending_context = context; s_quotas_changed = true;
    portEXIT_CRITICAL(&s_quota_lock);
    if (previous_handler != NULL) previous_handler(previous, previous_epoch, false, previous_context);
    note4_quota_snapshot_release(previous);
    wake_screen();
}
void note4_screen_publish_quotas(note4_quota_snapshot_t *snapshot) {
    note4_screen_stage_quotas(snapshot, 0U, NULL, NULL);
}
void note4_screen_publish_prefs(note4_display_prefs_t *prefs) {
    note4_display_prefs_retain(prefs);
    portENTER_CRITICAL(&s_quota_lock);
    note4_display_prefs_t *previous = s_pending_prefs;
    s_pending_prefs = prefs; s_prefs_changed = true;
    portEXIT_CRITICAL(&s_quota_lock);
    note4_display_prefs_release(previous);
    wake_screen();
}
void note4_screen_button(note4_button_id_t button) {
    const uint32_t value = button;
    if (xQueueSend(s_buttons, &value, 0U) == pdTRUE) wake_screen();
}
