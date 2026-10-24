#ifndef NOTE4_DISPLAY_ADAPTER_H
#define NOTE4_DISPLAY_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOTE4_WIDTH 400U
#define NOTE4_HEIGHT 300U
#define NOTE4_ROW_BYTES (NOTE4_WIDTH / 8U)
#define NOTE4_FRAME_BYTES (NOTE4_ROW_BYTES * NOTE4_HEIGHT)
#define NOTE4_4BPP_ROW_BYTES (NOTE4_WIDTH / 2U)
#define NOTE4_4BPP_FRAME_BYTES (NOTE4_4BPP_ROW_BYTES * NOTE4_HEIGHT)

typedef struct {
    size_t x;
    size_t y;
    size_t width;
    size_t height;
} note4_rect_t;

/* Source is a full RGB565 frame with source_stride pixels per row. */
bool note4_rgb565_to_1bpp_rect(const uint16_t *source, size_t source_pixels,
                               size_t source_stride, note4_rect_t rect,
                               uint8_t *destination, size_t destination_bytes,
                               uint8_t threshold);

/* Source is a full RGB565 frame. Destination is the complete 4bpp frame. */
bool note4_rgb565_to_4bpp_frame(const uint16_t *source, size_t source_pixels,
                                size_t source_stride, const note4_rect_t *gray_regions,
                                size_t gray_region_count, uint8_t *destination,
                                size_t destination_bytes, uint8_t threshold);

/* Compatibility helper for full-width RGB565 strips. */
bool note4_rgb565_to_1bpp_rows(const uint16_t *source, size_t source_pixels,
                               size_t first_row, size_t row_count,
                               uint8_t *frame, size_t frame_bytes,
                               uint8_t threshold);

#endif
