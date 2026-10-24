#include "note4_presenter.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

struct note4_presenter {
    uint16_t *rgb565;
    uint8_t *bw;
    uint8_t *gray;
    bool has_frame;
    note4_present_mode_t mode;
};

static bool rect_valid(note4_rect_t rect) {
    return rect.width != 0U && rect.height != 0U && rect.x < NOTE4_WIDTH &&
           rect.y < NOTE4_HEIGHT && rect.width <= NOTE4_WIDTH - rect.x &&
           rect.height <= NOTE4_HEIGHT - rect.y;
}

static bool intersects(note4_rect_t a, note4_rect_t b) {
    return a.x < b.x + b.width && b.x < a.x + a.width &&
           a.y < b.y + b.height && b.y < a.y + a.height;
}

static esp_err_t validate_regions(const note4_rect_t *regions, size_t count) {
    if (count != 0U && regions == NULL) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < count; ++i) {
        if (!rect_valid(regions[i])) return ESP_ERR_INVALID_ARG;
        for (size_t j = 0; j < i; ++j) {
            if (intersects(regions[i], regions[j])) return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}

esp_err_t note4_presenter_create(note4_presenter_t **out_presenter) {
    if (out_presenter == NULL) return ESP_ERR_INVALID_ARG;
    *out_presenter = NULL;
    note4_presenter_t *presenter = calloc(1, sizeof(*presenter));
    if (presenter == NULL) return ESP_ERR_NO_MEM;
    presenter->rgb565 = heap_caps_malloc(
        NOTE4_WIDTH * NOTE4_HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    presenter->bw = heap_caps_malloc(NOTE4_FRAME_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    presenter->gray = heap_caps_malloc(NOTE4_4BPP_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (presenter->rgb565 == NULL || presenter->bw == NULL || presenter->gray == NULL) {
        note4_presenter_destroy(presenter);
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < NOTE4_WIDTH * NOTE4_HEIGHT; ++i) presenter->rgb565[i] = 0xFFFF;
    presenter->mode = NOTE4_PRESENT_MODE_BW;
    *out_presenter = presenter;
    return ESP_OK;
}

void note4_presenter_destroy(note4_presenter_t *presenter) {
    if (presenter == NULL) return;
    if (presenter->rgb565 != NULL) heap_caps_free(presenter->rgb565);
    if (presenter->bw != NULL) heap_caps_free(presenter->bw);
    if (presenter->gray != NULL) heap_caps_free(presenter->gray);
    free(presenter);
}

uint16_t *note4_presenter_rgb565_frame(note4_presenter_t *presenter) {
    return presenter == NULL ? NULL : presenter->rgb565;
}

size_t note4_presenter_rgb565_pixels(void) {
    return NOTE4_WIDTH * NOTE4_HEIGHT;
}

esp_err_t note4_presenter_present(
    note4_presenter_t *presenter, zectrix_epd_handle_t epd,
    const note4_rect_t *damage, size_t damage_count, bool full_redraw,
    const note4_rect_t *gray_regions, size_t gray_region_count,
    uint8_t threshold, note4_present_mode_t *out_mode) {
    if (presenter == NULL || epd == NULL ||
        validate_regions(damage, damage_count) != ESP_OK ||
        validate_regions(gray_regions, gray_region_count) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_mode != NULL) *out_mode = presenter->mode;
    if (damage_count == 0U && presenter->has_frame) return ESP_OK;

    bool gray_refresh = full_redraw && gray_region_count != 0U;
    for (size_t i = 0; !gray_refresh && i < damage_count; ++i) {
        for (size_t j = 0; j < gray_region_count; ++j) {
            if (intersects(damage[i], gray_regions[j])) {
                gray_refresh = true;
                break;
            }
        }
    }
    if (gray_refresh) {
        if (!note4_rgb565_to_4bpp_frame(
                presenter->rgb565, NOTE4_WIDTH * NOTE4_HEIGHT, NOTE4_WIDTH,
                gray_regions, gray_region_count, presenter->gray,
                NOTE4_4BPP_FRAME_BYTES, threshold)) {
            return ESP_ERR_INVALID_ARG;
        }
        esp_err_t err = zectrix_epd_refresh_full_4bpp(
            epd, presenter->gray, NOTE4_4BPP_FRAME_BYTES);
        if (err == ESP_OK) {
            presenter->has_frame = true;
            presenter->mode = NOTE4_PRESENT_MODE_GRAY;
            if (out_mode != NULL) *out_mode = presenter->mode;
        }
        return err;
    }

    const bool need_full = full_redraw || !presenter->has_frame ||
                           presenter->mode != NOTE4_PRESENT_MODE_BW;
    if (!note4_rgb565_to_1bpp_rect(
            presenter->rgb565, NOTE4_WIDTH * NOTE4_HEIGHT, NOTE4_WIDTH,
            (note4_rect_t){0, 0, NOTE4_WIDTH, NOTE4_HEIGHT}, presenter->bw,
            NOTE4_FRAME_BYTES, threshold)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (need_full) {
        esp_err_t err = zectrix_epd_refresh_full_1bpp(
            epd, presenter->bw, NOTE4_FRAME_BYTES);
        if (err == ESP_OK) {
            presenter->has_frame = true;
            presenter->mode = NOTE4_PRESENT_MODE_BW;
            if (out_mode != NULL) *out_mode = presenter->mode;
        }
        return err;
    }

    for (size_t i = 0; i < damage_count; ++i) {
        const note4_rect_t rect = damage[i];
        const size_t bytes = ((rect.width + 7U) / 8U) * rect.height;
        uint8_t *patch = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (patch == NULL) return ESP_ERR_NO_MEM;
        const bool converted = note4_rgb565_to_1bpp_rect(
            presenter->rgb565, NOTE4_WIDTH * NOTE4_HEIGHT, NOTE4_WIDTH, rect,
            patch, bytes, threshold);
        const zectrix_epd_rect_t epd_rect = {
            (int)rect.x, (int)rect.y, (int)rect.width, (int)rect.height};
        esp_err_t err = converted ? zectrix_epd_refresh_partial_1bpp(
            epd, &epd_rect, patch, bytes) : ESP_ERR_INVALID_ARG;
        heap_caps_free(patch);
        if (err != ESP_OK) return err;
    }
    presenter->has_frame = true;
    if (out_mode != NULL) *out_mode = presenter->mode;
    return ESP_OK;
}
