#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config_store.h"
#include "nvs.h"
#include "nvs_flash.h"

/* Handles: 1 = note4_cfg/note4_wifi, 2 = nvs/note4_wifi (legacy), 3 = nvs/note4_meta. */
typedef struct {
    unsigned handle; char key[16];
    uint8_t durable[4096], pending[4096]; size_t durable_length, pending_length; bool erased;
} entry_t;
static entry_t entries[64];
static size_t entry_count;
static entry_t *lookup(unsigned handle, const char *key) {
    for (size_t i = 0U; i < entry_count; ++i) if (entries[i].handle == handle && !strcmp(entries[i].key, key)) return &entries[i];
    if (entry_count == sizeof(entries) / sizeof(entries[0]) || strlen(key) >= sizeof(entries[0].key)) exit(1);
    entry_t *entry = &entries[entry_count++]; entry->handle = handle; memcpy(entry->key, key, strlen(key) + 1U); return entry;
}
static size_t namespace_size(unsigned handle) {
    size_t count = 0U;
    for (size_t i = 0U; i < entry_count; ++i) if (entries[i].handle == handle && entries[i].durable_length) ++count;
    return count;
}
static esp_err_t init_error, config_init_error, write_error, commit_error;
static unsigned erases, config_erases;
static size_t available_entries = 100000U;

esp_err_t nvs_flash_init(void) { return init_error; }
esp_err_t nvs_flash_erase(void) { ++erases; init_error = ESP_OK; return ESP_OK; }
esp_err_t nvs_flash_init_partition(const char *label) { if (strcmp(label, "note4_cfg")) exit(1); return config_init_error; }
esp_err_t nvs_flash_erase_partition(const char *label) {
    if (strcmp(label, "note4_cfg")) exit(1);
    ++config_erases; config_init_error = ESP_OK;
    for (size_t i = 0U; i < entry_count; ++i) if (entries[i].handle == 1U) entries[i].durable_length = 0U;
    return ESP_OK;
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) { (void)name; (void)mode; (void)handle; exit(1); }
esp_err_t nvs_open_from_partition(const char *part, const char *name, int mode, nvs_handle_t *handle) {
    unsigned id = 0U;
    if (!strcmp(part, "note4_cfg") && !strcmp(name, "note4_wifi")) id = 1U;
    else if (!strcmp(part, "nvs") && !strcmp(name, "note4_wifi")) id = 2U;
    else if (!strcmp(part, "nvs") && !strcmp(name, "note4_meta")) id = 3U;
    else exit(1);
    if (mode == NVS_READONLY && namespace_size(id) == 0U) return ESP_ERR_NVS_NOT_FOUND;
    *handle = id; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *size) {
    entry_t *entry = lookup(handle, key);
    if (entry->durable_length == 0U) return ESP_ERR_NVS_NOT_FOUND;
    if (data && *size < entry->durable_length) return ESP_ERR_NVS_INVALID_LENGTH;
    *size = entry->durable_length;
    if (data) memcpy(data, entry->durable, entry->durable_length);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size) {
    if (write_error && handle == 1U) return write_error;
    entry_t *entry = lookup(handle, key);
    if (size > sizeof(entry->pending)) return ESP_ERR_NO_MEM;
    memcpy(entry->pending, data, size); entry->pending_length = size;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    const esp_err_t result = handle == 1U ? commit_error : ESP_OK;
    for (size_t i = 0U; i < entry_count; ++i) {
        entry_t *entry = &entries[i];
        if (entry->handle != handle) continue;
        if (!result && entry->erased) entry->durable_length = 0U;
        if (!result && entry->pending_length) {
            memcpy(entry->durable, entry->pending, entry->pending_length);
            entry->durable_length = entry->pending_length;
        }
        entry->pending_length = 0U;
        entry->erased = false;
    }
    return result;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) { lookup(handle, key)->erased = true; return ESP_OK; }
