#include "note4_presenter.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

struct note4_presenter {
    uint8_t *frame;  /* draw target, 1 = black */
    uint8_t *shown;  /* panel content, 1 = black */
    uint8_t *panel;  /* packed driver scratch, 1 = white */
    bool has_frame;
    size_t partials; /* partial passes since the last full refresh */
    int64_t full_at_us;
};

static bool regions_valid(const note4_rect_t *regions, size_t count) {
    if (count != 0U && regions == NULL) return false;
    for (size_t i = 0U; i < count; ++i) {
        if (!note4_rect_valid(regions[i]) || regions[i].x % 8U != 0U) return false;
    }
    return true;
}

esp_err_t note4_presenter_create(note4_presenter_t **out_presenter) {
    if (out_presenter == NULL) return ESP_ERR_INVALID_ARG;
    *out_presenter = NULL;
    note4_presenter_t *presenter = calloc(1, sizeof(*presenter));
    if (presenter == NULL) return ESP_ERR_NO_MEM;
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    presenter->frame = heap_caps_malloc(NOTE4_FRAME_BYTES, caps);
    presenter->shown = heap_caps_malloc(NOTE4_FRAME_BYTES, caps);
    presenter->panel = heap_caps_malloc(NOTE4_FRAME_BYTES, caps);
    if (presenter->frame == NULL || presenter->shown == NULL || presenter->panel == NULL) {
        note4_presenter_destroy(presenter);
        return ESP_ERR_NO_MEM;
    }
    memset(presenter->frame, 0, NOTE4_FRAME_BYTES);
    memset(presenter->shown, 0, NOTE4_FRAME_BYTES);
    *out_presenter = presenter;
    return ESP_OK;
}

void note4_presenter_destroy(note4_presenter_t *presenter) {
    if (presenter == NULL) return;
    heap_caps_free(presenter->frame);
    heap_caps_free(presenter->shown);
    heap_caps_free(presenter->panel);
    free(presenter);
}

uint8_t *note4_presenter_frame(note4_presenter_t *presenter) {
    return presenter == NULL ? NULL : presenter->frame;
}

const uint8_t *note4_presenter_shown(const note4_presenter_t *presenter) {
    return presenter == NULL ? NULL : presenter->shown;
}

size_t note4_presenter_damage(const note4_presenter_t *presenter,
                              note4_rect_t *rects, size_t max_rects) {
    if (presenter == NULL || rects == NULL || max_rects == 0U) return 0U;
    if (!presenter->has_frame) {
        rects[0] = (note4_rect_t){0U, 0U, NOTE4_WIDTH, NOTE4_HEIGHT};
        return 1U;
    }
    return note4_frame_damage(presenter->shown, presenter->frame, rects, max_rects);
}

static bool needs_full(const note4_presenter_t *presenter, size_t damage_count,
                       bool full_redraw, int64_t now_us) {
    return full_redraw || !presenter->has_frame ||
           presenter->partials + damage_count > NOTE4_PRESENTER_MAX_PARTIALS ||
           now_us - presenter->full_at_us > NOTE4_PRESENTER_FULL_INTERVAL_US ||
           now_us < presenter->full_at_us;
}

esp_err_t note4_presenter_present(
    note4_presenter_t *presenter, zectrix_epd_handle_t epd,
    const note4_rect_t *damage, size_t damage_count, bool full_redraw,
    int64_t now_us, note4_present_mode_t *out_mode) {
    if (out_mode != NULL) *out_mode = NOTE4_PRESENT_NONE;
    if (presenter == NULL || epd == NULL || !regions_valid(damage, damage_count)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (damage_count == 0U && presenter->has_frame && !full_redraw) return ESP_OK;

    esp_err_t err = ESP_OK;
    note4_present_mode_t mode;
    if (needs_full(presenter, damage_count, full_redraw, now_us)) {
        mode = NOTE4_PRESENT_FULL;
        const note4_rect_t all = {0U, 0U, NOTE4_WIDTH, NOTE4_HEIGHT};
        (void)note4_frame_pack(presenter->frame, all, presenter->panel, NOTE4_FRAME_BYTES);
        err = zectrix_epd_refresh_full_1bpp(epd, presenter->panel, NOTE4_FRAME_BYTES);
        if (err == ESP_OK) {
            presenter->partials = 0U;
            presenter->full_at_us = now_us;
        }
    } else {
        mode = NOTE4_PRESENT_PARTIAL;
        for (size_t i = 0U; err == ESP_OK && i < damage_count; ++i) {
            const note4_rect_t rect = damage[i];
            const size_t bytes = ((rect.width + 7U) / 8U) * rect.height;
            (void)note4_frame_pack(presenter->frame, rect, presenter->panel, bytes);
            const zectrix_epd_rect_t epd_rect = {
                (int)rect.x, (int)rect.y, (int)rect.width, (int)rect.height};
            err = zectrix_epd_refresh_partial_1bpp(epd, &epd_rect, presenter->panel, bytes);
            if (err == ESP_OK) ++presenter->partials;
        }
    }
    presenter->has_frame = err == ESP_OK;
    if (err != ESP_OK) return err;
    memcpy(presenter->shown, presenter->frame, NOTE4_FRAME_BYTES);
    if (out_mode != NULL) *out_mode = mode;
    return ESP_OK;
}
