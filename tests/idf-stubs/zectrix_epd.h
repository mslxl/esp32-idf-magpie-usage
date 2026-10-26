#ifndef TEST_ZECTRIX_EPD_H
#define TEST_ZECTRIX_EPD_H
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct zectrix_epd_t *zectrix_epd_handle_t;
typedef struct { int x, y, width, height; } zectrix_epd_rect_t;
esp_err_t zectrix_epd_refresh_full_1bpp(zectrix_epd_handle_t epd, const uint8_t *pixels, size_t bytes);
esp_err_t zectrix_epd_refresh_partial_1bpp(zectrix_epd_handle_t epd, const zectrix_epd_rect_t *rect, const uint8_t *pixels, size_t bytes);
esp_err_t zectrix_epd_refresh_full_4bpp(zectrix_epd_handle_t epd, const uint8_t *pixels, size_t bytes);
#endif
