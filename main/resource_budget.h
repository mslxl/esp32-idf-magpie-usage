#ifndef NOTE4_RESOURCE_BUDGET_H
#define NOTE4_RESOURCE_BUDGET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Candidate byte limits; combined radio/render peaks need target measurement. */
#define NOTE4_RESPONSE_BUDGET (384U * 1024U)
#define NOTE4_MODEL_BUDGET (1536U * 1024U)
#define NOTE4_DOCUMENT_BUDGET (384U * 1024U)
#define NOTE4_INTERNAL_RESERVE (32U * 1024U)
#define NOTE4_PSRAM_RESERVE (256U * 1024U)

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static inline bool note4_resource_available(size_t bytes) {
    const size_t external = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    return bytes <= external && external - bytes >= NOTE4_PSRAM_RESERVE &&
        bytes <= heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) &&
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= NOTE4_INTERNAL_RESERVE;
}
static inline void note4_resource_log(const char *stage, size_t bytes) {
    ESP_LOGI("note4_resource", "stage=%s bytes=%u internal_free=%u internal_min=%u internal_largest=%u psram_free=%u psram_min=%u psram_largest=%u stack_free=%u",
        stage, (unsigned)bytes,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
#else
static inline bool note4_resource_available(size_t bytes) { (void)bytes; return true; }
static inline void note4_resource_log(const char *stage, size_t bytes) { (void)stage; (void)bytes; }
#endif
#endif
