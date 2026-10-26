#ifndef TEST_ESP_HEAP_CAPS_H
#define TEST_ESP_HEAP_CAPS_H
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_SPIRAM 1U
#define MALLOC_CAP_8BIT 2U
#define MALLOC_CAP_INTERNAL 4U
void *heap_caps_malloc(size_t bytes, uint32_t caps);
void heap_caps_free(void *pointer);
#endif
