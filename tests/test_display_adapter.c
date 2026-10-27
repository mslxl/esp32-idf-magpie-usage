#include "display_adapter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static uint8_t previous[NOTE4_FRAME_BYTES], next[NOTE4_FRAME_BYTES];

static void reset(void) {
    memset(previous, 0, sizeof(previous));
    memset(next, 0, sizeof(next));
}

static void set(uint8_t *frame, size_t x, size_t y) {
    frame[y * NOTE4_ROW_BYTES + x / 8U] |= (uint8_t)(0x80U >> (x % 8U));
}

static bool equal(note4_rect_t rect, size_t x, size_t y, size_t width, size_t height) {
    return rect.x == x && rect.y == y && rect.width == width && rect.height == height;
}

static void test_identical_frames_have_no_damage(void) {
    note4_rect_t rects[NOTE4_DAMAGE_MAX_RECTS];
    reset();
    CHECK(note4_frame_damage(previous, next, rects, NOTE4_DAMAGE_MAX_RECTS) == 0U);
    CHECK(note4_frame_damage(NULL, next, rects, NOTE4_DAMAGE_MAX_RECTS) == 0U);
    CHECK(note4_frame_damage(previous, next, rects, 0U) == 0U);
}

static void test_separate_bands_are_byte_aligned(void) {
    note4_rect_t rects[NOTE4_DAMAGE_MAX_RECTS];
    reset();
    set(next, 3, 10);
    set(next, 20, 12); /* within the merge gap of row 10 */
    set(next, 399, 299);
    set(next, 200, 100);
    CHECK(note4_frame_damage(previous, next, rects, NOTE4_DAMAGE_MAX_RECTS) == 3U);
    CHECK(equal(rects[0], 0, 10, 24, 3));
    CHECK(equal(rects[1], 200, 100, 8, 1));
    CHECK(equal(rects[2], 392, 299, 8, 1));
}

static void test_merge_gap_boundary(void) {
    note4_rect_t rects[NOTE4_DAMAGE_MAX_RECTS];
    reset();
    set(next, 0, 0);
    set(next, 0, 1U + NOTE4_DAMAGE_MERGE_ROWS); /* exactly MERGE_ROWS unchanged rows */
    CHECK(note4_frame_damage(previous, next, rects, NOTE4_DAMAGE_MAX_RECTS) == 1U);
    CHECK(equal(rects[0], 0, 0, 8, 2U + NOTE4_DAMAGE_MERGE_ROWS));
    reset();
    set(next, 0, 0);
    set(next, 0, 2U + NOTE4_DAMAGE_MERGE_ROWS);
    CHECK(note4_frame_damage(previous, next, rects, NOTE4_DAMAGE_MAX_RECTS) == 2U);
}

static void test_excess_bands_merge_closest_neighbours(void) {
    note4_rect_t rects[NOTE4_DAMAGE_MAX_RECTS];
    reset();
    /* Bands at rows 0, 40, 60, 120, 200 and 280: the 40/60 gap is smallest,
     * then 0 joins 40-60; the 80-row gaps further down stay separate. */
    const size_t rows[] = {0, 40, 60, 120, 200, 280};
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) set(next, 8U * i, rows[i]);
    CHECK(note4_frame_damage(previous, next, rects, NOTE4_DAMAGE_MAX_RECTS) == 4U);
    CHECK(equal(rects[0], 0, 0, 24, 61));
    CHECK(equal(rects[1], 24, 120, 8, 1));
    CHECK(equal(rects[2], 32, 200, 8, 1));
    CHECK(equal(rects[3], 40, 280, 8, 1));
    CHECK(note4_frame_damage(previous, next, rects, 1U) == 1U);
    CHECK(equal(rects[0], 0, 0, 48, 281));
}

static void test_every_row_changed_is_one_rect(void) {
    note4_rect_t rects[NOTE4_DAMAGE_MAX_RECTS];
    reset();
    memset(next, 0xFF, sizeof(next));
    CHECK(note4_frame_damage(previous, next, rects, NOTE4_DAMAGE_MAX_RECTS) == 1U);
    CHECK(equal(rects[0], 0, 0, NOTE4_WIDTH, NOTE4_HEIGHT));
}

static void test_pack_inverts_for_the_panel(void) {
    uint8_t guarded[6];
    uint8_t *packed = guarded + 1;
    reset();
    next[NOTE4_ROW_BYTES + 1] = 0x0F;
    next[2 * NOTE4_ROW_BYTES + 2] = 0x80;
    memset(guarded, 0xA5, sizeof(guarded));
    CHECK(note4_frame_pack(next, (note4_rect_t){8, 1, 16, 2}, packed, 4));
    CHECK(packed[0] == 0xF0 && packed[1] == 0xFF && packed[2] == 0xFF && packed[3] == 0x7F);
    CHECK(guarded[0] == 0xA5 && guarded[5] == 0xA5);
    CHECK(note4_frame_pack(next, (note4_rect_t){392, 299, 8, 1}, packed, 1) && packed[0] == 0xFF);
    CHECK(!note4_frame_pack(next, (note4_rect_t){3, 1, 8, 1}, packed, 1));
    CHECK(!note4_frame_pack(next, (note4_rect_t){8, 1, 16, 2}, packed, 3));
    CHECK(!note4_frame_pack(next, (note4_rect_t){392, 299, 16, 1}, packed, 2));
    CHECK(!note4_frame_pack(NULL, (note4_rect_t){0, 0, 8, 1}, packed, 1));
}

int main(void) {
    CHECK(NOTE4_FRAME_BYTES == 15000U);
    test_identical_frames_have_no_damage();
    test_separate_bands_are_byte_aligned();
    test_merge_gap_boundary();
    test_excess_bands_merge_closest_neighbours();
    test_every_row_changed_is_one_rect();
    test_pack_inverts_for_the_panel();
    puts("PASS: row-band damage, merging, cap and panel packing");
    return EXIT_SUCCESS;
}
