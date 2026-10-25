#include "note4_screen.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pocketjs/guest.h"
#include "pocketjs/guest_quickjs.h"
#include "pocketjs/package.h"
#include "pocketjs/render_rgb565.h"
#include "pocketjs/ui_core.h"
#include "pocketjs/ui_qjs.h"
#include "pocketjs_package_note4_hello.h"
#include "zectrix_epd.h"
#include "note4_presenter.h"
#include "ble_provisioning.h"

static const char *TAG = "note4_screen";
static QueueHandle_t s_snapshots;
static QueueHandle_t s_buttons;
static note4_screen_action_handler_t s_action_handler;
static void *s_action_context;

typedef struct {
    pocketjs_package_t *package;
    pocketjs_guest_t *guest;
    pocketjs_ui_core_t *core;
    pocketjs_ui_qjs_t *binding;
    pocketjs_rgb565_renderer_t *renderer;
    pocketjs_rgb565_target_t *target;
    note4_presenter_t *presenter;
    zectrix_epd_handle_t epd;
    bool grayscale;
} note4_screen_context_t;

static note4_screen_context_t s_screen;

static const char *state_name(note4_provisioning_state_t state) {
    switch (state) {
    case NOTE4_PROVISIONING_CONFIGURING: return "configuring";
    case NOTE4_PROVISIONING_DISCARD_DIALOG: return "discard";
    case NOTE4_PROVISIONING_APPLYING: return "applying";
    case NOTE4_PROVISIONING_CONNECTING: return "connecting";
    case NOTE4_PROVISIONING_CONNECTED: return "connected";
    case NOTE4_PROVISIONING_FAILURE: return "failure";
    default: return "boot";
    }
}

static JSValue request_action(JSContext *context, JSValueConst self,
                               int argc, JSValueConst *argv) {
    (void)self;
    int32_t action;
    if (argc != 1 || JS_ToInt32(context, &action, argv[0]) < 0) return JS_EXCEPTION;
    if (action != NOTE4_PROVISIONING_EVENT_OK && action != NOTE4_PROVISIONING_EVENT_CANCEL &&
        action != NOTE4_PROVISIONING_EVENT_LONG_DOWN) {
        return JS_ThrowRangeError(context, "Unknown NOTE4 action");
    }
    if (s_action_handler != NULL) s_action_handler(action, s_action_context);
    return JS_UNDEFINED;
}

static JSValue set_display_mode(JSContext *context, JSValueConst self,
                                 int argc, JSValueConst *argv) {
    (void)self;
    if (argc != 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(context, "Display mode must be 1bpp or 4bpp");
    }
    const char *mode = JS_ToCString(context, argv[0]);
    if (mode == NULL) return JS_EXCEPTION;
    const bool grayscale = strcmp(mode, "4bpp") == 0;
    const bool valid = grayscale || strcmp(mode, "1bpp") == 0;
    JS_FreeCString(context, mode);
    if (!valid) return JS_ThrowRangeError(context, "Display mode must be 1bpp or 4bpp");
    if (s_screen.grayscale != grayscale) {
        s_screen.grayscale = grayscale;
        if (s_screen.target != NULL) pocketjs_rgb565_target_invalidate(s_screen.target);
    }
    return JS_UNDEFINED;
}

static esp_err_t install_surface(JSContext *context, void *user_data) {
    (void)user_data;
    JSValue global = JS_GetGlobalObject(context);
    JSValue note4 = JS_NewObject(context);
    JSValue ble = JS_NewObject(context);
    JSValue display = JS_NewObject(context);
    int result = JS_SetPropertyStr(context, note4, "status", JS_NewObject(context));
    result |= JS_SetPropertyStr(context, ble, "name", JS_NewString(context, note4_ble_provisioning_name()));
    result |= JS_SetPropertyStr(context, ble, "requestAction",
                               JS_NewCFunction(context, request_action, "requestAction", 1));
    result |= JS_SetPropertyStr(context, note4, "ble", ble);
    result |= JS_SetPropertyStr(context, display, "setMode",
                               JS_NewCFunction(context, set_display_mode, "setMode", 1));
    result |= JS_SetPropertyStr(context, note4, "display", display);
    result |= JS_SetPropertyStr(context, global, "note4", note4);
    JS_FreeValue(context, global);
    return result < 0 ? ESP_FAIL : ESP_OK;
}

