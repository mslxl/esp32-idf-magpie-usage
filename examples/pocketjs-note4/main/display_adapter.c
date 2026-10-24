#include "display_adapter.h"

static unsigned luminance(uint16_t pixel) {
    const unsigned red5 = (pixel >> 11) & 31U;
    const unsigned green6 = (pixel >> 5) & 63U;
    const unsigned blue5 = pixel & 31U;
    const unsigned red = (red5 << 3) | (red5 >> 2);
    const unsigned green = (green6 << 2) | (green6 >> 4);
    const unsigned blue = (blue5 << 3) | (blue5 >> 2);
    return (77U * red + 150U * green + 29U * blue + 128U) >> 8;
}

static bool rect_valid(note4_rect_t rect) {
    return rect.width != 0 && rect.height != 0 && rect.x < NOTE4_WIDTH &&
           rect.y < NOTE4_HEIGHT && rect.width <= NOTE4_WIDTH - rect.x &&
           rect.height <= NOTE4_HEIGHT - rect.y;
}

static bool point_in_region(size_t x, size_t y, note4_rect_t region) {
    return x >= region.x && x < region.x + region.width && y >= region.y &&
           y < region.y + region.height;
}

bool note4_rgb565_to_1bpp_rect(const uint16_t *source, size_t source_pixels,
                               size_t source_stride, note4_rect_t rect,
                               uint8_t *destination, size_t destination_bytes,
                               uint8_t threshold) {
    const size_t destination_stride = (rect.width + 7U) / 8U;
    if (source == NULL || destination == NULL || source_stride < NOTE4_WIDTH ||
        !rect_valid(rect) || source_pixels < source_stride * NOTE4_HEIGHT ||
        destination_bytes != destination_stride * rect.height) {
        return false;
    }
    for (size_t row = 0; row < rect.height; ++row) {
        for (size_t byte = 0; byte < destination_stride; ++byte) {
            uint8_t packed = 0;
            for (size_t bit = 0; bit < 8; ++bit) {
                const size_t x = byte * 8U + bit;
                if (x >= rect.width) continue;
                const unsigned value = luminance(
                    source[(rect.y + row) * source_stride + rect.x + x]);
                if (value >= threshold) packed |= (uint8_t)(0x80U >> bit);
            }
            destination[row * destination_stride + byte] = packed;
        }
    }
    return true;
}

bool note4_rgb565_to_4bpp_frame(const uint16_t *source, size_t source_pixels,
                                size_t source_stride, const note4_rect_t *gray_regions,
                                size_t gray_region_count, uint8_t *destination,
                                size_t destination_bytes, uint8_t threshold) {
    if (source == NULL || destination == NULL || source_stride < NOTE4_WIDTH ||
        source_pixels < source_stride * NOTE4_HEIGHT ||
        (gray_region_count != 0U && gray_regions == NULL) ||
        destination_bytes != NOTE4_4BPP_FRAME_BYTES) {
        return false;
    }
    for (size_t index = 0; index < gray_region_count; ++index) {
        if (!rect_valid(gray_regions[index])) return false;
    }
    for (size_t y = 0; y < NOTE4_HEIGHT; ++y) {
        for (size_t x = 0; x < NOTE4_WIDTH; x += 2U) {
            uint8_t packed = 0;
            for (size_t pair = 0; pair < 2U; ++pair) {
                const size_t px = x + pair;
                const unsigned value = luminance(source[y * source_stride + px]);
                bool gray = false;
                for (size_t region = 0; region < gray_region_count; ++region) {
                    if (point_in_region(px, y, gray_regions[region])) {
                        gray = true;
                        break;
                    }
                }
                const unsigned level = gray ? (value * 15U + 127U) / 255U
                                            : (value >= threshold ? 15U : 0U);
                packed |= (uint8_t)(level << (pair == 0U ? 4U : 0U));
            }
            destination[y * NOTE4_4BPP_ROW_BYTES + x / 2U] = packed;
        }
    }
    return true;
}

bool note4_rgb565_to_1bpp_rows(const uint16_t *source, size_t source_pixels,
                               size_t first_row, size_t row_count,
                               uint8_t *frame, size_t frame_bytes,
                               uint8_t threshold) {
    if (first_row >= NOTE4_HEIGHT || row_count == 0U ||
        row_count > NOTE4_HEIGHT - first_row) {
        return false;
    }
    const note4_rect_t rect = {0, first_row, NOTE4_WIDTH, row_count};
    const size_t required = NOTE4_ROW_BYTES * row_count;
    if (source == NULL || frame == NULL || frame_bytes != NOTE4_FRAME_BYTES ||
        source_pixels < NOTE4_WIDTH * row_count) {
        return false;
    }
    for (size_t row = 0; row < row_count; ++row) {
        for (size_t byte = 0; byte < NOTE4_ROW_BYTES; ++byte) {
            uint8_t packed = 0;
            for (size_t bit = 0; bit < 8; ++bit) {
                const unsigned value = luminance(source[row * NOTE4_WIDTH + byte * 8U + bit]);
                if (value >= threshold) packed |= (uint8_t)(0x80U >> bit);
            }
            frame[(first_row + row) * NOTE4_ROW_BYTES + byte] = packed;
        }
    }
    (void)rect;
    (void)required;
    return true;
}
