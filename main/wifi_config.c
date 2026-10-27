#include "wifi_config.h"

#include <string.h>

#include "note4_bytes.h"

static bool bounded_text(const char *text, size_t max, uint8_t *length) {
    if (text == NULL || length == NULL) return false;
    size_t size = 0U;
    while (size <= max && text[size] != '\0') ++size;
    if (size > max) return false;
    *length = (uint8_t)size;
    return true;
}

void note4_wifi_config_clear(note4_wifi_config_t *config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->version = NOTE4_WIFI_CONFIG_VERSION;
}

bool note4_wifi_slot_set(note4_wifi_config_t *config, size_t index,
                         const char *ssid, const char *password,
                         bool enabled) {
    if (config == NULL || index >= NOTE4_WIFI_MAX_SLOTS || ssid == NULL ||
        password == NULL) return false;
    uint8_t ssid_len = 0U;
    uint8_t password_len = 0U;
    if (!bounded_text(ssid, NOTE4_WIFI_SSID_MAX, &ssid_len) ||
        !bounded_text(password, NOTE4_WIFI_PASSWORD_MAX, &password_len)) return false;
    note4_wifi_slot_t *slot = &config->slots[index];
    memset(slot, 0, sizeof(*slot));
    slot->enabled = enabled ? 1U : 0U;
    slot->ssid_len = ssid_len;
    slot->password_len = password_len;
    memcpy(slot->ssid, ssid, ssid_len);
    memcpy(slot->password, password, password_len);
    config->slot_count = 0U;
    for (size_t item = 0; item < NOTE4_WIFI_MAX_SLOTS; ++item) {
        if (config->slots[item].enabled) ++config->slot_count;
    }
    return true;
}

bool note4_wifi_config_validate(const note4_wifi_config_t *config) {
    if (config == NULL || config->version != NOTE4_WIFI_CONFIG_VERSION ||
        config->slot_count == 0U || config->slot_count > NOTE4_WIFI_MAX_SLOTS) return false;
    uint16_t count = 0U;
    for (size_t index = 0; index < NOTE4_WIFI_MAX_SLOTS; ++index) {
        const note4_wifi_slot_t *slot = &config->slots[index];
        if (!slot->enabled) {
            if (slot->ssid_len != 0U || slot->password_len != 0U) return false;
            continue;
        }
        if (slot->ssid_len == 0U || slot->ssid_len > NOTE4_WIFI_SSID_MAX ||
            slot->password_len > NOTE4_WIFI_PASSWORD_MAX ||
            slot->ssid[slot->ssid_len] != '\0' ||
            slot->password[slot->password_len] != '\0') return false;
        ++count;
        for (size_t other = 0; other < index; ++other) {
            if (config->slots[other].enabled &&
                config->slots[other].ssid_len == slot->ssid_len &&
                memcmp(config->slots[other].ssid, slot->ssid, slot->ssid_len) == 0) return false;
        }
    }
    return count == config->slot_count;
}

size_t note4_wifi_config_enabled_mask(const note4_wifi_config_t *config) {
    if (config == NULL) return 0U;
    size_t mask = 0U;
    for (size_t index = 0; index < NOTE4_WIFI_MAX_SLOTS; ++index) {
        if (config->slots[index].enabled) mask |= (size_t)1U << index;
    }
    return mask;
}

bool note4_wifi_config_serialize(const note4_wifi_config_t *config,
                                 uint32_t generation, note4_wifi_blob_t *blob) {
    if (config == NULL || blob == NULL || !note4_wifi_config_validate(config)) return false;
    memset(blob, 0, sizeof(*blob));
    blob->magic = NOTE4_WIFI_CONFIG_MAGIC;
    blob->version = NOTE4_WIFI_CONFIG_VERSION;
    blob->payload_size = (uint16_t)sizeof(blob->config);
    blob->generation = generation;
    blob->config = *config;
    blob->config.generation = generation;
    blob->crc32 = note4_crc32((const uint8_t *)blob,
                              offsetof(note4_wifi_blob_t, crc32));
    return true;
}

bool note4_wifi_config_deserialize(const note4_wifi_blob_t *blob,
                                   note4_wifi_config_t *config) {
    if (blob == NULL || config == NULL || blob->magic != NOTE4_WIFI_CONFIG_MAGIC ||
        blob->version != NOTE4_WIFI_CONFIG_VERSION ||
        blob->payload_size != sizeof(blob->config) ||
        note4_crc32((const uint8_t *)blob, offsetof(note4_wifi_blob_t, crc32)) != blob->crc32 ||
        !note4_wifi_config_validate(&blob->config)) return false;
    *config = blob->config;
    return true;
}
