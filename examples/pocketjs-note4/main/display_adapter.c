#include "display_adapter.h"

bool note4_rgb565_to_1bpp_rows(const uint16_t *source, size_t source_pixels,
                              size_t first_row, size_t row_count,
                              uint8_t *frame, size_t frame_bytes,
                              uint8_t threshold) {
    if (source == NULL || frame == NULL || frame_bytes != NOTE4_FRAME_BYTES ||
        first_row >= NOTE4_HEIGHT || row_count == 0 ||
        row_count > NOTE4_HEIGHT - first_row ||
        source_pixels < NOTE4_WIDTH * row_count) {
        return false;
    }

    for (size_t row = 0; row < row_count; ++row) {
        for (size_t byte = 0; byte < NOTE4_ROW_BYTES; ++byte) {
            uint8_t packed = 0;
            for (size_t bit = 0; bit < 8; ++bit) {
                const uint16_t pixel = source[row * NOTE4_WIDTH + byte * 8 + bit];
                const unsigned red5 = (pixel >> 11) & 31U;
                const unsigned green6 = (pixel >> 5) & 63U;
                const unsigned blue5 = pixel & 31U;
                const unsigned red = (red5 << 3) | (red5 >> 2);
                const unsigned green = (green6 << 2) | (green6 >> 4);
                const unsigned blue = (blue5 << 3) | (blue5 >> 2);
                const unsigned luminance =
                    (77U * red + 150U * green + 29U * blue + 128U) >> 8;
                if (luminance >= threshold) {
                    packed |= (uint8_t)(0x80U >> bit);
                }
            }
            frame[(first_row + row) * NOTE4_ROW_BYTES + byte] = packed;
        }
    }
    return true;
}
