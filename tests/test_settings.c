#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr, "Assertion failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
    exit(EXIT_FAILURE); \
} } while (0)

#include "settings.h"
#include "provisioning_state.h"

static size_t message(uint8_t *payload, uint8_t mode, const char *endpoint, const char *key) {
    const size_t url_size = strlen(endpoint), key_size = strlen(key);
    payload[0] = mode;
    payload[1] = (uint8_t)url_size;
    payload[2] = (uint8_t)(url_size >> 8U);
    payload[3] = (uint8_t)key_size;
    payload[4] = (uint8_t)(key_size >> 8U);
    memcpy(payload + 5U, endpoint, url_size);
    memcpy(payload + 5U + url_size, key, key_size);
    return 5U + url_size + key_size;
}

static void test_web_key(void) {
    note4_magpie_config_t config = {0};
    memcpy(config.endpoint, "https://magpie.test/prefix", sizeof("https://magpie.test/prefix"));
    memcpy(config.key, "1234567890abcdef", sizeof("1234567890abcdef"));
    assert(note4_magpie_config_validate(&config));
    memcpy(config.key, "gateway-key", sizeof("gateway-key"));
    assert(!note4_magpie_config_validate(&config));
    memcpy(config.key, "1234567890abcde!", sizeof("1234567890abcde!"));
    assert(!note4_magpie_config_validate(&config));
    memcpy(config.key, "1234567890ab-._~", sizeof("1234567890ab-._~"));
    assert(note4_magpie_config_validate(&config));
}

static void test_magpie(void) {
    note4_magpie_config_t saved = {0}, next = {0};
    uint8_t payload[800];
    size_t size = message(payload, 1U, "https://magpie.test/proxy", "web-key-0123456789");
    assert(note4_magpie_config_apply(&saved, &next, payload, size));
    size = message(payload, 0U, saved.endpoint, "");
    assert(note4_magpie_config_apply(&next, &saved, payload, size));
    assert(strcmp(next.key, "web-key-0123456789") == 0);
    size = message(payload, 0U, "https://other.test", "");
    assert(!note4_magpie_config_apply(&next, &saved, payload, size));
    const char *bad[] = {"https://", "ftp://host", "http://user@host", "http://host?a=b",
        "http://host#x", "http://host:0", "http://host:65536", "http://host:", "http://host/path with space"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        size = message(payload, 1U, bad[i], "web-key-0123456789");
        assert(!note4_magpie_config_apply(&next, &saved, payload, size));
    }
    size = message(payload, 1U, "http://192.168.1.2:8080/proxy", "bad key");
    assert(!note4_magpie_config_apply(&next, &saved, payload, size));
    size = message(payload, 1U, "http://192.168.1.2:8080/proxy", "web-key-0123456789");
    assert(note4_magpie_config_apply(&next, &saved, payload, size));
    payload[size - 1U] = 0U;
    assert(!note4_magpie_config_apply(&next, &saved, payload, size));
    size = note4_magpie_config_metadata(&saved, payload, sizeof(payload));
    assert(size == strlen(saved.endpoint) + 3U && payload[2] == 1U);
    assert(memcmp(payload + 3U, saved.endpoint, size - 3U) == 0);
    size = message(payload, 2U, "", "");
    assert(note4_magpie_config_apply(&next, &saved, payload, size));
    assert(next.endpoint[0] == '\0' && next.key[0] == '\0');
    memset(next.endpoint, 'x', sizeof(next.endpoint));
    assert(!note4_magpie_config_validate(&next));
    memset(&next, 0, sizeof(next));
    memcpy(next.endpoint, "https://host/", 13U);
    memset(next.endpoint + 13U, 'a', NOTE4_MAGPIE_ENDPOINT_MAX - 13U);
    memset(next.key, 'k', NOTE4_MAGPIE_KEY_MAX);
    assert(note4_magpie_config_validate(&next));
    size = message(payload, 1U, next.endpoint, next.key);
    assert(size == 773U);
    assert(note4_magpie_config_apply(&saved, &next, payload, size));
    assert(!note4_magpie_config_apply(&saved, &next, payload, size - 1U));
    payload[2] = 3U;
    assert(!note4_magpie_config_apply(&saved, &next, payload, size));
}