static esp_err_t update_surface(const note4_provisioning_snapshot_t *snapshot) {
    JSContext *context = pocketjs_guest_quickjs_context(s_screen.guest);
    if (context == NULL) return ESP_ERR_INVALID_STATE;
    JSValue global = JS_GetGlobalObject(context);
    JSValue note4 = JS_GetPropertyStr(context, global, "note4");
    JSValue status = JS_NewObject(context);
    int result = JS_SetPropertyStr(context, status, "state", JS_NewString(context, state_name(snapshot->state)));
    result |= JS_SetPropertyStr(context, status, "slot", JS_NewInt32(context, snapshot->slot));
    result |= JS_SetPropertyStr(context, status, "round", JS_NewInt32(context, snapshot->round));
    result |= JS_SetPropertyStr(context, status, "pairing", JS_NewBool(context, snapshot->pairing));
    result |= JS_SetPropertyStr(context, status, "passkey", JS_NewUint32(context, snapshot->passkey));
    result |= JS_SetPropertyStr(context, note4, "status", status);
    JS_FreeValue(context, note4);
    JS_FreeValue(context, global);
    return result < 0 ? ESP_FAIL : ESP_OK;
}

static esp_err_t render_frame(uint32_t buttons) {
    pocketjs_ui_input_t input = {.struct_size = sizeof(input), .buttons = buttons};
    pocketjs_ui_frame_view_t frame = {.struct_size = sizeof(frame)};
    esp_err_t err = pocketjs_ui_turn(s_screen.binding, &input, &frame);
    if (err != ESP_OK) return err;
    pocketjs_rgb565_damage_plan_t plan = {.struct_size = sizeof(plan)};
    err = pocketjs_rgb565_prepare(s_screen.renderer, s_screen.target, &frame, &plan);
    if (err != ESP_OK) return err;
    if (frame.logical_width != NOTE4_WIDTH || frame.logical_height != NOTE4_HEIGHT ||
        plan.region_count > POCKETJS_RGB565_MAX_DAMAGE_REGIONS) {
        err = ESP_ERR_INVALID_SIZE;
        goto fail;
    }
    uint16_t *destination = note4_presenter_rgb565_frame(s_screen.presenter);
    note4_rect_t damage[POCKETJS_RGB565_MAX_DAMAGE_REGIONS];
    for (uint32_t index = 0U; index < plan.region_count; ++index) {
        const pocketjs_rgb565_rect_t region = plan.regions[index];
        if (region.x >= NOTE4_WIDTH || region.y >= NOTE4_HEIGHT ||
            region.width > NOTE4_WIDTH - region.x || region.height > NOTE4_HEIGHT - region.y) {
            err = ESP_ERR_INVALID_ARG;
            goto fail;
        }
        damage[index] = (note4_rect_t){region.x, region.y, region.width, region.height};
        pocketjs_rgb565_render_stats_t stats = {.struct_size = sizeof(stats)};
        err = pocketjs_rgb565_render_strip(s_screen.renderer, &frame,
            destination + (size_t)region.y * NOTE4_WIDTH, (size_t)NOTE4_WIDTH * region.height,
            region, NULL, &stats);
        if (err != ESP_OK) goto fail;
    }
    if (plan.region_count != 0U) {
        size_t black_pixels = 0U;
        for (size_t pixel = 0U; pixel < NOTE4_WIDTH * NOTE4_HEIGHT; ++pixel) {
            if (destination[pixel] < 0x8000U) ++black_pixels;
        }
        ESP_LOGI(TAG, "frame plan: regions=%u full=%d black_pixels=%u/%u",
                 (unsigned)plan.region_count, plan.full_redraw, (unsigned)black_pixels,
                 NOTE4_WIDTH * NOTE4_HEIGHT);
        const note4_rect_t full = {0U, 0U, NOTE4_WIDTH, NOTE4_HEIGHT};
        note4_present_mode_t mode;
        err = zectrix_epd_power_on(s_screen.epd);
        if (err != ESP_OK) goto fail;
        /* Gray pixels from text antialiasing do not opt the page into 4bpp. */
        err = note4_presenter_present(s_screen.presenter, s_screen.epd, damage,
                                      plan.region_count, plan.full_redraw,
                                      s_screen.grayscale ? &full : NULL,
                                      s_screen.grayscale ? 1U : 0U, 128U, &mode);
        const esp_err_t off_err = zectrix_epd_power_off(s_screen.epd);
        if (err == ESP_OK) err = off_err;
        if (err != ESP_OK) goto fail;
        ESP_LOGI(TAG, "PocketJS frame presented in %s mode",
                 mode == NOTE4_PRESENT_MODE_GRAY ? "4bpp" : "1bpp");
    }
    return pocketjs_rgb565_commit(s_screen.renderer, s_screen.target, &frame);
fail:
    pocketjs_rgb565_abort(s_screen.renderer, s_screen.target);
    return err;
}

