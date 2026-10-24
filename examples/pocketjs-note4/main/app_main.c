#include <assert.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "pocketjs/guest.h"
#include "pocketjs/package.h"
#include "pocketjs/render_rgb565.h"
#include "pocketjs/ui_core.h"
#include "pocketjs/ui_qjs.h"
#include "pocketjs_package_note4_hello.h"
#include "zectrix_epd.h"

#include "display_adapter.h"

#define NOTE4_VBAT_LATCH GPIO_NUM_17

static const char *TAG = "note4_hello";

static void keep_battery_latch_enabled(void) {
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << NOTE4_VBAT_LATCH,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    gpio_hold_dis(NOTE4_VBAT_LATCH);
    gpio_set_level(NOTE4_VBAT_LATCH, 1);
    gpio_hold_en(NOTE4_VBAT_LATCH);
}

static void render_frame(pocketjs_ui_frame_view_t *frame,
                         pocketjs_rgb565_renderer_t *renderer,
                         pocketjs_rgb565_target_t *target,
                         uint8_t *epaper_frame) {
    pocketjs_rgb565_damage_plan_t plan = {.struct_size = sizeof(plan)};
    ESP_ERROR_CHECK(pocketjs_rgb565_prepare(renderer, target, frame, &plan));
    if (frame->logical_width != NOTE4_WIDTH ||
        frame->logical_height != NOTE4_HEIGHT || !plan.full_redraw) {
        ESP_LOGE(TAG, "expected a 400x300 full redraw");
        abort();
    }

    uint16_t *rgb565_frame = heap_caps_malloc(
        NOTE4_WIDTH * NOTE4_HEIGHT * sizeof(*rgb565_frame),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(rgb565_frame != NULL);
    for (size_t pixel = 0; pixel < NOTE4_WIDTH * NOTE4_HEIGHT; ++pixel) {
        rgb565_frame[pixel] = 0xFFFF;
    }

    for (uint32_t index = 0; index < plan.region_count; ++index) {
        const pocketjs_rgb565_rect_t region = plan.regions[index];
        if (region.x >= NOTE4_WIDTH || region.y >= NOTE4_HEIGHT ||
            region.width > NOTE4_WIDTH - region.x ||
            region.height > NOTE4_HEIGHT - region.y) {
            ESP_LOGE(TAG, "renderer returned an out-of-range damage region");
            abort();
        }
        const size_t strip_pixels = (size_t)NOTE4_WIDTH * region.height;
        uint16_t *strip = rgb565_frame + (size_t)region.y * NOTE4_WIDTH;
        pocketjs_rgb565_render_stats_t stats = {.struct_size = sizeof(stats)};
        ESP_ERROR_CHECK(pocketjs_rgb565_render_strip(
            renderer, frame, strip, strip_pixels, region, NULL, &stats));
    }

    if (!note4_rgb565_to_1bpp_rows(
            rgb565_frame, NOTE4_WIDTH * NOTE4_HEIGHT, 0, NOTE4_HEIGHT,
            epaper_frame, NOTE4_FRAME_BYTES, 128)) {
        ESP_LOGE(TAG, "RGB565 conversion failed");
        abort();
    }
    heap_caps_free(rgb565_frame);
    ESP_ERROR_CHECK(pocketjs_rgb565_commit(renderer, target, frame));
}

void app_main(void) {
    keep_battery_latch_enabled();

    pocketjs_package_t *package = NULL;
    ESP_ERROR_CHECK(pocketjs_package_open(
        pocketjs_package_note4_hello.data, pocketjs_package_note4_hello.size,
        0, &package));
    pocketjs_package_variant_t app = {.struct_size = sizeof(app)};
    ESP_ERROR_CHECK(pocketjs_package_select(
        package, &pocketjs_package_note4_hello_contract, &app));

    pocketjs_guest_config_t guest_config;
    pocketjs_guest_config_defaults(&guest_config);
    pocketjs_guest_t *guest = NULL;
    ESP_ERROR_CHECK(pocketjs_guest_create(&guest_config, &guest));

    pocketjs_ui_core_config_t core_config;
    pocketjs_ui_core_config_defaults(&core_config);
    core_config.logical_width = pocketjs_package_note4_hello_contract.logical_width;
    core_config.logical_height = pocketjs_package_note4_hello_contract.logical_height;
    core_config.raster_density = pocketjs_package_note4_hello_contract.raster_density;
    core_config.tick_hz = pocketjs_package_note4_hello_contract.tick_hz;
    pocketjs_ui_core_t *core = NULL;
    ESP_ERROR_CHECK(pocketjs_ui_core_create(&core_config, &core));

    const pocketjs_ui_qjs_config_t binding_config = {
        .struct_size = sizeof(binding_config),
        .target_id = pocketjs_package_note4_hello_contract.target_id,
        .host_abi = pocketjs_package_note4_hello_contract.host_abi,
    };
    pocketjs_ui_qjs_t *binding = NULL;
    ESP_ERROR_CHECK(pocketjs_ui_qjs_create(guest, core, &binding_config, &binding));
    ESP_ERROR_CHECK(pocketjs_ui_qjs_feed_pak(binding, app.pak.data, app.pak.size));
    ESP_ERROR_CHECK(pocketjs_ui_qjs_mount(binding));
    ESP_ERROR_CHECK(pocketjs_guest_eval(guest, (const char *)app.javascript.data,
                                        app.javascript.size - 1U, "note4-hello"));

    pocketjs_ui_frame_view_t frame = {.struct_size = sizeof(frame)};
    pocketjs_ui_input_t input = {.struct_size = sizeof(input)};
    ESP_ERROR_CHECK(pocketjs_ui_turn(binding, &input, &frame));

    pocketjs_rgb565_renderer_config_t renderer_config;
    pocketjs_rgb565_renderer_config_defaults(&renderer_config);
    renderer_config.scale = pocketjs_package_note4_hello_contract.raster_density;
    pocketjs_rgb565_renderer_t *renderer = NULL;
    pocketjs_rgb565_target_t *target = NULL;
    ESP_ERROR_CHECK(pocketjs_rgb565_renderer_create(&renderer_config, &renderer));
    ESP_ERROR_CHECK(pocketjs_rgb565_target_create(&target));

    uint8_t *epaper_frame = heap_caps_malloc(
        NOTE4_FRAME_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(epaper_frame != NULL);
    render_frame(&frame, renderer, target, epaper_frame);

    zectrix_epd_config_t epd_config;
    zectrix_epd_get_default_config(&epd_config);
    zectrix_epd_handle_t epd = NULL;
    ESP_ERROR_CHECK(zectrix_epd_new(&epd_config, &epd));
    ESP_ERROR_CHECK(zectrix_epd_power_on(epd));
    ESP_ERROR_CHECK(zectrix_epd_refresh_full_1bpp(
        epd, epaper_frame, NOTE4_FRAME_BYTES));
    ESP_LOGI(TAG, "PocketJS Hello World full refresh complete");
    ESP_ERROR_CHECK(zectrix_epd_power_off(epd));
    ESP_ERROR_CHECK(zectrix_epd_del(epd));

    heap_caps_free(epaper_frame);
    pocketjs_rgb565_target_destroy(target);
    pocketjs_rgb565_renderer_destroy(renderer);
    pocketjs_guest_destroy(guest);
    pocketjs_ui_qjs_destroy(binding);
    pocketjs_ui_core_destroy(core);
    pocketjs_package_close(package);
}
