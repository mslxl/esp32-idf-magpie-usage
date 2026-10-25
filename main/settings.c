#include "settings.h"

#include <string.h>

static uint32_t checksum(const uint8_t *data, size_t length) {
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8U; ++bit) crc = (crc >> 1U) ^ (0xedb88320U & (uint32_t)-(int32_t)(crc & 1U));
    }
    return ~crc;
}

static uint32_t read32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static void write32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4U; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

bool note4_settings_timezone_supported(const char *timezone) {
    static const char *const zones[] = {
#include "supported_timezones.inc"
    };
    if (timezone == NULL) return false;
    for (size_t i = 0; i < sizeof(zones) / sizeof(zones[0]); ++i) {
        if (strcmp(timezone, zones[i]) == 0) return true;
    }
    return false;
}

void note4_settings_clear(note4_settings_t *settings) {
    memset(settings, 0, sizeof(*settings));
    note4_wifi_config_clear(&settings->wifi);
    settings->web.interval_seconds = 30U;
    settings->web.adaptive_font = true;
}

bool note4_settings_validate(const note4_settings_t *settings) {
    char endpoint[NOTE4_MAGPIE_ENDPOINT_MAX + 1U];
    if (settings == NULL || !note4_magpie_config_validate(&settings->magpie) ||
        !note4_magpie_endpoint_canonicalize(settings->magpie.endpoint, endpoint, sizeof(endpoint)) ||
        settings->web.interval_seconds < 30U || settings->web.interval_seconds > 3600U ||
        settings->web.layout > 1U || settings->extension_length > sizeof(settings->extensions) ||
        memchr(settings->web.timezone, 0, sizeof(settings->web.timezone)) == NULL ||
        !note4_settings_timezone_supported(settings->web.timezone) ||
        (!settings->web.allow_plain_http && strncmp(settings->magpie.endpoint, "http://", 7U) == 0)) return false;
    if (note4_wifi_config_enabled_mask(&settings->wifi) != 0U) return note4_wifi_config_validate(&settings->wifi);
    note4_wifi_config_t empty;
    note4_wifi_config_clear(&empty);
    empty.generation = settings->wifi.generation;
    return memcmp(&empty, &settings->wifi, sizeof(empty)) == 0;
}

note4_settings_result_t note4_settings_apply_web(note4_settings_t *target,
    const note4_settings_t *saved, const uint8_t *payload, size_t length) {
    if (target == NULL || saved == NULL || payload == NULL || length < 14U) return NOTE4_SETTINGS_INVALID;
    if (payload[0] != NOTE4_WEB_SCHEMA) return NOTE4_SETTINGS_UNSUPPORTED_SCHEMA;
    if (read32(payload + 1U) != saved->revision) return NOTE4_SETTINGS_STALE_REVISION;
    const uint8_t mode = payload[5], flags = payload[6];
    const size_t zone_length = payload[9];
    const size_t endpoint_length = payload[10] | ((size_t)payload[11] << 8U);
    const size_t key_length = payload[12] | ((size_t)payload[13] << 8U);
    if (mode > 2U || flags > 7U || zone_length > NOTE4_TIMEZONE_MAX ||
        endpoint_length > NOTE4_MAGPIE_ENDPOINT_MAX || key_length > NOTE4_MAGPIE_KEY_MAX ||
        length != 14U + endpoint_length + key_length + zone_length ||
        (mode != 1U && key_length != 0U) || (mode == 1U && key_length < 16U) ||
        memchr(payload + 14U, 0, length - 14U) != NULL) return NOTE4_SETTINGS_INVALID;
    note4_settings_t next = *target;
    next.web.allow_plain_http = (flags & 1U) != 0U;
    next.web.adaptive_font = (flags & 2U) != 0U;
    next.web.layout = (flags >> 2U) & 1U;
    next.web.interval_seconds = payload[7] | ((uint16_t)payload[8] << 8U);
    memset(&next.web.timezone, 0, sizeof(next.web.timezone));
    memcpy(next.web.timezone, payload + 14U + endpoint_length + key_length, zone_length);
    memset(&next.magpie, 0, sizeof(next.magpie));
    char endpoint[NOTE4_MAGPIE_ENDPOINT_MAX + 1U] = {0};
    memcpy(endpoint, payload + 14U, endpoint_length);
    if (!note4_magpie_endpoint_canonicalize(endpoint, next.magpie.endpoint, sizeof(next.magpie.endpoint))) return NOTE4_SETTINGS_INVALID;
    if (mode == 0U) {
        char previous[NOTE4_MAGPIE_ENDPOINT_MAX + 1U];
        if (!note4_magpie_endpoint_canonicalize(saved->magpie.endpoint, previous, sizeof(previous)) ||
            strcmp(previous, next.magpie.endpoint) != 0) return NOTE4_SETTINGS_INVALID;
        memcpy(next.magpie.key, saved->magpie.key, sizeof(next.magpie.key));
    } else if (mode == 1U) {
        memcpy(next.magpie.key, payload + 14U + endpoint_length, key_length);
    }
    const bool valid = note4_settings_validate(&next);
    if (valid) *target = next;
    memset(&next, 0, sizeof(next));
    return valid ? NOTE4_SETTINGS_OK : NOTE4_SETTINGS_INVALID;
}