esp_err_t nvs_erase_all(nvs_handle_t handle) {
    for (size_t i = 0U; i < entry_count; ++i) if (entries[i].handle == handle) entries[i].erased = true;
    return ESP_OK;
}
esp_err_t nvs_get_stats(const char *part, nvs_stats_t *stats) {
    if (strcmp(part, "note4_cfg")) exit(1);
    memset(stats, 0, sizeof(*stats)); stats->available_entries = available_entries; return ESP_OK;
}
static void put_durable(unsigned handle, const char *key, const void *data, size_t size) {
    entry_t *entry = lookup(handle, key); memcpy(entry->durable, data, size); entry->durable_length = size;
}

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "failed %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
int main(void) {
    /* A full default partition is reported, not silently erased. */
    init_error = ESP_ERR_NVS_NO_FREE_PAGES;
    CHECK(note4_config_store_init() == ESP_ERR_NVS_NO_FREE_PAGES);
    CHECK(note4_config_store_erase_recommended(ESP_ERR_NVS_NO_FREE_PAGES));
    CHECK(note4_config_store_erase_recommended(ESP_ERR_NVS_NEW_VERSION_FOUND));
    CHECK(!note4_config_store_erase_recommended(ESP_FAIL));
    CHECK(erases == 0U && config_erases == 0U);
    CHECK(note4_config_store_erase() == ESP_OK && erases == 1U && config_erases == 0U);

    /* Legacy settings migrate once into note4_cfg, then the legacy namespace is deleted. */
    note4_settings_t saved, loaded;
    note4_settings_clear(&saved);
    CHECK(note4_wifi_slot_set(&saved.wifi, 0U, "legacy", "plaintext-pass", true));
    saved.web.language = NOTE4_LANGUAGE_CHINESE;
    saved.web.interval_seconds = 120U;
    note4_settings_blob_t blob;
    CHECK(note4_settings_serialize(&saved, &blob));
    put_durable(2U, "web_settings_v2", blob.bytes, blob.length);
    put_durable(2U, "config", "stale", 5U);
    put_durable(2U, "b1_00000000", "orphan", 6U);
    /* A never-formatted settings region is formatted automatically. */
    config_init_error = ESP_ERR_NVS_NEW_VERSION_FOUND;
    CHECK(note4_config_store_init() == ESP_OK && config_erases == 1U);
    CHECK(namespace_size(2U) == 0U);
    CHECK(namespace_size(3U) == 1U);
    bool found = false;
    CHECK(note4_config_store_load(&loaded, &found) == ESP_OK && found);
    CHECK(strcmp(loaded.wifi.slots[0].ssid, "legacy") == 0 && strcmp(loaded.wifi.slots[0].password, "plaintext-pass") == 0);
    CHECK(loaded.web.interval_seconds == 120U && loaded.web.language == NOTE4_LANGUAGE_CHINESE);
    CHECK(lookup(1U, "web_settings_v2")->durable_length == 0U);

    /* Once the partition is ours, NO_FREE_PAGES needs an explicit erase of note4_cfg only. */
    config_init_error = ESP_ERR_NVS_NO_FREE_PAGES;
    CHECK(note4_config_store_init() == ESP_ERR_NVS_NO_FREE_PAGES && config_erases == 1U);
    CHECK(note4_config_store_erase() == ESP_OK && config_erases == 2U && erases == 1U);
    CHECK(note4_config_store_init() == ESP_OK);
    CHECK(note4_config_store_load(&loaded, &found) == ESP_OK && !found);

    /* A legacy namespace that reappears after migration never overrides note4_cfg. */
    CHECK(note4_config_store_save(&saved) == ESP_OK);
    put_durable(2U, "config", "stale", 5U);
    CHECK(note4_config_store_init() == ESP_OK && namespace_size(2U) == 0U);
    CHECK(note4_config_store_load(&loaded, &found) == ESP_OK && found && loaded.web.interval_seconds == 120U);

    /* Failed writes keep the previous generation. */
    saved.web.interval_seconds = 300U;
    write_error = ESP_ERR_NO_MEM;
    CHECK(note4_config_store_save(&saved) == ESP_ERR_NO_MEM);
    CHECK(note4_config_store_load(&loaded, &found) == ESP_OK && found);
    CHECK(loaded.web.interval_seconds == 120U && strcmp(loaded.wifi.slots[0].ssid, "legacy") == 0);
    write_error = ESP_OK;
    commit_error = ESP_FAIL;
    CHECK(note4_config_store_save(&saved) == ESP_FAIL);
    CHECK(note4_config_store_load(&loaded, &found) == ESP_OK && found);
    CHECK(loaded.web.interval_seconds == 120U);
    commit_error = ESP_OK;

    /* Display preference uploads are rejected before transfer when NVS cannot hold them. */
    available_entries = 100000U;
    CHECK(note4_config_store_check_capacity(200U * 1024U) == ESP_OK);
    CHECK(note4_config_store_check_capacity(1024U * 1024U) == ESP_ERR_NVS_NOT_ENOUGH_SPACE);
    available_entries = 400U;
    CHECK(note4_config_store_check_capacity(4096U) == ESP_OK);
    CHECK(note4_config_store_check_capacity(64U * 1024U) == ESP_ERR_NVS_NOT_ENOUGH_SPACE);
    available_entries = 100000U;

    /* The previous firmware kept the two-bank bundle in nvs/note4_wifi; it moves with its prefs. */
    uint8_t prefs[5000]; memset(prefs, 'p', sizeof(prefs));
    saved.web.interval_seconds = 240U;
    CHECK(note4_config_store_save_bundle(&saved, prefs, sizeof(prefs)) == ESP_OK);
    for (size_t i = 0U; i < entry_count; ++i) if (entries[i].handle == 2U) entries[i].handle = 99U;
    for (size_t i = 0U; i < entry_count; ++i) if (entries[i].handle == 1U) entries[i].handle = 2U;
    CHECK(note4_config_store_init() == ESP_OK && namespace_size(2U) == 0U);
    uint8_t *received = NULL; size_t received_length = 0U;
    CHECK(note4_config_store_load_bundle(&loaded, &found, &received, &received_length) == ESP_OK && found);
    CHECK(loaded.web.interval_seconds == 240U && received_length == sizeof(prefs) && !memcmp(received, prefs, sizeof(prefs)));
    free(received);
    CHECK(erases == 1U);
    puts("config store migration, erase and capacity checks passed");
    return 0;
}
