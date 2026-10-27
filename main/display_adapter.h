#ifndef NOTE4_DISPLAY_ADAPTER_H
#define NOTE4_DISPLAY_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOTE4_WIDTH 400U
#define NOTE4_HEIGHT 300U
#define NOTE4_ROW_BYTES (NOTE4_WIDTH / 8U)
#define NOTE4_FRAME_BYTES (NOTE4_ROW_BYTES * NOTE4_HEIGHT)
/* Each damage rectangle is a separate SSD2683 waveform pass, so nearby bands
 * are merged and the total is capped. */
#define NOTE4_DAMAGE_MAX_RECTS 4U
#define NOTE4_DAMAGE_MERGE_ROWS 16U

typedef struct {
    size_t x;
    size_t y;
    size_t width;
    size_t height;
} note4_rect_t;

/*
 * Frames are 400x300 1bpp, 1 = black, MSB first, NOTE4_ROW_BYTES per row.
 * Writes up to max_rects byte-aligned row-band rectangles covering every
 * differing pixel, ordered top to bottom, and returns their count. Bands
 * separated by at most NOTE4_DAMAGE_MERGE_ROWS unchanged rows are merged;
 * beyond max_rects the closest neighbours are merged.
 */
size_t note4_frame_damage(const uint8_t *previous, const uint8_t *next,
                          note4_rect_t *rects, size_t max_rects);

/* Copies a byte-aligned rectangle of the frame into panel order (1 = white),
 * ceil(width / 8) bytes per row. */
bool note4_frame_pack(const uint8_t *frame, note4_rect_t rect,
                      uint8_t *destination, size_t destination_bytes);

bool note4_rect_valid(note4_rect_t rect);

#endif
