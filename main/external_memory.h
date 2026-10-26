#ifndef NOTE4_EXTERNAL_MEMORY_H
#define NOTE4_EXTERNAL_MEMORY_H

#include <stddef.h>
#include <stdlib.h>

/* Models and documents share the PSRAM pool checked by resource_budget.h.
 * Keep libc on hosts so allocation-failure injection covers the same calls. */
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static inline void *note4_external_malloc(size_t bytes) {
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static inline void *note4_external_calloc(size_t count, size_t bytes) {
    return heap_caps_calloc(count, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static inline void *note4_external_realloc(void *pointer, size_t bytes) {
    return heap_caps_realloc(pointer, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
#else
static inline void *note4_external_malloc(size_t bytes) { return malloc(bytes); }
static inline void *note4_external_calloc(size_t count, size_t bytes) { return calloc(count, bytes); }
static inline void *note4_external_realloc(void *pointer, size_t bytes) { return realloc(pointer, bytes); }
#endif

#endif
