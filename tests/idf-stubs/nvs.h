#ifndef TEST_NVS_H
#define TEST_NVS_H
#include <stddef.h>
#include "esp_err.h"
typedef unsigned nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define NVS_DEFAULT_PART_NAME "nvs"
typedef struct {
    size_t used_entries, free_entries, available_entries, total_entries, namespace_count;
} nvs_stats_t;
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle);
esp_err_t nvs_open_from_partition(const char *part, const char *name, int mode, nvs_handle_t *handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *size);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
esp_err_t nvs_erase_all(nvs_handle_t handle);
esp_err_t nvs_get_stats(const char *part, nvs_stats_t *stats);
#endif
