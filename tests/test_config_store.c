#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "config_store.h"
#include "nvs.h"

typedef struct { char key[16]; uint8_t durable[NOTE4_SETTINGS_MAX_BYTES], pending[NOTE4_SETTINGS_MAX_BYTES]; size_t durable_length, pending_length; } entry_t;
static entry_t entries[8];
static size_t entry_count;
static entry_t *lookup(const char *key) {
    for (size_t i = 0U; i < entry_count; ++i) if (!strcmp(entries[i].key, key)) return &entries[i];
    if (entry_count == 8U || strlen(key) >= sizeof(entries[0].key)) abort();
    entry_t *entry = &entries[entry_count++]; memcpy(entry->key, key, strlen(key) + 1U); return entry;
}
static esp_err_t init_error, write_error, commit_error;
static unsigned erases;

esp_err_t nvs_flash_init(void) { return init_error; }
esp_err_t nvs_flash_erase(void) { ++erases; return ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    (void)name; (void)mode; *handle = 1; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *size) {
    (void)handle;
    entry_t *entry = lookup(key);
    if (entry->durable_length == 0U) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < entry->durable_length) return ESP_ERR_NVS_INVALID_LENGTH;
    *size = entry->durable_length;
    memcpy(data, entry->durable, entry->durable_length);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size) {
    (void)handle; (void)key;
    if (write_error) return write_error;
    entry_t *entry = lookup(key);
    if (size > sizeof(entry->pending)) return ESP_ERR_NO_MEM;
    memcpy(entry->pending, data, size); entry->pending_length = size;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    for (size_t i = 0U; i < entry_count; ++i) {
        entry_t *entry = &entries[i];
        if (!commit_error && entry->pending_length) {
            memcpy(entry->durable, entry->pending, entry->pending_length);
            entry->durable_length = entry->pending_length;
        }
        entry->pending_length = 0U;
    }
    return commit_error;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    (void)handle; (void)key; ++erases; return ESP_OK;
}

#define CHECK(condition) do { if (!(condition)) abort(); } while (0)
int main(void) {
    init_error = ESP_ERR_NVS_NO_FREE_PAGES;
    CHECK(note4_config_store_init() == ESP_ERR_NVS_NO_FREE_PAGES);
    CHECK(erases == 0U);
    init_error = ESP_OK;
    CHECK(note4_config_store_init() == ESP_OK);
    note4_settings_t saved, loaded;
    note4_settings_clear(&saved);
    CHECK(note4_wifi_slot_set(&saved.wifi, 0U, "original", "password", true));
    CHECK(note4_config_store_save(&saved) == ESP_OK);
    saved.web.interval_seconds = 300U;
    write_error = ESP_ERR_NO_MEM;
    CHECK(note4_config_store_save(&saved) == ESP_ERR_NO_MEM);
    bool found = false;
    CHECK(note4_config_store_load(&loaded, &found) == ESP_OK && found);
    CHECK(loaded.web.interval_seconds == 30U && strcmp(loaded.wifi.slots[0].ssid, "original") == 0);
    write_error = ESP_OK;
    commit_error = ESP_FAIL;
    CHECK(note4_config_store_save(&saved) == ESP_FAIL);
    CHECK(note4_config_store_load(&loaded, &found) == ESP_OK && found);
    CHECK(loaded.web.interval_seconds == 30U);
    CHECK(erases == 0U);
    return 0;
}
