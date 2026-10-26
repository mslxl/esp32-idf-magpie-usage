#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "note4_presenter.h"
#include "esp_heap_caps.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed %d: %s\n", __LINE__, #x); exit(1); } } while (0)
struct zectrix_epd_t { int unused; };
static size_t allocations, live, full_calls, partial_calls, gray_calls;
static size_t fail_at, gray_allocations;
static bool drawing, require_external;
static esp_err_t driver_result, gray_result;
void *heap_caps_malloc(size_t bytes, uint32_t caps) {
    ++allocations;
    CHECK(!drawing || bytes == NOTE4_4BPP_FRAME_BYTES);
    if (bytes == NOTE4_4BPP_FRAME_BYTES) ++gray_allocations;
    if (require_external) CHECK(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (allocations == fail_at) return NULL;
    void *p = malloc(bytes);
    if (p) ++live;
    return p;
}
void heap_caps_free(void *p) { if (p) { CHECK(live); --live; } free(p); }
esp_err_t zectrix_epd_refresh_full_1bpp(zectrix_epd_handle_t epd, const uint8_t *pixels, size_t bytes) {
    CHECK(epd && pixels && bytes == NOTE4_FRAME_BYTES);
    CHECK(pixels[0] == 0xff);
    ++full_calls; return driver_result;
}
esp_err_t zectrix_epd_refresh_partial_1bpp(zectrix_epd_handle_t epd, const zectrix_epd_rect_t *rect, const uint8_t *pixels, size_t bytes) {
    CHECK(epd && pixels);
    if (partial_calls == 0) {
        CHECK(rect->x == 3 && rect->y == 1 && rect->width == 9 && rect->height == 2 && bytes == 4);
        const uint8_t expected[] = {0x55, 0x00, 0xaa, 0x80};
        CHECK(!memcmp(pixels, expected, sizeof(expected)));
    } else {
        CHECK(rect->x == 390 && rect->y == 299 && rect->width == 10 && rect->height == 1 && bytes == 2);
        CHECK(pixels[0] == 0x00 && pixels[1] == 0x00);
    }
    ++partial_calls; return driver_result;
}
esp_err_t zectrix_epd_refresh_full_4bpp(zectrix_epd_handle_t epd, const uint8_t *pixels, size_t bytes) {
    CHECK(epd && pixels && bytes == NOTE4_4BPP_FRAME_BYTES);
    ++gray_calls; return gray_result;
}
int main(int argc, char **argv) {
    (void)argv; require_external = argc > 1;
    for (size_t failure = 1; failure <= 2; ++failure) {
        allocations = 0; fail_at = failure;
        note4_presenter_t *candidate = NULL;
        CHECK(note4_presenter_create(&candidate) == ESP_ERR_NO_MEM && candidate == NULL && live == 0);
    }
    allocations = 0; fail_at = 0;
    note4_presenter_t *presenter = NULL;
    CHECK(note4_presenter_create(&presenter) == ESP_OK);
    struct zectrix_epd_t device = {0};
    note4_present_mode_t mode;
    drawing = true;
    CHECK(note4_presenter_present(presenter, &device, NULL, 0, true, NULL, 0, 128, &mode) == ESP_OK);
    CHECK(full_calls == 1 && mode == NOTE4_PRESENT_MODE_BW);
    uint16_t *frame = note4_presenter_rgb565_frame(presenter);
    for (size_t x = 0; x < 9; ++x) {
        frame[NOTE4_WIDTH + 3 + x] = x % 2 ? 0xffff : 0;
        frame[2 * NOTE4_WIDTH + 3 + x] = x % 2 ? 0 : 0xffff;
    }
    for (size_t x = 390; x < 400; ++x) frame[299 * NOTE4_WIDTH + x] = 0;
    const note4_rect_t damage[] = {{3, 1, 9, 2}, {390, 299, 10, 1}};
    CHECK(note4_presenter_present(presenter, &device, damage, 2, false, NULL, 0, 128, &mode) == ESP_OK);
    CHECK(partial_calls == 2 && full_calls == 1 && gray_allocations == 0);
    const note4_rect_t gray = {20, 20, 8, 8};
    fail_at = allocations + 1;
    CHECK(note4_presenter_present(presenter, &device, &gray, 1, true, &gray, 1, 128, &mode) == ESP_ERR_NO_MEM);
    CHECK(full_calls == 1 && gray_calls == 0 && live == 2 && mode == NOTE4_PRESENT_MODE_BW);
    fail_at = 0;
    CHECK(note4_presenter_present(presenter, &device, &gray, 1, true, &gray, 1, 128, &mode) == ESP_OK);
    CHECK(gray_calls == 1 && mode == NOTE4_PRESENT_MODE_GRAY && live == 2);
    CHECK(note4_presenter_present(presenter, &device, damage, 2, false, NULL, 0, 128, &mode) == ESP_OK);
    CHECK(full_calls == 3 && partial_calls == 2 && mode == NOTE4_PRESENT_MODE_BW);
    driver_result = ESP_FAIL;
    CHECK(note4_presenter_present(presenter, &device, damage + 1, 1, false, NULL, 0, 128, &mode) == ESP_FAIL);
    CHECK(note4_presenter_present(presenter, &device, &gray, 1, true, &gray, 1, 128, &mode) == ESP_FAIL);
    CHECK(live == 2 && gray_calls == 1);
    driver_result = ESP_OK; gray_result = ESP_FAIL;
    CHECK(note4_presenter_present(presenter, &device, &gray, 1, true, &gray, 1, 128, &mode) == ESP_FAIL);
    CHECK(live == 2 && gray_calls == 2);
    note4_presenter_destroy(presenter);
    CHECK(live == 0);
    puts("presenter packs multiple partial rectangles without allocating; full/gray rebase and failures preserved");
    return 0;
}