size_t note4_settings_web_metadata(const note4_settings_t *settings, uint8_t *output, size_t capacity) {
    if (!note4_settings_validate(settings)) return 0U;
    const size_t endpoint_length = strlen(settings->magpie.endpoint), zone_length = strlen(settings->web.timezone);
    const size_t length = 12U + endpoint_length + zone_length;
    if (output == NULL || capacity < length) return 0U;
    memset(output, 0, length);
    output[0] = NOTE4_WEB_SCHEMA;
    write32(output + 1U, settings->revision);
    output[5] = settings->magpie.key[0] != 0;
    output[6] = (uint8_t)((settings->web.allow_plain_http ? 1U : 0U) |
        (settings->web.adaptive_font ? 2U : 0U) | (settings->web.layout << 2U));
    output[7] = (uint8_t)settings->web.interval_seconds;
    output[8] = (uint8_t)(settings->web.interval_seconds >> 8U);
    output[9] = (uint8_t)zone_length;
    output[10] = (uint8_t)endpoint_length;
    output[11] = (uint8_t)(endpoint_length >> 8U);
    memcpy(output + 12U, settings->magpie.endpoint, endpoint_length);
    memcpy(output + 12U + endpoint_length, settings->web.timezone, zone_length);
    return length;
}

bool note4_settings_serialize(const note4_settings_t *settings, note4_settings_blob_t *blob) {
    if (blob == NULL || !note4_settings_validate(settings)) return false;
    size_t required = 16U + NOTE4_WIFI_MAX_SLOTS * 3U + 8U + 4U + settings->extension_length +
        strlen(settings->magpie.endpoint) + strlen(settings->magpie.key) + strlen(settings->web.timezone);
    for (size_t i = 0; i < NOTE4_WIFI_MAX_SLOTS; ++i)
        required += settings->wifi.slots[i].ssid_len + settings->wifi.slots[i].password_len;
    if (required > sizeof(blob->bytes)) return false;
    memset(blob, 0, sizeof(*blob));
    uint8_t *p = blob->bytes;
    memcpy(p, "N4S2", 4U);
    write32(p + 8U, settings->revision);
    write32(p + 12U, settings->wifi.generation);
    size_t offset = 16U;
    for (size_t i = 0; i < NOTE4_WIFI_MAX_SLOTS; ++i) {
        const note4_wifi_slot_t *slot = &settings->wifi.slots[i];
        p[offset++] = slot->enabled;
        p[offset++] = slot->ssid_len;
        p[offset++] = slot->password_len;
        memcpy(p + offset, slot->ssid, slot->ssid_len); offset += slot->ssid_len;
        memcpy(p + offset, slot->password, slot->password_len); offset += slot->password_len;
    }
    const size_t endpoint_length = strlen(settings->magpie.endpoint), key_length = strlen(settings->magpie.key);
    const size_t zone_length = strlen(settings->web.timezone);
    p[offset++] = (uint8_t)endpoint_length; p[offset++] = (uint8_t)(endpoint_length >> 8U);
    p[offset++] = (uint8_t)key_length; p[offset++] = (uint8_t)(key_length >> 8U);
    p[offset++] = (uint8_t)((settings->web.allow_plain_http ? 1U : 0U) |
        (settings->web.adaptive_font ? 2U : 0U) | (settings->web.layout << 2U));
    p[offset++] = (uint8_t)settings->web.interval_seconds;
    p[offset++] = (uint8_t)(settings->web.interval_seconds >> 8U);
    p[offset++] = (uint8_t)zone_length;
    memcpy(p + offset, settings->magpie.endpoint, endpoint_length); offset += endpoint_length;
    memcpy(p + offset, settings->magpie.key, key_length); offset += key_length;
    memcpy(p + offset, settings->web.timezone, zone_length); offset += zone_length;
    memcpy(p + offset, settings->extensions, settings->extension_length); offset += settings->extension_length;
    write32(p + 4U, (uint32_t)(offset + 4U));
    write32(p + offset, checksum(p, offset));
    blob->length = offset + 4U;
    return true;
}

