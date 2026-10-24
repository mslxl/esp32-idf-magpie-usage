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

static uint16_t pixels[NOTE4_WIDTH * NOTE4_HEIGHT];
static uint8_t guarded_frame[NOTE4_FRAME_BYTES + 2U];
static uint8_t *const frame = guarded_frame + 1U;

static void reset_frame(void) {
    memset(guarded_frame, 0xA5, sizeof(guarded_frame));
}

static void check_guards(void) {
    CHECK(guarded_frame[0] == 0xA5);
    CHECK(guarded_frame[NOTE4_FRAME_BYTES + 1U] == 0xA5);
}

static void test_full_frames(void) {
    CHECK(NOTE4_FRAME_BYTES == 15000U);
    reset_frame();
    memset(pixels, 0, sizeof(pixels));
    CHECK(note4_rgb565_to_1bpp_rows(pixels, NOTE4_WIDTH * NOTE4_HEIGHT,
                                   0, NOTE4_HEIGHT, frame, NOTE4_FRAME_BYTES, 128));
    for (size_t i = 0; i < NOTE4_FRAME_BYTES; ++i) CHECK(frame[i] == 0);
    check_guards();

    memset(pixels, 0xFF, sizeof(pixels));
    CHECK(note4_rgb565_to_1bpp_rows(pixels, NOTE4_WIDTH * NOTE4_HEIGHT,
                                   0, NOTE4_HEIGHT, frame, NOTE4_FRAME_BYTES, 128));
    for (size_t i = 0; i < NOTE4_FRAME_BYTES; ++i) CHECK(frame[i] == 0xFF);
    check_guards();
}

static void test_bit_order_rows_and_edges(void) {
    reset_frame();
    memset(pixels, 0, sizeof(pixels));
    pixels[0] = 0xFFFF;
    pixels[7] = 0xFFFF;
    pixels[8] = 0xFFFF;
    pixels[399] = 0xFFFF;
    pixels[400] = 0xFFFF;
    CHECK(note4_rgb565_to_1bpp_rows(pixels, 800, 1, 2,
                                   frame, NOTE4_FRAME_BYTES, 128));
    CHECK(frame[50] == 0x81);
    CHECK(frame[51] == 0x80);
    CHECK(frame[99] == 0x01);
    CHECK(frame[100] == 0x80);
    for (size_t i = 0; i < 50; ++i) CHECK(frame[i] == 0xA5);
    for (size_t i = 150; i < NOTE4_FRAME_BYTES; ++i) CHECK(frame[i] == 0xA5);
    check_guards();

    CHECK(note4_rgb565_to_1bpp_rows(pixels, 400, 299, 1,
                                   frame, NOTE4_FRAME_BYTES, 128));
    CHECK(frame[14950] == 0x81);
    CHECK(frame[14999] == 0x01);
    check_guards();
}

static void test_luminance_and_threshold(void) {
    reset_frame();
    memset(pixels, 0, sizeof(pixels));
    const uint16_t colors[] = {0x0000, 0xFFFF, 0xF800, 0x07E0,
                               0x001F, 0x7BEF, 0x8410, 0xFFFF};
    memcpy(pixels, colors, sizeof(colors));
    CHECK(note4_rgb565_to_1bpp_rows(pixels, 400, 0, 1,
                                   frame, NOTE4_FRAME_BYTES, 128));
    CHECK(frame[0] == 0x53); /* black, white, red, green, blue, dark, light, white */
    CHECK(note4_rgb565_to_1bpp_rows(pixels, 400, 0, 1,
                                   frame, NOTE4_FRAME_BYTES, 0));
    for (size_t i = 0; i < NOTE4_ROW_BYTES; ++i) CHECK(frame[i] == 0xFF);
    CHECK(note4_rgb565_to_1bpp_rows(pixels, 400, 0, 1,
                                   frame, NOTE4_FRAME_BYTES, 255));
    CHECK(frame[0] == 0x41);
    check_guards();
}

static void test_rejected_arguments(void) {
    reset_frame();
    CHECK(!note4_rgb565_to_1bpp_rows(NULL, 400, 0, 1, frame, NOTE4_FRAME_BYTES, 128));
    CHECK(!note4_rgb565_to_1bpp_rows(pixels, 400, 0, 1, NULL, NOTE4_FRAME_BYTES, 128));
    CHECK(!note4_rgb565_to_1bpp_rows(pixels, 399, 0, 1, frame, NOTE4_FRAME_BYTES, 128));
    CHECK(!note4_rgb565_to_1bpp_rows(pixels, 400, 0, 1, frame, NOTE4_FRAME_BYTES - 1, 128));
    CHECK(!note4_rgb565_to_1bpp_rows(pixels, 400, 0, 1, frame, NOTE4_FRAME_BYTES + 1, 128));
    CHECK(!note4_rgb565_to_1bpp_rows(pixels, 400, 0, 0, frame, NOTE4_FRAME_BYTES, 128));
    CHECK(!note4_rgb565_to_1bpp_rows(pixels, 400, 300, 1, frame, NOTE4_FRAME_BYTES, 128));
    CHECK(!note4_rgb565_to_1bpp_rows(pixels, 800, 299, 2, frame, NOTE4_FRAME_BYTES, 128));
    CHECK(!note4_rgb565_to_1bpp_rows(pixels, 400, SIZE_MAX, 1, frame, NOTE4_FRAME_BYTES, 128));
    CHECK(!note4_rgb565_to_1bpp_rows(pixels, SIZE_MAX, 0, SIZE_MAX, frame, NOTE4_FRAME_BYTES, 128));
    for (size_t i = 0; i < sizeof(guarded_frame); ++i) CHECK(guarded_frame[i] == 0xA5);
}

int main(void) {
    test_full_frames();
    test_bit_order_rows_and_edges();
    test_luminance_and_threshold();
    test_rejected_arguments();
    puts("PASS: full frames, bit order, strip offsets, edges, luminance, bounds");
    return EXIT_SUCCESS;
}
