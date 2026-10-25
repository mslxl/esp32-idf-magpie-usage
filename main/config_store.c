#include "config_store.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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
    if (err != ESP_OK) return err;
    err = nvs_open(NOTE4_CONFIG_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err == ESP_OK) s_initialized = true;
    return err;
}

static esp_err_t load_legacy(note4_settings_t *config, bool *found) {
    if (config == NULL || found == NULL || !s_initialized) return ESP_ERR_INVALID_ARG;
    *found = false;
    note4_settings_clear(config);
    note4_settings_blob_t blob = {0};
    size_t size = sizeof(blob.bytes);
    esp_err_t err = nvs_get_blob(s_handle, "web_settings_v2", blob.bytes, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        note4_settings_legacy_t combined = {0};
        size = sizeof(combined);
        err = nvs_get_blob(s_handle, "settings", &combined, &size);
        if (err == ESP_OK) {
            const bool valid = size == sizeof(combined) && note4_settings_migrate_combined(&combined, config);
            memset(&combined, 0, sizeof(combined));
            if (!valid) return ESP_ERR_INVALID_STATE;
            err = note4_config_store_save_bundle(config, NULL, 0U);
            if (err != ESP_OK) return err;
            *found = true;
            return ESP_OK;
        }
        if (err != ESP_ERR_NVS_NOT_FOUND) return err;
        note4_wifi_blob_t legacy = {0};
        size = sizeof(legacy);
        err = nvs_get_blob(s_handle, NOTE4_CONFIG_KEY, &legacy, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
        if (err != ESP_OK) return err;
        const bool valid = size == sizeof(legacy) && note4_settings_migrate(&legacy, config);
        memset(&legacy, 0, sizeof(legacy));
        if (!valid) return ESP_ERR_INVALID_STATE;
        err = note4_config_store_save_bundle(config, NULL, 0U);
        if (err != ESP_OK) return err;
    } else {
        if (err != ESP_OK) return err;
        blob.length = size;
        const bool valid = note4_settings_deserialize(&blob, config);
        memset(&blob, 0, sizeof(blob));
        if (!valid) {
            ESP_LOGE(TAG, "stored settings are invalid");
            return ESP_ERR_INVALID_STATE;
        }
    }
    *found = true;
    return ESP_OK;
}

#define BUNDLE_CHUNK 3072U
#define BUNDLE_MANIFEST 24U
static uint32_t get32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}
static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0U; i < 4U; ++i) p[i] = (uint8_t)(value >> (i * 8U));
}
static uint32_t crc32(const uint8_t *bytes, size_t length) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned b = 0U; b < 8U; ++b) crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
static esp_err_t head(uint8_t manifest[BUNDLE_MANIFEST], bool *found) {
    size_t size = BUNDLE_MANIFEST;
    const esp_err_t err = nvs_get_blob(s_handle, "bundle_head", manifest, &size);
    *found = err == ESP_OK;
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (size != BUNDLE_MANIFEST || memcmp(manifest, "N4B3", 4U) || manifest[4] > 1U ||
        manifest[5] || manifest[6] || manifest[7] || crc32(manifest, 20U) != get32(manifest + 20U)) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
esp_err_t note4_config_store_load_bundle(note4_settings_t *config, bool *found, uint8_t **prefs, size_t *length) {
    if (config == NULL || found == NULL || prefs == NULL || length == NULL || !s_initialized) return ESP_ERR_INVALID_ARG;
    *found = false;
    uint8_t manifest[BUNDLE_MANIFEST]; bool present;
    esp_err_t err = head(manifest, &present);
    if (err != ESP_OK) return err;
    if (!present) { err = load_legacy(config, found); if (err == ESP_OK) { *prefs = NULL; *length = 0U; } return err; }
    const uint32_t total = get32(manifest + 8U), settings_length = get32(manifest + 12U);
    if (settings_length > NOTE4_SETTINGS_MAX_BYTES || settings_length > total || settings_length == 0U) return ESP_ERR_INVALID_STATE;
    uint8_t *bytes = malloc(total);
    if (bytes == NULL) return ESP_ERR_NO_MEM;
    size_t offset = 0U;
    for (uint32_t index = 0U; offset < total; ++index) {
        char key[16]; (void)snprintf(key, sizeof(key), "b%u_%08lx", manifest[4], (unsigned long)index);
        const size_t expected = total - offset < BUNDLE_CHUNK ? total - offset : BUNDLE_CHUNK;
        size_t size = expected;
        err = nvs_get_blob(s_handle, key, bytes + offset, &size);
        if (err != ESP_OK || size != expected) { memset(bytes, 0, total); free(bytes); return err == ESP_OK ? ESP_ERR_INVALID_STATE : err; }
        offset += size;
    }
    note4_settings_blob_t blob = {.length = settings_length};
    memcpy(blob.bytes, bytes, settings_length);
    note4_settings_t candidate = {0};
    const bool valid = crc32(bytes, total) == get32(manifest + 16U) && note4_settings_deserialize(&blob, &candidate);
    memset(&blob, 0, sizeof(blob));
    if (!valid) { memset(bytes, 0, total); free(bytes); return ESP_ERR_INVALID_STATE; }
    const size_t prefs_length = total - settings_length;
    uint8_t *document = prefs_length ? malloc(prefs_length) : NULL;
    if (prefs_length && document == NULL) { memset(bytes, 0, total); free(bytes); return ESP_ERR_NO_MEM; }
    if (prefs_length) memcpy(document, bytes + settings_length, prefs_length);
    memset(bytes, 0, total); free(bytes);
    *config = candidate; memset(&candidate, 0, sizeof(candidate));
    *found = true; *prefs = document; *length = prefs_length;
    return ESP_OK;
}
esp_err_t note4_config_store_load(note4_settings_t *config, bool *found) {
    uint8_t *prefs = NULL; size_t length;
    const esp_err_t err = note4_config_store_load_bundle(config, found, &prefs, &length);
    free(prefs); return err;
}
esp_err_t note4_config_store_save_bundle(const note4_settings_t *config, const uint8_t *prefs, size_t length) {
    if (config == NULL || !s_initialized || (length && prefs == NULL)) return ESP_ERR_INVALID_ARG;
    note4_settings_blob_t blob;
    if (!note4_settings_serialize(config, &blob)) return ESP_ERR_INVALID_ARG;
    if (length > UINT32_MAX - blob.length) { memset(&blob, 0, sizeof(blob)); return ESP_ERR_NO_MEM; }
    uint8_t previous[BUNDLE_MANIFEST] = {0}; bool present;
    esp_err_t err = head(previous, &present);
    if (err == ESP_ERR_INVALID_STATE && present && memcmp(previous, "N4B3", 4U) == 0 &&
        previous[4] <= 1U && !previous[5] && !previous[6] && !previous[7]) {
        /* Explicit setup saves can replace a damaged known manifest. Preserve its
         * bank and pointer until the replacement has been written completely. */
        size_t previous_size = sizeof(previous);
        err = nvs_get_blob(s_handle, "bundle_head", previous, &previous_size);
        if (err == ESP_OK && previous_size != sizeof(previous)) err = ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) { memset(&blob, 0, sizeof(blob)); return err; }
    const uint8_t bank = present ? (uint8_t)(1U - previous[4]) : 0U;
    const size_t total = blob.length + length;
    uint8_t *bytes = malloc(total);
    if (bytes == NULL) { memset(&blob, 0, sizeof(blob)); return ESP_ERR_NO_MEM; }
    memcpy(bytes, blob.bytes, blob.length); if (length) memcpy(bytes + blob.length, prefs, length);
    uint8_t manifest[BUNDLE_MANIFEST] = {'N', '4', 'B', '3', bank};
    put32(manifest + 8U, (uint32_t)total); put32(manifest + 12U, (uint32_t)blob.length);
    put32(manifest + 16U, crc32(bytes, total)); put32(manifest + 20U, crc32(manifest, 20U));
    memset(&blob, 0, sizeof(blob));
    /* Reclaim only the inactive bank's tail; the last committed generation stays intact. */
    const size_t first_unused = (total - 1U) / BUNDLE_CHUNK + 1U;
    size_t tail = first_unused;
    for (;; ++tail) {
        char key[16]; (void)snprintf(key, sizeof(key), "b%u_%08lx", bank, (unsigned long)tail);
        size_t size = 0U;
        err = nvs_get_blob(s_handle, key, NULL, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) { err = ESP_OK; break; }
        if (err != ESP_OK) break;
    }
    while (err == ESP_OK && tail > first_unused) {
        char key[16]; (void)snprintf(key, sizeof(key), "b%u_%08lx", bank, (unsigned long)--tail);
        err = nvs_erase_key(s_handle, key);
    }
    if (err != ESP_OK) { memset(bytes, 0, total); free(bytes); return err; }
    for (size_t offset = 0U, index = 0U; offset < total; ++index) {
        char key[16]; (void)snprintf(key, sizeof(key), "b%u_%08lx", bank, (unsigned long)index);
        const size_t size = total - offset < BUNDLE_CHUNK ? total - offset : BUNDLE_CHUNK;
        err = nvs_set_blob(s_handle, key, bytes + offset, size);
        if (err != ESP_OK) break;
        offset += size;
    }
    memset(bytes, 0, total); free(bytes);
    if (err != ESP_OK) return err;
    err = nvs_commit(s_handle); if (err != ESP_OK) return err;
    /* The active bank is untouched until every candidate chunk is durable. */
    err = nvs_set_blob(s_handle, "bundle_head", manifest, sizeof(manifest));
    if (err == ESP_OK) err = nvs_commit(s_handle);
    if (err == ESP_OK) return ESP_OK;
    /* NVS writes may be visible before commit. Restore the last complete pointer
     * on a reported failure; candidate chunks never overwrite the active bank. */
    esp_err_t restored = present ? nvs_set_blob(s_handle, "bundle_head", previous, sizeof(previous))
        : nvs_erase_key(s_handle, "bundle_head");
    if (restored == ESP_ERR_NVS_NOT_FOUND && !present) restored = ESP_OK;
    if (restored == ESP_OK) restored = nvs_commit(s_handle);
    if (restored != ESP_OK) {
        ESP_LOGE(TAG, "could not restore settings manifest after failed save");
        return ESP_ERR_INVALID_STATE;
    }
    return err;
}
esp_err_t note4_config_store_save(const note4_settings_t *config) {
    note4_settings_t previous; bool found; uint8_t *prefs = NULL; size_t length = 0U;
    esp_err_t err = note4_config_store_load_bundle(&previous, &found, &prefs, &length);
    if (err == ESP_OK) err = note4_config_store_save_bundle(config, prefs, length);
    memset(&previous, 0, sizeof(previous)); free(prefs); return err;
}

esp_err_t note4_config_store_clear(void) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_OK;
    for (unsigned bank = 0U; bank < 2U; ++bank) {
        size_t tail = 0U;
        for (;; ++tail) {
            char key[16]; (void)snprintf(key, sizeof(key), "b%u_%08lx", bank, (unsigned long)tail);
            size_t size = 0U;
            err = nvs_get_blob(s_handle, key, NULL, &size);
            if (err == ESP_ERR_NVS_NOT_FOUND) break;
            if (err != ESP_OK) return err;
        }
        while (tail) {
            char key[16]; (void)snprintf(key, sizeof(key), "b%u_%08lx", bank, (unsigned long)--tail);
            err = nvs_erase_key(s_handle, key);
            if (err != ESP_OK) return err;
        }
    }
    err = nvs_erase_key(s_handle, "bundle_head");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    err = nvs_erase_key(s_handle, "web_settings_v2");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    err = nvs_erase_key(s_handle, "settings");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    err = nvs_erase_key(s_handle, NOTE4_CONFIG_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    return nvs_commit(s_handle);
}
