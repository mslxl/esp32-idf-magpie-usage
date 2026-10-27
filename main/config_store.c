#include "config_store.h"
#include "external_memory.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "note4_bytes.h"
#include "resource_budget.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NOTE4_CONFIG_NAMESPACE "note4_wifi"
#define NOTE4_CONFIG_KEY "config"
#define NOTE4_META_NAMESPACE "note4_meta"
#define NOTE4_META_READY_KEY "cfg_ready"

static const char *TAG = "note4_config";
/* Settings live in the dedicated NOTE4_CONFIG_PARTITION. The default "nvs"
 * partition keeps Wi-Fi/PHY/BLE data, the legacy note4_wifi namespace until it
 * has been migrated, and a small marker proving the new partition is ours. */
static nvs_handle_t s_handle;
static bool s_initialized;
static unsigned s_erase_mask;
#define ERASE_DEFAULT 1U
#define ERASE_CONFIG 2U

static esp_err_t head(nvs_handle_t handle, uint8_t manifest[24], bool *found);
static esp_err_t load_from(nvs_handle_t handle, note4_settings_t *config, bool *found, uint8_t **prefs, size_t *length);

bool note4_config_store_erase_recommended(esp_err_t err) {
    return err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND;
}

/* Copy the legacy namespace once, then delete it so old plaintext Wi-Fi
 * passwords and superseded keys do not remain in the default partition. */
static esp_err_t migrate_legacy(void) {
    nvs_handle_t legacy;
    esp_err_t err = nvs_open_from_partition(NVS_DEFAULT_PART_NAME, NOTE4_CONFIG_NAMESPACE, NVS_READONLY, &legacy);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    nvs_close(legacy);
    err = nvs_open_from_partition(NVS_DEFAULT_PART_NAME, NOTE4_CONFIG_NAMESPACE, NVS_READWRITE, &legacy);
    if (err != ESP_OK) return err;
    uint8_t manifest[24]; bool present = false;
    err = head(s_handle, manifest, &present);
    if (err == ESP_OK && !present) {
        note4_settings_t config; bool found = false; uint8_t *prefs = NULL; size_t length = 0U;
        err = load_from(legacy, &config, &found, &prefs, &length);
        if (err == ESP_OK && found) err = note4_config_store_save_bundle(&config, prefs, length);
        else if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "legacy settings are invalid; discarding them");
            err = ESP_OK;
        }
        memset(&config, 0, sizeof(config));
        if (prefs != NULL) { memset(prefs, 0, length); free(prefs); }
    }
    if (err == ESP_OK) {
        esp_err_t erased = nvs_erase_all(legacy);
        if (erased == ESP_OK) erased = nvs_commit(legacy);
        if (erased != ESP_OK) ESP_LOGE(TAG, "legacy settings could not be deleted; retrying next boot");
    }
    nvs_close(legacy);
    return err;
}

static bool config_partition_ready(void) {
    nvs_handle_t meta; uint8_t ready = 0U; size_t size = sizeof(ready);
    if (nvs_open_from_partition(NVS_DEFAULT_PART_NAME, NOTE4_META_NAMESPACE, NVS_READONLY, &meta) != ESP_OK) return false;
    const bool found = nvs_get_blob(meta, NOTE4_META_READY_KEY, &ready, &size) == ESP_OK && size == 1U && ready == 1U;
    nvs_close(meta);
    return found;
}

static esp_err_t mark_config_partition_ready(void) {
    nvs_handle_t meta; const uint8_t ready = 1U;
    esp_err_t err = nvs_open_from_partition(NVS_DEFAULT_PART_NAME, NOTE4_META_NAMESPACE, NVS_READWRITE, &meta);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(meta, NOTE4_META_READY_KEY, &ready, sizeof(ready));
    if (err == ESP_OK) err = nvs_commit(meta);
    nvs_close(meta);
    return err;
}

