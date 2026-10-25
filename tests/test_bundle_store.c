#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config_store.h"
#include "nvs.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed %d: %s\n", __LINE__, #x); exit(1); } } while (0)
typedef struct { char key[16]; uint8_t *data, *pending; size_t length, pending_length; bool erased; } entry_t;
static entry_t entries[256]; static size_t count; static int writes, commits, fail_write, fail_commit;
static bool immediate;
static entry_t *entry(const char *key) {
    for (size_t i = 0; i < count; ++i) if (!strcmp(entries[i].key, key)) return &entries[i];
    CHECK(count < 256 && strlen(key) < 16); entry_t *e = &entries[count++]; memcpy(e->key, key, strlen(key) + 1); return e;
}
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) { (void)name; (void)mode; *handle = 1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *size) {
    (void)handle; entry_t *e = entry(key); if (!e->data) return ESP_ERR_NVS_NOT_FOUND;
    if (data && *size < e->length) return ESP_ERR_NVS_INVALID_LENGTH;
    *size = e->length; if (data) memcpy(data, e->data, e->length); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size) {
    (void)handle; if (++writes == fail_write) return ESP_ERR_NO_MEM;
    entry_t *e = entry(key);
    if (immediate) { uint8_t *copy = malloc(size); CHECK(copy); memcpy(copy, data, size); free(e->data); e->data = copy; e->length = size; return ESP_OK; }
    free(e->pending); e->pending = malloc(size); CHECK(e->pending); memcpy(e->pending, data, size); e->pending_length = size; return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    (void)handle; entry_t *e = entry(key);
    if (immediate) { free(e->data); e->data = NULL; e->length = 0; }
    else e->erased = true;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle; const bool failed = ++commits == fail_commit;
    for (size_t i = 0; i < count; ++i) {
        entry_t *e = &entries[i];
        if (!failed && e->erased) { free(e->data); e->data = NULL; e->length = 0; }
        if (!failed && e->pending) { free(e->data); e->data = e->pending; e->length = e->pending_length; e->pending = NULL; }
        free(e->pending); e->pending = NULL; e->erased = false;
    }
    return failed ? ESP_FAIL : ESP_OK;
}
int main(void) {
    CHECK(note4_config_store_init() == ESP_OK);
    note4_settings_t settings, loaded; note4_settings_clear(&settings);
    uint8_t *prefs = malloc(9000), *received = NULL; CHECK(prefs); memset(prefs, 'x', 9000);
    CHECK(note4_config_store_save_bundle(&settings, prefs, 9000) == ESP_OK);
    bool found; size_t length = 0;
    CHECK(note4_config_store_load_bundle(&loaded, &found, &received, &length) == ESP_OK && found);
    CHECK(length == 9000 && !memcmp(prefs, received, length)); free(received); received = NULL;
    for (int fail = 1; fail <= 7; ++fail) {
        settings.web.interval_seconds = 300;
        fail_write = writes + fail;
        const esp_err_t result = note4_config_store_save_bundle(&settings, (const uint8_t *)"new", 3);
        fail_write = 0;
        if (result == ESP_OK) { /* Only two writes are needed for this small candidate. */
            settings.web.interval_seconds = 30; CHECK(note4_config_store_save_bundle(&settings, prefs, 9000) == ESP_OK);
        }
        CHECK(note4_config_store_load_bundle(&loaded, &found, &received, &length) == ESP_OK && found);
        CHECK(loaded.web.interval_seconds == 30 && length == 9000 && !memcmp(prefs, received, length)); free(received); received = NULL;
    }
    for (int fail = 1; fail <= 2; ++fail) {
        settings.web.interval_seconds = 600; fail_commit = commits + fail;
        CHECK(note4_config_store_save_bundle(&settings, (const uint8_t *)"new", 3) != ESP_OK); fail_commit = 0;
        CHECK(note4_config_store_load_bundle(&loaded, &found, &received, &length) == ESP_OK && found);
        CHECK(loaded.web.interval_seconds == 30 && length == 9000); free(received); received = NULL;
    }
    settings.web.interval_seconds = 60;
    CHECK(note4_config_store_save(&settings) == ESP_OK);
    CHECK(note4_config_store_load_bundle(&loaded, &found, &received, &length) == ESP_OK && length == 9000 && loaded.web.interval_seconds == 60);
    free(received); received = NULL;
    immediate = true;
    for (int fail = 1; fail <= 4; ++fail) {
        settings.web.interval_seconds = 300; fail_write = writes + fail;
        CHECK(note4_config_store_save_bundle(&settings, prefs, 9000) != ESP_OK); fail_write = 0;
        CHECK(note4_config_store_load_bundle(&loaded, &found, &received, &length) == ESP_OK && found);
        CHECK(loaded.web.interval_seconds == 60 && length == 9000 && !memcmp(received, prefs, length));
        free(received); received = NULL;
    }
    settings.web.interval_seconds = 600; fail_commit = commits + 2;
    CHECK(note4_config_store_save_bundle(&settings, (const uint8_t *)"new", 3) != ESP_OK); fail_commit = 0;
    CHECK(note4_config_store_load_bundle(&loaded, &found, &received, &length) == ESP_OK && found);
    CHECK(loaded.web.interval_seconds == 60 && length == 9000);
    free(received); received = NULL;
    settings.web.interval_seconds = 90;
    CHECK(note4_config_store_save_bundle(&settings, (const uint8_t *)"small", 5) == ESP_OK);
    CHECK(note4_config_store_save_bundle(&settings, (const uint8_t *)"small", 5) == ESP_OK);
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(entries[i].key, "b0_00000001") || !strcmp(entries[i].key, "b1_00000001")) CHECK(entries[i].data == NULL);
    entry("bundle_head")->data[20] ^= 1U;
    CHECK(note4_config_store_load_bundle(&loaded, &found, &received, &length) == ESP_ERR_INVALID_STATE && !found);
    settings.web.interval_seconds = 120;
    uint8_t damaged[24]; memcpy(damaged, entry("bundle_head")->data, sizeof(damaged));
    fail_write = writes + 2;
    CHECK(note4_config_store_save_bundle(&settings, (const uint8_t *)"repaired", 8) != ESP_OK);
    fail_write = 0;
    CHECK(!memcmp(damaged, entry("bundle_head")->data, sizeof(damaged)));
    CHECK(note4_config_store_save_bundle(&settings, (const uint8_t *)"repaired", 8) == ESP_OK);
    CHECK(note4_config_store_load_bundle(&loaded, &found, &received, &length) == ESP_OK && found);
    CHECK(loaded.web.interval_seconds == 120 && length == 8 && !memcmp(received, "repaired", 8));
    free(received); received = NULL;
    CHECK(note4_config_store_clear() == ESP_OK);
    CHECK(note4_config_store_load_bundle(&loaded, &found, &received, &length) == ESP_OK && !found);
    for (size_t i = 0; i < count; ++i) if (entries[i].key[0] == 'b') CHECK(entries[i].data == NULL);
    free(prefs);
    for (size_t i = 0; i < count; ++i) { free(entries[i].data); free(entries[i].pending); }
    puts("immutable settings/prefs generations preserve previous complete commit on failures"); return 0;
}
