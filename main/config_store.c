#include "config_store.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NOTE4_CONFIG_NAMESPACE "note4_wifi"
#define NOTE4_CONFIG_KEY "config"

static const char *TAG = "note4_config";
static nvs_handle_t s_handle;
static bool s_initialized;

esp_err_t note4_config_store_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "reinitializing NVS partition");
        err = nvs_flash_erase();
        if (err != ESP_OK) return err;
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    err = nvs_open(NOTE4_CONFIG_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err == ESP_OK) s_initialized = true;
    return err;
}

esp_err_t note4_config_store_load(note4_wifi_config_t *config, bool *found) {
    if (config == NULL || found == NULL || !s_initialized) return ESP_ERR_INVALID_ARG;
    *found = false;
    note4_wifi_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    size_t size = sizeof(blob);
    esp_err_t err = nvs_get_blob(s_handle, NOTE4_CONFIG_KEY, &blob, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (size != sizeof(blob) || !note4_wifi_config_deserialize(&blob, config)) {
        ESP_LOGW(TAG, "stored Wi-Fi configuration is invalid");
        return ESP_OK;
    }
    *found = true;
    return ESP_OK;
}

esp_err_t note4_config_store_save(const note4_wifi_config_t *config) {
    if (config == NULL || !s_initialized || !note4_wifi_config_validate(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t generation = 0U;
    esp_err_t err = nvs_get_u32(s_handle, "generation", &generation);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    note4_wifi_blob_t blob;
    if (!note4_wifi_config_serialize(config, generation + 1U, &blob)) return ESP_ERR_INVALID_ARG;
    err = nvs_set_blob(s_handle, NOTE4_CONFIG_KEY, &blob, sizeof(blob));
    if (err != ESP_OK) return err;
    err = nvs_set_u32(s_handle, "generation", generation + 1U);
    if (err != ESP_OK) return err;
    return nvs_commit(s_handle);
}

esp_err_t note4_config_store_clear(void) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_erase_key(s_handle, NOTE4_CONFIG_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    return nvs_commit(s_handle);
}