static esp_err_t create_runtime(void) {
    esp_err_t err = pocketjs_package_open(pocketjs_package_note4_hello.data,
        pocketjs_package_note4_hello.size, 0U, &s_screen.package);
    if (err != ESP_OK) return err;
    pocketjs_package_variant_t app = {.struct_size = sizeof(app)};
    err = pocketjs_package_select(s_screen.package, &pocketjs_package_note4_hello_contract, &app);
    if (err != ESP_OK) return err;
    pocketjs_guest_config_t guest_config;
    pocketjs_guest_config_defaults(&guest_config);
    guest_config.stack_limit = 48U * 1024U;
    err = pocketjs_guest_create(&guest_config, &s_screen.guest);
    if (err != ESP_OK) return err;
    pocketjs_ui_core_config_t core_config;
    pocketjs_ui_core_config_defaults(&core_config);
    core_config.logical_width = pocketjs_package_note4_hello_contract.logical_width;
    core_config.logical_height = pocketjs_package_note4_hello_contract.logical_height;
    core_config.raster_density = pocketjs_package_note4_hello_contract.raster_density;
    core_config.tick_hz = pocketjs_package_note4_hello_contract.tick_hz;
    err = pocketjs_ui_core_create(&core_config, &s_screen.core);
    if (err != ESP_OK) return err;
    const pocketjs_ui_qjs_config_t binding_config = {
        .struct_size = sizeof(binding_config),
        .target_id = pocketjs_package_note4_hello_contract.target_id,
        .host_abi = pocketjs_package_note4_hello_contract.host_abi,
    };
    err = pocketjs_ui_qjs_create(s_screen.guest, s_screen.core, &binding_config, &s_screen.binding);
    if (err != ESP_OK) return err;
    err = pocketjs_ui_qjs_feed_pak(s_screen.binding, app.pak.data, app.pak.size);
    if (err != ESP_OK) return err;
    err = pocketjs_guest_quickjs_install_once(s_screen.guest, "note4", install_surface, NULL);
    if (err != ESP_OK) return err;
    err = pocketjs_ui_qjs_mount(s_screen.binding);
    if (err != ESP_OK) return err;
    err = pocketjs_guest_eval(s_screen.guest, (const char *)app.javascript.data,
                              app.javascript.size - 1U, "note4-status");
    if (err != ESP_OK) return err;
    pocketjs_rgb565_renderer_config_t renderer_config;
    pocketjs_rgb565_renderer_config_defaults(&renderer_config);
    renderer_config.scale = pocketjs_package_note4_hello_contract.raster_density;
    err = pocketjs_rgb565_renderer_create(&renderer_config, &s_screen.renderer);
    if (err != ESP_OK) return err;
    err = pocketjs_rgb565_target_create(&s_screen.target);
    if (err != ESP_OK) return err;
    err = note4_presenter_create(&s_screen.presenter);
    if (err != ESP_OK) return err;
    zectrix_epd_config_t epd_config;
    zectrix_epd_get_default_config(&epd_config);
    return zectrix_epd_new(&epd_config, &s_screen.epd);
}

static void screen_task(void *arg) {
    (void)arg;
    ESP_ERROR_CHECK(create_runtime());
    note4_provisioning_snapshot_t snapshot;
    note4_provisioning_init(&snapshot);
    uint32_t previous_buttons = 0U;
    for (;;) {
        if (xQueueReceive(s_snapshots, &snapshot, 0U) == pdTRUE) {
            ESP_ERROR_CHECK(update_surface(&snapshot));
        }
        uint32_t buttons = 0U;
        if (previous_buttons == 0U) (void)xQueueReceive(s_buttons, &buttons, 0U);
        previous_buttons = buttons;
        const esp_err_t err = render_frame(buttons);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "PocketJS render failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        /* EPD refresh can take seconds; do not run accumulated UI turns to catch up. */
        vTaskDelay(pdMS_TO_TICKS(1000U / pocketjs_package_note4_hello_contract.tick_hz));
    }
}

esp_err_t note4_screen_init(note4_screen_action_handler_t handler, void *context) {
    s_action_handler = handler;
    s_action_context = context;
    s_snapshots = xQueueCreate(1U, sizeof(note4_provisioning_snapshot_t));
    s_buttons = xQueueCreate(8U, sizeof(uint32_t));
    if (s_snapshots == NULL || s_buttons == NULL) return ESP_ERR_NO_MEM;
    /* This task owns QuickJS/rendering; radio and NVS operations stay on the coordinator. */
    return xTaskCreateWithCaps(screen_task, "note4_screen", 65536U, NULL, 4, NULL,
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t note4_screen_render(const note4_provisioning_snapshot_t *snapshot) {
    if (snapshot == NULL || s_snapshots == NULL) return ESP_ERR_INVALID_STATE;
    return xQueueOverwrite(s_snapshots, snapshot) == pdTRUE ? ESP_OK : ESP_FAIL;
}

void note4_screen_button(note4_button_id_t button) {
    /* PocketJS BTN.UP, BTN.DOWN and BTN.CIRCLE. Release is delivered on the next turn. */
    const uint32_t mask = button == NOTE4_BUTTON_UP ? 0x10U
        : button == NOTE4_BUTTON_DOWN ? 0x40U : 0x2000U;
    (void)xQueueSend(s_buttons, &mask, 0U);
}
