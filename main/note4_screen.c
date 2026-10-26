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
static uint8_t *s_previous_frame;
static bool s_has_frame;

typedef struct {
    const note4_quota_snapshot_t *quotas;
    const note4_display_prefs_t *prefs;
} quota_source_t;

static const char *source_text(const void *context, size_t card, uint32_t section,
                               size_t child, const char *field) {
    const quota_source_t *source = context;
    if (section == 1U) return note4_quota_window_text(source->quotas, card, child, field);
    if (section == 2U) {
        const char *value = note4_quota_balance_text(source->quotas, card, child, field);
        return !strcmp(field, "error") && value[0] ? "unavailable" : value;
    }
    if (!strcmp(field, "readDeferred")) return note4_quota_flag(source->quotas, card, field) ? "true" : "false";
    /* Provider error bodies never cross into the UI. */
    if (!strcmp(field, "error")) return note4_quota_text(source->quotas, card, field)[0] ? "unavailable" : "";
    return note4_quota_text(source->quotas, card, field);
}
static size_t source_children(const void *context, size_t card, uint32_t section) {
    const quota_source_t *source = context;
    return section == 1U ? note4_quota_window_count(source->quotas, card)
        : note4_quota_balance_count(source->quotas, card);
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
static note4_ui_status_t ui_status(const note4_provisioning_snapshot_t *snapshot) {
    return (note4_ui_status_t){
        .state = snapshot->state, .slot = snapshot->slot, .round = snapshot->round,
        .storage_error = snapshot->storage_error, .pairing = snapshot->pairing,
        .passkey = snapshot->passkey, .phase = snapshot->magpie_phase,
        .result = snapshot->magpie_result, .error_page = snapshot->magpie_error_page,
        .reading = snapshot->magpie_reading, .retry_seconds = snapshot->magpie_retry_seconds,
        .layout = snapshot->display_layout, .adaptive = snapshot->adaptive_font,
        .now = (int64_t)time(NULL), .timezone = snapshot->display_timezone[0] ? snapshot->display_timezone : "UTC",
        .ble_name = note4_ble_provisioning_name(),
    };
}
static esp_err_t build_layout(note4_ui_t *ui, const note4_quota_snapshot_t *quotas,
                              const note4_ui_status_t *status) {
    const quota_source_t context = {quotas, s_prefs};
    const note4_ui_source_t source = {&context, note4_quota_count(quotas), source_text,
        source_children, source_visible, source_rank};
    return note4_ui_build(ui, &source, status) == 0 ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t render_frame(const note4_ui_t *ui, const note4_ui_status_t *status,
                               uint32_t page, bool available, uint32_t buttons, unsigned layout_ms) {
    const int64_t started = esp_timer_get_time();
    uint16_t *pixels = note4_presenter_rgb565_frame(s_presenter);
    if (note4_ui_draw(ui, status, page, available, pixels, note4_presenter_rgb565_pixels()) != 0) return ESP_FAIL;
    unsigned min_x = NOTE4_WIDTH, min_y = NOTE4_HEIGHT, max_x = 0U, max_y = 0U;
    for (unsigned y = 0U; y < NOTE4_HEIGHT; ++y) {
        for (unsigned x = 0U; x < NOTE4_WIDTH; ++x) {
            const unsigned index = y * NOTE4_WIDTH + x;
            const bool black = pixels[index] == 0U;
            const bool previous = (s_previous_frame[index / 8U] & (0x80U >> (index % 8U))) != 0U;
            if (!s_has_frame || black != previous) {
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
        }
    }
    const int64_t drawn = esp_timer_get_time();
    esp_err_t err = ESP_OK;
    if (min_y < NOTE4_HEIGHT) {
        /* Align partial updates with the controller's byte columns. */
        min_x &= ~7U; max_x = max_x | 7U;
        const note4_rect_t damage = {min_x, min_y, max_x - min_x + 1U, max_y - min_y + 1U};
        note4_present_mode_t mode;
        err = zectrix_epd_power_on(s_epd);
        if (err == ESP_OK) {
            err = note4_presenter_present(s_presenter, s_epd, &damage, 1U, !s_has_frame,
                                          NULL, 0U, 128U, &mode);
            const esp_err_t off = zectrix_epd_power_off(s_epd);
            if (err == ESP_OK) err = off;
        }
        if (err == ESP_OK) {
            memset(s_previous_frame, 0, NOTE4_FRAME_BYTES);
            for (unsigned index = 0U; index < NOTE4_WIDTH * NOTE4_HEIGHT; ++index)
                if (pixels[index] == 0U) s_previous_frame[index / 8U] |= 0x80U >> (index % 8U);
            s_has_frame = true;
            ESP_LOGI(TAG, "Rust frame presented in 1bpp mode");
        } else s_has_frame = false;
    }
    ESP_LOGI(TAG, "frame_timing buttons=%u ui_ms=%u draw_ms=%u panel_ms=%u regions=%u",
        (unsigned)buttons, layout_ms, (unsigned)((drawn - started) / 1000),
        (unsigned)((esp_timer_get_time() - drawn) / 1000), min_y < NOTE4_HEIGHT ? 1U : 0U);
    return err;
}
static esp_err_t create_runtime(void) {
    s_ui = heap_caps_aligned_alloc(note4_ui_align(), note4_ui_size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_candidate = heap_caps_aligned_alloc(note4_ui_align(), note4_ui_size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_previous_frame = heap_caps_calloc(1U, NOTE4_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_ui == NULL || s_candidate == NULL || s_previous_frame == NULL) return ESP_ERR_NO_MEM;
    note4_ui_init(s_ui); note4_ui_init(s_candidate);
    esp_err_t err = note4_presenter_create(&s_presenter);
    if (err != ESP_OK) return err;
    zectrix_epd_config_t config;
    zectrix_epd_get_default_config(&config);
    ESP_LOGI(TAG, "native_ui arena_bytes=%u buffers=2", (unsigned)note4_ui_size());
    return zectrix_epd_new(&config, &s_epd);
}
static void screen_task(void *arg) {
    (void)arg;
    ESP_ERROR_CHECK(create_runtime());
    note4_provisioning_snapshot_t snapshot;
    note4_provisioning_init(&snapshot);
    note4_provisioning_snapshot_t laid_out = snapshot;
    uint32_t page = 0U;
    bool dirty = true, layout_dirty = true;
    int64_t last_minute = -1, next_diagnostic = 0;
    for (;;) {
        if (xQueueReceive(s_snapshots, &snapshot, 0U) == pdTRUE) dirty = true;
        note4_ui_status_t status = ui_status(&snapshot);
        const bool top_changed = (laid_out.state != NOTE4_PROVISIONING_CONNECTED || laid_out.magpie_phase == 6U)
            != (snapshot.state != NOTE4_PROVISIONING_CONNECTED || snapshot.magpie_phase == 6U);
        if (top_changed || laid_out.display_layout != snapshot.display_layout ||
            laid_out.adaptive_font != snapshot.adaptive_font || strcmp(laid_out.display_timezone, snapshot.display_timezone) ||
            last_minute != status.now / 60) layout_dirty = true;
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
            if (button == NOTE4_BUTTON_OK || snapshot.state == NOTE4_PROVISIONING_DISCARD_DIALOG) {
                if (s_action_handler != NULL) s_action_handler(button == NOTE4_BUTTON_OK
                    ? NOTE4_PROVISIONING_EVENT_OK : NOTE4_PROVISIONING_EVENT_CANCEL, s_action_context);
            } else if (!snapshot.storage_error && !snapshot.pairing && snapshot.state != NOTE4_PROVISIONING_CONFIGURING &&
                       button == NOTE4_BUTTON_UP && next_page > 0U) --next_page;
            else if (!snapshot.storage_error && !snapshot.pairing && snapshot.state != NOTE4_PROVISIONING_CONFIGURING &&
                     button == NOTE4_BUTTON_DOWN && next_page + 1U < pages) ++next_page;
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
            if (rebuild) { note4_ui_t *previous = s_ui; s_ui = s_candidate; s_candidate = previous; }
            page = next_page; laid_out = snapshot; last_minute = status.now / 60;
            dirty = false; layout_dirty = false;
        }
        if (esp_timer_get_time() >= next_diagnostic) {
            note4_resource_log("screen", note4_ui_used(s_ui));
            ESP_LOGI(TAG, "native_ui pages=%u page=%u layout_bytes=%u cards=%u", (unsigned)note4_ui_pages(s_ui),
                (unsigned)page, (unsigned)note4_ui_used(s_ui), (unsigned)note4_quota_count(s_quotas));
            next_diagnostic = esp_timer_get_time() + 60000000;
        }
        if (err != ESP_OK) { ESP_LOGE(TAG, "Rust render failed: %s", esp_err_to_name(err)); vTaskDelay(pdMS_TO_TICKS(1000)); }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
esp_err_t note4_screen_init(note4_screen_action_handler_t handler, void *context) {
    s_action_handler = handler; s_action_context = context;
    s_snapshots = xQueueCreate(1U, sizeof(note4_provisioning_snapshot_t));
    s_buttons = xQueueCreate(8U, sizeof(uint32_t));
    if (s_snapshots == NULL || s_buttons == NULL) return ESP_ERR_NO_MEM;
    return xTaskCreateWithCaps(screen_task, "note4_screen", 32768U, NULL, tskIDLE_PRIORITY, NULL,
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
esp_err_t note4_screen_render(const note4_provisioning_snapshot_t *snapshot) {
    if (snapshot == NULL || s_snapshots == NULL) return ESP_ERR_INVALID_STATE;
    return xQueueOverwrite(s_snapshots, snapshot) == pdTRUE ? ESP_OK : ESP_FAIL;
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
}
void note4_screen_button(note4_button_id_t button) {
    const uint32_t value = button;
    (void)xQueueSend(s_buttons, &value, 0U);
}
