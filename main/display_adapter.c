#include "display_adapter.h"

#include <string.h>

typedef struct {
    size_t top, bottom; /* inclusive rows */
    size_t first, last; /* inclusive byte columns */
} band_t;

bool note4_rect_valid(note4_rect_t rect) {
    return rect.width != 0U && rect.height != 0U && rect.x < NOTE4_WIDTH &&
           rect.y < NOTE4_HEIGHT && rect.width <= NOTE4_WIDTH - rect.x &&
           rect.height <= NOTE4_HEIGHT - rect.y;
}

static void merge(band_t *into, const band_t *from) {
    if (from->first < into->first) into->first = from->first;
    if (from->last > into->last) into->last = from->last;
    into->bottom = from->bottom;
}

size_t note4_frame_damage(const uint8_t *previous, const uint8_t *next,
                          note4_rect_t *rects, size_t max_rects) {
    if (previous == NULL || next == NULL || rects == NULL || max_rects == 0U) return 0U;
    /* Merging gaps of NOTE4_DAMAGE_MERGE_ROWS bounds the band count. */
    band_t bands[NOTE4_HEIGHT / (NOTE4_DAMAGE_MERGE_ROWS + 1U) + 1U];
    size_t count = 0U;
    for (size_t y = 0U; y < NOTE4_HEIGHT; ++y) {
        const uint8_t *a = previous + y * NOTE4_ROW_BYTES, *b = next + y * NOTE4_ROW_BYTES;
        if (memcmp(a, b, NOTE4_ROW_BYTES) == 0) continue;
        size_t first = 0U, last = NOTE4_ROW_BYTES - 1U;
        while (a[first] == b[first]) ++first;
        while (a[last] == b[last]) --last;
        const band_t row = {y, y, first, last};
        if (count != 0U && y - bands[count - 1U].bottom <= NOTE4_DAMAGE_MERGE_ROWS + 1U)
            merge(&bands[count - 1U], &row);
        else
            bands[count++] = row;
    }
    while (count > max_rects) {
        size_t closest = 0U;
        for (size_t i = 1U; i + 1U < count; ++i) {
            if (bands[i + 1U].top - bands[i].bottom < bands[closest + 1U].top - bands[closest].bottom)
                closest = i;
        }
        merge(&bands[closest], &bands[closest + 1U]);
        memmove(&bands[closest + 1U], &bands[closest + 2U],
                (count - closest - 2U) * sizeof(bands[0]));
        --count;
    }
    for (size_t i = 0U; i < count; ++i) {
        rects[i] = (note4_rect_t){bands[i].first * 8U, bands[i].top,
            (bands[i].last - bands[i].first + 1U) * 8U, bands[i].bottom - bands[i].top + 1U};
    }
    return count;
}

bool note4_frame_pack(const uint8_t *frame, note4_rect_t rect,
                      uint8_t *destination, size_t destination_bytes) {
    const size_t stride = (rect.width + 7U) / 8U;
    if (frame == NULL || destination == NULL || !note4_rect_valid(rect) ||
        rect.x % 8U != 0U || destination_bytes != stride * rect.height) {
        return false;
    }
    for (size_t row = 0U; row < rect.height; ++row) {
        const uint8_t *source = frame + (rect.y + row) * NOTE4_ROW_BYTES + rect.x / 8U;
        uint8_t *out = destination + row * stride;
        for (size_t byte = 0U; byte < stride; ++byte) out[byte] = (uint8_t)~source[byte];
    }
    return true;
}