bool note4_settings_deserialize(const note4_settings_blob_t *blob, note4_settings_t *settings) {
    if (blob == NULL || settings == NULL || blob->length < 43U || blob->length > sizeof(blob->bytes) ||
        memcmp(blob->bytes, "N4S2", 4U) != 0 || read32(blob->bytes + 4U) != blob->length ||
        read32(blob->bytes + blob->length - 4U) != checksum(blob->bytes, blob->length - 4U)) return false;
    note4_settings_t next;
    note4_settings_clear(&next);
    const uint8_t *p = blob->bytes;
    next.revision = read32(p + 8U);
    next.wifi.generation = read32(p + 12U);
    size_t offset = 16U;
    for (size_t i = 0; i < NOTE4_WIFI_MAX_SLOTS; ++i) {
        if (offset + 3U > blob->length - 4U) return false;
        note4_wifi_slot_t *slot = &next.wifi.slots[i];
        slot->enabled = p[offset++]; slot->ssid_len = p[offset++]; slot->password_len = p[offset++];
        if (slot->enabled > 1U || slot->ssid_len > NOTE4_WIFI_SSID_MAX || slot->password_len > NOTE4_WIFI_PASSWORD_MAX ||
            offset + slot->ssid_len + slot->password_len > blob->length - 4U) return false;
        memcpy(slot->ssid, p + offset, slot->ssid_len); offset += slot->ssid_len;
        memcpy(slot->password, p + offset, slot->password_len); offset += slot->password_len;
        if (slot->enabled) ++next.wifi.slot_count;
    }
    if (offset + 8U > blob->length - 4U) return false;
    const size_t endpoint_length = p[offset] | ((size_t)p[offset + 1U] << 8U);
    const size_t key_length = p[offset + 2U] | ((size_t)p[offset + 3U] << 8U);
    const uint8_t flags = p[offset + 4U];
    const size_t zone_length = p[offset + 7U];
    next.web.allow_plain_http = (flags & 1U) != 0U;
    next.web.adaptive_font = (flags & 2U) != 0U;
    next.web.layout = (flags >> 2U) & 1U;
    next.web.interval_seconds = p[offset + 5U] | ((uint16_t)p[offset + 6U] << 8U);
    offset += 8U;
    if (flags > 7U || endpoint_length > NOTE4_MAGPIE_ENDPOINT_MAX || key_length > NOTE4_MAGPIE_KEY_MAX ||
        zone_length > NOTE4_TIMEZONE_MAX || offset + endpoint_length + key_length + zone_length > blob->length - 4U ||
        memchr(p + offset, 0, endpoint_length + key_length + zone_length) != NULL) return false;
    memcpy(next.magpie.endpoint, p + offset, endpoint_length); offset += endpoint_length;
    memcpy(next.magpie.key, p + offset, key_length); offset += key_length;
    memcpy(next.web.timezone, p + offset, zone_length); offset += zone_length;
    next.extension_length = (uint16_t)(blob->length - 4U - offset);
    memcpy(next.extensions, p + offset, next.extension_length);
    const bool valid = note4_settings_validate(&next);
    if (valid) *settings = next;
    memset(&next, 0, sizeof(next));
    return valid;
}

bool note4_settings_migrate(const note4_wifi_blob_t *legacy, note4_settings_t *settings) {
    if (settings == NULL) return false;
    note4_settings_t next;
    note4_settings_clear(&next);
    if (!note4_wifi_config_deserialize(legacy, &next.wifi)) return false;
    *settings = next;
    memset(&next, 0, sizeof(next));
    return true;
}

bool note4_settings_migrate_combined(const note4_settings_legacy_t *legacy, note4_settings_t *settings) {
    if (legacy == NULL || settings == NULL || legacy->magic != 0x4e345354U || legacy->version != 1U ||
        legacy->payload_size != sizeof(legacy->settings) ||
        legacy->crc32 != checksum((const uint8_t *)legacy, offsetof(note4_settings_legacy_t, crc32))) return false;
    note4_settings_t next;
    note4_settings_clear(&next);
    next.wifi = legacy->settings.wifi;
    if (!note4_settings_validate(&next)) return false;
    *settings = next;
    memset(&next, 0, sizeof(next));
    return true;
}
