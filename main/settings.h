#ifndef NOTE4_SETTINGS_H
#define NOTE4_SETTINGS_H

#include "magpie_config.h"
#include "wifi_config.h"

#define NOTE4_WEB_SCHEMA 2U
#define NOTE4_TIMEZONE_MAX 63U
#define NOTE4_SETTINGS_MAX_BYTES 1536U
/* Web flags: bit0 allow_plain_http, bit1 adaptive_font, bit2 layout, bit3 language. */
#define NOTE4_WEB_FLAGS_MAX 15U
#define NOTE4_LANGUAGE_ENGLISH 0U
#define NOTE4_LANGUAGE_CHINESE 1U

typedef struct {
    bool allow_plain_http;
    bool adaptive_font;
    uint8_t layout;
    uint8_t language;
    uint16_t interval_seconds;
    char timezone[NOTE4_TIMEZONE_MAX + 1U];
} note4_web_settings_t;

typedef struct {
    note4_wifi_config_t wifi;
    note4_magpie_config_t magpie;
    note4_web_settings_t web;
    uint32_t revision;
    uint16_t extension_length;
    uint8_t extensions[NOTE4_SETTINGS_MAX_BYTES];
} note4_settings_t;

typedef struct {
    uint8_t bytes[NOTE4_SETTINGS_MAX_BYTES];
    size_t length;
} note4_settings_blob_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    struct {
        note4_wifi_config_t wifi;
        note4_magpie_config_t magpie;
    } settings;
    uint32_t crc32;
} note4_settings_legacy_t;

typedef enum {
    NOTE4_SETTINGS_OK,
    NOTE4_SETTINGS_INVALID,
    NOTE4_SETTINGS_UNSUPPORTED_SCHEMA,
    NOTE4_SETTINGS_STALE_REVISION,
} note4_settings_result_t;

void note4_settings_clear(note4_settings_t *settings);
bool note4_settings_validate(const note4_settings_t *settings);
bool note4_settings_timezone_supported(const char *timezone);
bool note4_settings_serialize(const note4_settings_t *settings, note4_settings_blob_t *blob);
bool note4_settings_deserialize(const note4_settings_blob_t *blob, note4_settings_t *settings);
bool note4_settings_migrate(const note4_wifi_blob_t *legacy, note4_settings_t *settings);
bool note4_settings_migrate_combined(const note4_settings_legacy_t *legacy, note4_settings_t *settings);
note4_settings_result_t note4_settings_apply_web(note4_settings_t *target,
    const note4_settings_t *saved, const uint8_t *payload, size_t length);
size_t note4_settings_web_metadata(const note4_settings_t *settings, uint8_t *output, size_t capacity);

#endif
