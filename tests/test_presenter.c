#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "note4_presenter.h"
#include "esp_heap_caps.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed %d: %s\n", __LINE__, #x); exit(1); } } while (0)
struct zectrix_epd_t { int unused; };
static size_t allocations, live, full_calls, partial_calls, fail_at;
static bool require_external;
static esp_err_t driver_result;
static zectrix_epd_rect_t last_rect;
static uint8_t last_pixels[NOTE4_FRAME_BYTES];

void *heap_caps_malloc(size_t bytes, uint32_t caps) {
    ++allocations;
    CHECK(bytes == NOTE4_FRAME_BYTES);
    if (require_external) CHECK(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (allocations == fail_at) return NULL;
    void *p = malloc(bytes);
    if (p) ++live;
    return p;
}
void heap_caps_free(void *p) { if (p) { CHECK(live); --live; } free(p); }
esp_err_t zectrix_epd_refresh_full_1bpp(zectrix_epd_handle_t epd, const uint8_t *pixels, size_t bytes) {
    CHECK(epd && pixels && bytes == NOTE4_FRAME_BYTES);
    memcpy(last_pixels, pixels, bytes);
    ++full_calls; return driver_result;
}
esp_err_t zectrix_epd_refresh_partial_1bpp(zectrix_epd_handle_t epd, const zectrix_epd_rect_t *rect, const uint8_t *pixels, size_t bytes) {
    CHECK(epd && pixels && rect && bytes == (size_t)((rect->width + 7) / 8 * rect->height));
    last_rect = *rect;
    memcpy(last_pixels, pixels, bytes);
    ++partial_calls; return driver_result;
}
esp_err_t zectrix_epd_refresh_full_4bpp(zectrix_epd_handle_t epd, const uint8_t *pixels, size_t bytes) {
    (void)epd; (void)pixels; (void)bytes;
    fputs("the presenter never uses gray refreshes\n", stderr);
    exit(1);
}

static note4_present_mode_t present(note4_presenter_t *presenter, struct zectrix_epd_t *device,
                                    int64_t now_us, esp_err_t expected) {
    note4_rect_t damage[NOTE4_DAMAGE_MAX_RECTS];
    const size_t count = note4_presenter_damage(presenter, damage, NOTE4_DAMAGE_MAX_RECTS);
    note4_present_mode_t mode = NOTE4_PRESENT_PARTIAL;
    CHECK(note4_presenter_present(presenter, device, damage, count, false, now_us, &mode) == expected);
    return mode;
}
static void toggle(note4_presenter_t *presenter, size_t row) {
    note4_presenter_frame(presenter)[row * NOTE4_ROW_BYTES] ^= 0x80U;
}

int main(int argc, char **argv) {
    (void)argv; require_external = argc > 1;
    for (size_t failure = 1; failure <= 3; ++failure) {
        allocations = 0; fail_at = failure;
        note4_presenter_t *candidate = NULL;
        CHECK(note4_presenter_create(&candidate) == ESP_ERR_NO_MEM && candidate == NULL && live == 0);
    }
    allocations = 0; fail_at = 0;
    note4_presenter_t *presenter = NULL;
    CHECK(note4_presenter_create(&presenter) == ESP_OK && live == 3);
    struct zectrix_epd_t device = {0};
    const int64_t hour = NOTE4_PRESENTER_FULL_INTERVAL_US;

    /* The first frame is a full refresh in panel polarity (1 = white). */
    uint8_t *frame = note4_presenter_frame(presenter);
    frame[0] = 0x81;
    CHECK(present(presenter, &device, 0, ESP_OK) == NOTE4_PRESENT_FULL && full_calls == 1);
    CHECK(last_pixels[0] == 0x7E && last_pixels[1] == 0xFF);
    CHECK(!memcmp(note4_presenter_shown(presenter), frame, NOTE4_FRAME_BYTES));

    /* Nothing changed: no panel work at all. */
    CHECK(present(presenter, &device, 1, ESP_OK) == NOTE4_PRESENT_NONE);
    CHECK(full_calls == 1 && partial_calls == 0);

    /* Two separated bands become two partial passes. */
    toggle(presenter, 10); toggle(presenter, 200);
    CHECK(present(presenter, &device, 2, ESP_OK) == NOTE4_PRESENT_PARTIAL);
    CHECK(partial_calls == 2 && full_calls == 1);
    CHECK(last_rect.x == 0 && last_rect.y == 200 && last_rect.width == 8 && last_rect.height == 1);
    CHECK(last_pixels[0] == 0x7F);

    /* The 30-pass budget: passes 3..30 stay partial, the next one is full. */
    for (size_t pass = 3; pass <= NOTE4_PRESENTER_MAX_PARTIALS; ++pass) {
        toggle(presenter, 50);
        CHECK(present(presenter, &device, (int64_t)pass, ESP_OK) == NOTE4_PRESENT_PARTIAL);
    }
    CHECK(partial_calls == NOTE4_PRESENTER_MAX_PARTIALS && full_calls == 1);
    toggle(presenter, 50);
    CHECK(present(presenter, &device, 100, ESP_OK) == NOTE4_PRESENT_FULL && full_calls == 2);
    toggle(presenter, 50);
    CHECK(present(presenter, &device, 101, ESP_OK) == NOTE4_PRESENT_PARTIAL);

    /* More than an hour since the last full refresh forces a full one, but
     * only when something changed. */
    CHECK(present(presenter, &device, 100 + hour + 1, ESP_OK) == NOTE4_PRESENT_NONE);
    toggle(presenter, 60);
    CHECK(present(presenter, &device, 100 + hour, ESP_OK) == NOTE4_PRESENT_PARTIAL);
    toggle(presenter, 60);
    CHECK(present(presenter, &device, 100 + hour + 1, ESP_OK) == NOTE4_PRESENT_FULL && full_calls == 3);

    /* An explicit full redraw and invalid rectangles. */
    note4_present_mode_t mode;
    CHECK(note4_presenter_present(presenter, &device, NULL, 0, true, 2 * hour, &mode) == ESP_OK);
    CHECK(mode == NOTE4_PRESENT_FULL && full_calls == 4);
    const note4_rect_t unaligned = {3, 0, 8, 1}, outside = {392, 299, 16, 1};
    CHECK(note4_presenter_present(presenter, &device, &unaligned, 1, false, 2 * hour, &mode) == ESP_ERR_INVALID_ARG);
    CHECK(note4_presenter_present(presenter, &device, &outside, 1, false, 2 * hour, &mode) == ESP_ERR_INVALID_ARG);
    CHECK(note4_presenter_present(presenter, NULL, NULL, 0, true, 2 * hour, &mode) == ESP_ERR_INVALID_ARG);

    /* A failed partial pass leaves the panel unknown, so the retry is full. */
    driver_result = ESP_FAIL;
    toggle(presenter, 70);
    CHECK(present(presenter, &device, 2 * hour + 1, ESP_FAIL) == NOTE4_PRESENT_NONE);
    driver_result = ESP_OK;
    note4_rect_t damage[NOTE4_DAMAGE_MAX_RECTS];
    CHECK(note4_presenter_damage(presenter, damage, NOTE4_DAMAGE_MAX_RECTS) == 1U);
    CHECK(damage[0].width == NOTE4_WIDTH && damage[0].height == NOTE4_HEIGHT);
    CHECK(present(presenter, &device, 2 * hour + 2, ESP_OK) == NOTE4_PRESENT_FULL);
    CHECK(!memcmp(note4_presenter_shown(presenter), frame, NOTE4_FRAME_BYTES));

    CHECK(live == 3 && allocations == 3);
    note4_presenter_destroy(presenter);
    CHECK(live == 0);
    puts("presenter: row-band partials, 30-pass and 1 h full-refresh policy, failure rebase, no per-frame allocation");
    return 0;
}
