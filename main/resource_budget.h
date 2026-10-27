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

/* Dedicated settings NVS partition (partitions.csv "note4_cfg"). NVS pages hold
 * 126 entries of 32 bytes; one page stays reserved for garbage collection. The
 * two-bank bundle needs both banks plus a transient copy of one chunk while
 * an existing key is overwritten, so require three maximum bundles. */
#define NOTE4_CONFIG_PARTITION_SIZE (0x180000U)
#define NOTE4_NVS_PAGE_ENTRIES 126U
#define NOTE4_CONFIG_PARTITION_ENTRIES ((NOTE4_CONFIG_PARTITION_SIZE / 4096U - 1U) * NOTE4_NVS_PAGE_ENTRIES)
#define NOTE4_BUNDLE_MAX_ENTRIES ((NOTE4_DOCUMENT_BUDGET + 1536U) / 32U + ((NOTE4_DOCUMENT_BUDGET + 1536U) / 3072U + 1U) * 3U + 16U)
_Static_assert(NOTE4_CONFIG_PARTITION_ENTRIES >= 3U * NOTE4_BUNDLE_MAX_ENTRIES,
    "note4_cfg partition is too small for two maximum settings/preferences banks");

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