esp_err_t note4_config_store_init(void) {
    if (s_initialized) { nvs_close(s_handle); s_initialized = false; }
    s_erase_mask = 0U;
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        if (note4_config_store_erase_recommended(err)) s_erase_mask |= ERASE_DEFAULT;
        return err;
    }
    const bool ready = config_partition_ready();
    err = nvs_flash_init_partition(NOTE4_CONFIG_PARTITION);
    if (note4_config_store_erase_recommended(err) && !ready) {
        /* Never formatted by this firmware: the region holds unrelated bytes. */
        ESP_LOGE(TAG, "formatting new settings partition");
        err = nvs_flash_erase_partition(NOTE4_CONFIG_PARTITION);
        if (err == ESP_OK) err = nvs_flash_init_partition(NOTE4_CONFIG_PARTITION);
    }
    if (err != ESP_OK) {
        if (note4_config_store_erase_recommended(err)) s_erase_mask |= ERASE_CONFIG;
        return err;
    }
    err = nvs_open_from_partition(NOTE4_CONFIG_PARTITION, NOTE4_CONFIG_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) return err;
    s_initialized = true;
    if (!ready) err = mark_config_partition_ready();
    if (err == ESP_OK) err = migrate_legacy();
    if (err != ESP_OK) { nvs_close(s_handle); s_initialized = false; }
    return err;
}

esp_err_t note4_config_store_erase(void) {
    if (s_initialized) { nvs_close(s_handle); s_initialized = false; }
    const unsigned mask = s_erase_mask != 0U ? s_erase_mask : ERASE_CONFIG;
    esp_err_t err = ESP_OK;
    if (mask & ERASE_DEFAULT) err = nvs_flash_erase();
    if (err == ESP_OK && (mask & ERASE_CONFIG)) err = nvs_flash_erase_partition(NOTE4_CONFIG_PARTITION);
    if (err == ESP_OK) s_erase_mask = 0U;
    return err;
}

