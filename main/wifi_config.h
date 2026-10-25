#ifndef NOTE4_WIFI_CONFIG_H
#define NOTE4_WIFI_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOTE4_WIFI_MAX_SLOTS 5U
#define NOTE4_WIFI_SSID_MAX 32U
#define NOTE4_WIFI_PASSWORD_MAX 64U
#define NOTE4_WIFI_CONFIG_VERSION 1U
#define NOTE4_WIFI_CONFIG_MAGIC 0x4E345743UL

typedef struct {
    uint8_t enabled;
    uint8_t ssid_len;
    uint8_t password_len;
    char ssid[NOTE4_WIFI_SSID_MAX + 1U];
    char password[NOTE4_WIFI_PASSWORD_MAX + 1U];
} note4_wifi_slot_t;

typedef struct {
    uint16_t version;
    uint16_t slot_count;
    uint32_t generation;
    note4_wifi_slot_t slots[NOTE4_WIFI_MAX_SLOTS];
} note4_wifi_config_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t generation;
    note4_wifi_config_t config;
    uint32_t crc32;
} note4_wifi_blob_t;

void note4_wifi_config_clear(note4_wifi_config_t *config);
bool note4_wifi_slot_set(note4_wifi_config_t *config, size_t index,
                         const char *ssid, const char *password,
                         bool enabled);
bool note4_wifi_config_validate(const note4_wifi_config_t *config);
size_t note4_wifi_config_enabled_mask(const note4_wifi_config_t *config);
bool note4_wifi_config_serialize(const note4_wifi_config_t *config,
                                 uint32_t generation, note4_wifi_blob_t *blob);
bool note4_wifi_config_deserialize(const note4_wifi_blob_t *blob,
                                   note4_wifi_config_t *config);

#endif
