#ifndef NOTE4_DISPLAY_ADAPTER_H
#define NOTE4_DISPLAY_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOTE4_WIDTH 400U
#define NOTE4_HEIGHT 300U
#define NOTE4_ROW_BYTES (NOTE4_WIDTH / 8U)
#define NOTE4_FRAME_BYTES (NOTE4_ROW_BYTES * NOTE4_HEIGHT)

/* Source is a full-width RGB565 strip starting at first_row, not a full frame.
 * Destination is the complete SSD2683 frame: row-major, MSB first, 1=white.
 * Invalid arguments leave the destination unchanged. */
bool note4_rgb565_to_1bpp_rows(const uint16_t *source, size_t source_pixels,
                              size_t first_row, size_t row_count,
                              uint8_t *frame, size_t frame_bytes,
                              uint8_t threshold);

#endif
