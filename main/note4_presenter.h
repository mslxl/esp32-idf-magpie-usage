#ifndef NOTE4_PRESENTER_H
#define NOTE4_PRESENTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "zectrix_epd.h"

#include "display_adapter.h"

typedef struct note4_presenter note4_presenter_t;

typedef enum {
    NOTE4_PRESENT_MODE_BW = 0,
    NOTE4_PRESENT_MODE_GRAY,
} note4_present_mode_t;

esp_err_t note4_presenter_create(note4_presenter_t **out_presenter);
void note4_presenter_destroy(note4_presenter_t *presenter);

uint16_t *note4_presenter_rgb565_frame(note4_presenter_t *presenter);
size_t note4_presenter_rgb565_pixels(void);

/*
 * Present the already-rendered persistent RGB565 frame. A non-empty gray
 * region list selects a full 4bpp refresh; 1bpp damage uses synchronous
 * partial refreshes when the panel has a valid monochrome baseline.
 */
esp_err_t note4_presenter_present(
    note4_presenter_t *presenter, zectrix_epd_handle_t epd,
    const note4_rect_t *damage, size_t damage_count, bool full_redraw,
    const note4_rect_t *gray_regions, size_t gray_region_count,
    uint8_t threshold, note4_present_mode_t *out_mode);

#endif