static esp_err_t load_legacy(nvs_handle_t handle, note4_settings_t *config, bool *found) {
    *found = false;
    note4_settings_clear(config);
    note4_settings_blob_t blob = {0};
    size_t size = sizeof(blob.bytes);
    esp_err_t err = nvs_get_blob(handle, "web_settings_v2", blob.bytes, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        note4_settings_legacy_t combined = {0};
        size = sizeof(combined);
        err = nvs_get_blob(handle, "settings", &combined, &size);
        if (err == ESP_OK) {
            const bool valid = size == sizeof(combined) && note4_settings_migrate_combined(&combined, config);
            memset(&combined, 0, sizeof(combined));
            if (!valid) return ESP_ERR_INVALID_STATE;
            *found = true;
            return ESP_OK;
        }
        if (err != ESP_ERR_NVS_NOT_FOUND) return err;
        note4_wifi_blob_t legacy = {0};
        size = sizeof(legacy);
        err = nvs_get_blob(handle, NOTE4_CONFIG_KEY, &legacy, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
        if (err != ESP_OK) return err;
        const bool valid = size == sizeof(legacy) && note4_settings_migrate(&legacy, config);
        memset(&legacy, 0, sizeof(legacy));
        if (!valid) return ESP_ERR_INVALID_STATE;
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
#define BUNDLE_UNDO 36U
static esp_err_t head(nvs_handle_t handle, uint8_t manifest[BUNDLE_MANIFEST], bool *found) {
    uint8_t undo[BUNDLE_UNDO];
    size_t size = sizeof(undo);
    esp_err_t err = nvs_get_blob(handle, "bundle_undo", undo, &size);
    *found = false;
    if (err == ESP_OK) {
        if (size != sizeof(undo) || memcmp(undo, "N4U1", 4U) || undo[4] > 1U ||
            undo[5] || undo[6] || undo[7] || note4_crc32(undo, 32U) != note4_get32(undo + 32U)) return ESP_ERR_INVALID_STATE;
        *found = undo[4] != 0U;
        if (!*found) return ESP_OK;
        memcpy(manifest, undo + 8U, BUNDLE_MANIFEST);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        size = BUNDLE_MANIFEST;
        err = nvs_get_blob(handle, "bundle_head", manifest, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
        if (err != ESP_OK) return err;
        if (size != BUNDLE_MANIFEST) return ESP_ERR_INVALID_STATE;
        *found = true;
    } else return err;
    if (memcmp(manifest, "N4B3", 4U) || manifest[4] > 1U ||
        manifest[5] || manifest[6] || manifest[7] || note4_crc32(manifest, 20U) != note4_get32(manifest + 20U)) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
esp_err_t note4_config_store_load_bundle(note4_settings_t *config, bool *found, uint8_t **prefs, size_t *length) {
    if (config == NULL || found == NULL || prefs == NULL || length == NULL || !s_initialized) return ESP_ERR_INVALID_ARG;
    return load_from(s_handle, config, found, prefs, length);
}
static esp_err_t load_from(nvs_handle_t handle, note4_settings_t *config, bool *found, uint8_t **prefs, size_t *length) {
    *found = false;
    uint8_t manifest[BUNDLE_MANIFEST]; bool present;
    esp_err_t err = head(handle, manifest, &present);
    if (err != ESP_OK) return err;
    if (!present) { err = load_legacy(handle, config, found); if (err == ESP_OK) { *prefs = NULL; *length = 0U; } return err; }
    const uint32_t total = note4_get32(manifest + 8U), settings_length = note4_get32(manifest + 12U);
    if (settings_length > NOTE4_SETTINGS_MAX_BYTES || settings_length > total || settings_length == 0U) return ESP_ERR_INVALID_STATE;
    uint8_t *bytes = note4_external_malloc(total);
    if (bytes == NULL) return ESP_ERR_NO_MEM;
    size_t offset = 0U;
    for (uint32_t index = 0U; offset < total; ++index) {
        char key[16]; (void)snprintf(key, sizeof(key), "b%u_%08lx", manifest[4], (unsigned long)index);
        const size_t expected = total - offset < BUNDLE_CHUNK ? total - offset : BUNDLE_CHUNK;
        size_t size = expected;
        err = nvs_get_blob(handle, key, bytes + offset, &size);
        if (err != ESP_OK || size != expected) { memset(bytes, 0, total); free(bytes); return err == ESP_OK ? ESP_ERR_INVALID_STATE : err; }
        offset += size;
    }
    note4_settings_blob_t blob = {.length = settings_length};
    memcpy(blob.bytes, bytes, settings_length);
    note4_settings_t candidate = {0};
    const bool valid = note4_crc32(bytes, total) == note4_get32(manifest + 16U) && note4_settings_deserialize(&blob, &candidate);
    memset(&blob, 0, sizeof(blob));
    if (!valid) { memset(bytes, 0, total); free(bytes); return ESP_ERR_INVALID_STATE; }
    const size_t prefs_length = total - settings_length;
    uint8_t *document = prefs_length ? note4_external_malloc(prefs_length) : NULL;
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
    esp_err_t err = head(s_handle, previous, &present);
    if (err == ESP_ERR_INVALID_STATE && present && memcmp(previous, "N4B3", 4U) == 0 &&
        previous[4] <= 1U && !previous[5] && !previous[6] && !previous[7]) {
        /* Explicit setup saves can replace a damaged known manifest. Preserve its
         * bank and pointer until the replacement has been written completely. */
        size_t previous_size = sizeof(previous);
        err = nvs_get_blob(s_handle, "bundle_head", previous, &previous_size);
        if (err == ESP_OK && previous_size != sizeof(previous)) err = ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) { memset(&blob, 0, sizeof(blob)); return err; }
    uint8_t undo[BUNDLE_UNDO] = {'N', '4', 'U', '1', present ? 1U : 0U};
    if (present) memcpy(undo + 8U, previous, sizeof(previous));
    note4_put32(undo + 32U, note4_crc32(undo, 32U));
    size_t pending_size = 0U;
    err = nvs_get_blob(s_handle, "bundle_undo", NULL, &pending_size);
    if (err == ESP_OK) {
        /* Finish an interrupted rollback before reusing either bank. */
        err = present ? nvs_set_blob(s_handle, "bundle_head", previous, sizeof(previous))
            : nvs_erase_key(s_handle, "bundle_head");
        if (err == ESP_ERR_NVS_NOT_FOUND && !present) err = ESP_OK;
        if (err == ESP_OK) err = nvs_commit(s_handle);
        if (err == ESP_OK) err = nvs_erase_key(s_handle, "bundle_undo");
        if (err == ESP_OK) err = nvs_commit(s_handle);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err != ESP_OK) { memset(&blob, 0, sizeof(blob)); return err; }
    const uint8_t bank = present ? (uint8_t)(1U - previous[4]) : 0U;
    const size_t total = blob.length + length;
    uint8_t *bytes = note4_external_malloc(total);
    if (bytes == NULL) { memset(&blob, 0, sizeof(blob)); return ESP_ERR_NO_MEM; }
    memcpy(bytes, blob.bytes, blob.length); if (length) memcpy(bytes + blob.length, prefs, length);
    uint8_t manifest[BUNDLE_MANIFEST] = {'N', '4', 'B', '3', bank};
    note4_put32(manifest + 8U, (uint32_t)total); note4_put32(manifest + 12U, (uint32_t)blob.length);
    note4_put32(manifest + 16U, note4_crc32(bytes, total)); note4_put32(manifest + 20U, note4_crc32(manifest, 20U));
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
    /* A durable undo pointer keeps failed or interrupted publication on the old bank. */
    err = nvs_set_blob(s_handle, "bundle_undo", undo, sizeof(undo));
    if (err == ESP_OK) err = nvs_commit(s_handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(s_handle, "bundle_head", manifest, sizeof(manifest));
    if (err == ESP_OK) err = nvs_commit(s_handle);
    if (err == ESP_OK) {
        err = nvs_erase_key(s_handle, "bundle_undo");
        if (err == ESP_OK) err = nvs_commit(s_handle);
        if (err == ESP_OK) return ESP_OK;
    }
    /* Erasing the undo record may be visible even when its commit fails. Re-arm
     * it before restoring the head, so either successful write preserves the old bank. */
    esp_err_t guarded = nvs_set_blob(s_handle, "bundle_undo", undo, sizeof(undo));
    if (guarded == ESP_OK) guarded = nvs_commit(s_handle);
    esp_err_t restored = present ? nvs_set_blob(s_handle, "bundle_head", previous, sizeof(previous))
        : nvs_erase_key(s_handle, "bundle_head");
    if (restored == ESP_ERR_NVS_NOT_FOUND && !present) restored = ESP_OK;
    if (restored == ESP_OK) restored = nvs_commit(s_handle);
    if (restored != ESP_OK && guarded != ESP_OK) {
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
    err = nvs_erase_key(s_handle, "bundle_undo");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    err = nvs_erase_key(s_handle, "web_settings_v2");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    err = nvs_erase_key(s_handle, "settings");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    err = nvs_erase_key(s_handle, NOTE4_CONFIG_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    return nvs_commit(s_handle);
}

/* NVS stores a blob chunk as one header entry per page span plus 32-byte data
 * entries, and a blob index entry per key. */
static size_t blob_entries(size_t length) {
    size_t entries = 1U;
    for (size_t offset = 0U; offset < length; offset += BUNDLE_CHUNK) {
        const size_t size = length - offset < BUNDLE_CHUNK ? length - offset : BUNDLE_CHUNK;
        entries += (size + 31U) / 32U + 2U;
    }
    return entries;
}

esp_err_t note4_config_store_check_capacity(size_t prefs_length) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (prefs_length > NOTE4_DOCUMENT_BUDGET) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    nvs_stats_t stats;
    esp_err_t err = nvs_get_stats(NOTE4_CONFIG_PARTITION, &stats);
    if (err != ESP_OK) return err;
    /* Rewriting the inactive bank frees its old chunks as each key is replaced. */
    uint8_t manifest[BUNDLE_MANIFEST]; bool present = false;
    size_t reclaimable = 0U;
    if (head(s_handle, manifest, &present) == ESP_OK && present) {
        const unsigned bank = 1U - manifest[4];
        for (uint32_t index = 0U;; ++index) {
            char key[16]; (void)snprintf(key, sizeof(key), "b%u_%08lx", bank, (unsigned long)index);
            size_t size = 0U;
            if (nvs_get_blob(s_handle, key, NULL, &size) != ESP_OK) break;
            reclaimable += (size + 31U) / 32U + 2U;
        }
    }
    const size_t required = blob_entries(NOTE4_SETTINGS_MAX_BYTES + prefs_length) +
        (BUNDLE_CHUNK + 31U) / 32U + 2U + 8U;
    return stats.available_entries + reclaimable >= required ? ESP_OK : ESP_ERR_NVS_NOT_ENOUGH_SPACE;
}