static void test_web_settings(void) {
    note4_settings_t saved, staged, restored;
    note4_settings_clear(&saved);
    assert(saved.web.interval_seconds == 30U);
    assert(!saved.web.allow_plain_http && saved.web.adaptive_font && saved.web.layout == 0U);
    assert(saved.web.timezone[0] == '\0');
    staged = saved;
    char canonical[NOTE4_MAGPIE_ENDPOINT_MAX + 1U];
    const char *ambiguous[] = {"https://host/../proxy", "https://host/%2e/proxy", "https://host//proxy"};
    for (size_t i = 0U; i < sizeof(ambiguous) / sizeof(ambiguous[0]); ++i) {
        memcpy(staged.magpie.endpoint, ambiguous[i], strlen(ambiguous[i]) + 1U);
        assert(!note4_settings_validate(&staged));
    }
    assert(note4_magpie_endpoint_canonicalize("https://host:008080/proxy", canonical, sizeof(canonical)));
    assert(strcmp(canonical, "https://host:8080/proxy") == 0);
    staged = saved;
    const uint8_t payload[] = {2, 0, 0, 0, 0, 1, 2, 60, 0, 3, 19, 0, 16, 0,
        'h','t','t','p','s',':','/','/','m','a','g','p','i','e','.','t','e','s','t',
        '1','2','3','4','5','6','7','8','9','0','a','b','c','d','e','f','U','T','C'};
    assert(note4_settings_apply_web(&staged, &saved, payload, sizeof(payload)) == NOTE4_SETTINGS_OK);
    assert(staged.web.interval_seconds == 60U && strcmp(staged.web.timezone, "UTC") == 0);
    assert(strcmp(staged.magpie.key, "1234567890abcdef") == 0);
    note4_settings_blob_t blob;
    assert(note4_settings_serialize(&staged, &blob));
    assert(note4_settings_deserialize(&blob, &restored));
    assert(restored.web.interval_seconds == 60U && strcmp(restored.web.timezone, "UTC") == 0);
    /* Bit 3 selects the display language; old blobs without it stay English. */
    assert(saved.web.language == NOTE4_LANGUAGE_ENGLISH && restored.web.language == NOTE4_LANGUAGE_ENGLISH);
    uint8_t chinese[sizeof(payload)];
    memcpy(chinese, payload, sizeof(payload));
    chinese[6] = 2U | 8U;
    note4_settings_t localized = saved;
    assert(note4_settings_apply_web(&localized, &saved, chinese, sizeof(chinese)) == NOTE4_SETTINGS_OK);
    assert(localized.web.language == NOTE4_LANGUAGE_CHINESE && localized.web.adaptive_font && localized.web.layout == 0U);
    uint8_t metadata[NOTE4_MAGPIE_ENDPOINT_MAX + NOTE4_TIMEZONE_MAX + 12U];
    assert(note4_settings_web_metadata(&localized, metadata, sizeof(metadata)) != 0U && metadata[6] == (2U | 8U));
    assert(note4_settings_serialize(&localized, &blob));
    assert(note4_settings_deserialize(&blob, &restored) && restored.web.language == NOTE4_LANGUAGE_CHINESE);
    chinese[6] = 15U;
    assert(note4_settings_apply_web(&localized, &saved, chinese, sizeof(chinese)) == NOTE4_SETTINGS_OK);
    assert(localized.web.allow_plain_http && localized.web.layout == 1U && localized.web.language == 1U);
    chinese[6] = 16U;
    assert(note4_settings_apply_web(&localized, &saved, chinese, sizeof(chinese)) == NOTE4_SETTINGS_INVALID);
    uint8_t equivalent[sizeof(payload) + 5U];
    memcpy(equivalent, payload, 14U);
    const char *canonical_url = "https://MAGPIE.test:443/";
    const size_t canonical_length = strlen(canonical_url);
    equivalent[5] = 0U;
    equivalent[10] = (uint8_t)canonical_length;
    equivalent[12] = 0U;
    memcpy(equivalent + 14U, canonical_url, canonical_length);
    memcpy(equivalent + 14U + canonical_length, "UTC", 3U);
    saved = staged;
    assert(note4_settings_apply_web(&staged, &saved, equivalent, 17U + canonical_length) == NOTE4_SETTINGS_OK);
    assert(strcmp(staged.magpie.endpoint, "https://magpie.test") == 0);
    saved.revision = 1U;
    assert(note4_settings_apply_web(&staged, &saved, payload, sizeof(payload)) == NOTE4_SETTINGS_STALE_REVISION);
}

static uint32_t legacy_crc(const uint8_t *data, size_t length) {
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8U; ++bit) crc = (crc >> 1U) ^ (0xedb88320U & (uint32_t)-(int32_t)(crc & 1U));
    }
    return ~crc;
}

static void test_legacy_combined(void) {
    note4_settings_legacy_t legacy = {0};
    legacy.magic = 0x4e345354U;
    legacy.version = 1U;
    legacy.payload_size = sizeof(legacy.settings);
    note4_wifi_config_clear(&legacy.settings.wifi);
    assert(note4_wifi_slot_set(&legacy.settings.wifi, 2U, "saved-wifi", "saved-password", true));
    memcpy(legacy.settings.magpie.endpoint, "http://old-gateway", sizeof("http://old-gateway"));
    memcpy(legacy.settings.magpie.key, "gateway-key", sizeof("gateway-key"));
    legacy.crc32 = legacy_crc((const uint8_t *)&legacy, offsetof(note4_settings_legacy_t, crc32));
    note4_settings_t restored;
    assert(note4_settings_migrate_combined(&legacy, &restored));
    assert(strcmp(restored.wifi.slots[2].ssid, "saved-wifi") == 0);
    assert(strcmp(restored.wifi.slots[2].password, "saved-password") == 0);
    assert(restored.magpie.endpoint[0] == 0 && restored.magpie.key[0] == 0 && !restored.web.allow_plain_http);
    const note4_settings_t before = restored;
    legacy.crc32 ^= 1U;
    assert(!note4_settings_migrate_combined(&legacy, &restored));
    assert(memcmp(&before, &restored, sizeof(before)) == 0);
    legacy.version = 9U;
    legacy.crc32 = legacy_crc((const uint8_t *)&legacy, offsetof(note4_settings_legacy_t, crc32));
    assert(!note4_settings_migrate_combined(&legacy, &restored));
}

static void test_storage(void) {
    note4_settings_t settings, restored;
    note4_settings_clear(&settings);
    assert(note4_settings_validate(&settings));
    memcpy(settings.magpie.endpoint, "https://magpie.test", sizeof("https://magpie.test"));
    memcpy(settings.magpie.key, "web-key-0123456789", sizeof("web-key-0123456789"));
    note4_settings_blob_t blob;
    assert(note4_settings_serialize(&settings, &blob));
    assert(note4_settings_deserialize(&blob, &restored));
    assert(strcmp(restored.magpie.key, "web-key-0123456789") == 0);
    const size_t original_length = blob.length;
    const uint8_t extension[] = {0x80, 0x01, 0x03, 0x00, 'n', 'e', 'w'};
    memcpy(blob.bytes + original_length - 4U, extension, sizeof(extension));
    blob.length += sizeof(extension);
    for (unsigned i = 0; i < 4U; ++i) blob.bytes[4U + i] = (uint8_t)(blob.length >> (i * 8U));
    uint32_t crc = legacy_crc(blob.bytes, blob.length - 4U);
    for (unsigned i = 0; i < 4U; ++i) blob.bytes[blob.length - 4U + i] = (uint8_t)(crc >> (i * 8U));
    assert(note4_settings_deserialize(&blob, &restored));
    restored.web.interval_seconds = 60U;
    assert(note4_settings_serialize(&restored, &blob));
    assert(blob.length == original_length + sizeof(extension));
    assert(memcmp(blob.bytes + original_length - 4U, extension, sizeof(extension)) == 0);
    blob.bytes[blob.length - 1U] ^= 1U;
    assert(!note4_settings_deserialize(&blob, &restored));
    assert(note4_wifi_slot_set(&settings.wifi, 0U, "home", "password", true));
    note4_wifi_blob_t legacy;
    assert(note4_wifi_config_serialize(&settings.wifi, 4U, &legacy));
    assert(note4_settings_migrate(&legacy, &restored));
    assert(strcmp(restored.wifi.slots[0].password, "password") == 0);
    assert(restored.magpie.endpoint[0] == '\0');
    assert(note4_settings_serialize(&restored, &blob));
    assert(note4_settings_deserialize(&blob, &restored));
    assert(strcmp(restored.wifi.slots[0].ssid, "home") == 0);
    assert(strcmp(restored.wifi.slots[0].password, "password") == 0);
    legacy.crc32 ^= 1U;
    assert(!note4_settings_migrate(&legacy, &restored));
}

static void transition(note4_provisioning_snapshot_t *state, note4_provisioning_event_type_t type, uint8_t mask) {
    const note4_provisioning_event_t event = {.type = type};
    note4_provisioning_apply(state, &event, mask);
}

static void test_save_and_exit(void) {
    note4_provisioning_snapshot_t state;
    note4_provisioning_init(&state);
    transition(&state, NOTE4_PROVISIONING_EVENT_NO_CONFIG, 0U);
    transition(&state, NOTE4_PROVISIONING_EVENT_COMMIT_ACCEPTED, 0U);
    transition(&state, NOTE4_PROVISIONING_EVENT_COMMIT_FAILED, 0U);
    assert(state.state == NOTE4_PROVISIONING_CONFIGURING);
    transition(&state, NOTE4_PROVISIONING_EVENT_COMMIT_ACCEPTED, 0U);
    transition(&state, NOTE4_PROVISIONING_EVENT_COMMIT_SAVED, 0U);
    assert(state.state == NOTE4_PROVISIONING_CONFIGURING && !state.has_committed_config);
    transition(&state, NOTE4_PROVISIONING_EVENT_COMMIT_ACCEPTED, 1U);
    transition(&state, NOTE4_PROVISIONING_EVENT_COMMIT_SAVED, 1U);
    assert(state.state == NOTE4_PROVISIONING_CONFIGURING && state.has_committed_config);
    transition(&state, NOTE4_PROVISIONING_EVENT_OK, 1U);
    assert(state.state == NOTE4_PROVISIONING_DISCARD_DIALOG);
    transition(&state, NOTE4_PROVISIONING_EVENT_CANCEL, 1U);
    assert(state.state == NOTE4_PROVISIONING_CONFIGURING);
    transition(&state, NOTE4_PROVISIONING_EVENT_OK, 1U);
    transition(&state, NOTE4_PROVISIONING_EVENT_OK, 1U);
    assert(state.state == NOTE4_PROVISIONING_CONNECTING && !state.has_staged_config);
}

int main(void) {
    test_web_key();
    test_magpie();
    test_web_settings();
    test_legacy_combined();
    test_storage();
    test_save_and_exit();
    return 0;
}
