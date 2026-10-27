#ifndef NOTE4_DISPLAY_PREFS_H
#define NOTE4_DISPLAY_PREFS_H
#include "account_directory.h"
#include <stdatomic.h>
typedef struct { const char *provider, *user; } note4_account_identity_t;
/* `kinds` bits 0-3 select card kinds; this bit hides cards without windows, balances or an error. */
#define NOTE4_DISPLAY_HIDE_EMPTY 16U
typedef struct note4_display_prefs {
    atomic_uint references;
    uint8_t *bytes;
    char *text;
    uint32_t length;
    uint8_t order, provider_mode, kinds;
    const char *query;
    const char **providers;
    note4_account_identity_t *hidden, *custom;
    uint32_t provider_count, hidden_count, custom_count;
} note4_display_prefs_t;
typedef struct {
    uint8_t *bytes;
    uint32_t length, received, crc, revision;
    uint64_t active_at;
} note4_display_prefs_upload_t;
/* Parsed documents are immutable. Reference ownership stays on the coordinator. */
note4_ble_status_t note4_display_prefs_parse(const uint8_t *bytes, size_t length, note4_display_prefs_t **output);
void note4_display_prefs_retain(note4_display_prefs_t *prefs);
void note4_display_prefs_release(note4_display_prefs_t *prefs);
bool note4_display_prefs_hidden(const note4_display_prefs_t *prefs, const char *provider, const char *user);
bool note4_display_prefs_visible(const note4_display_prefs_t *prefs, const note4_quota_snapshot_t *snapshot, size_t card);
size_t note4_display_prefs_rank(const note4_display_prefs_t *prefs, const char *provider, const char *user);
note4_ble_status_t note4_display_prefs_begin(note4_display_prefs_upload_t *upload,
    uint32_t revision, uint32_t length, uint32_t crc, uint64_t now);
note4_ble_status_t note4_display_prefs_put(note4_display_prefs_upload_t *upload,
    uint32_t offset, const uint8_t *bytes, size_t length, uint64_t now);
note4_ble_status_t note4_display_prefs_finish(note4_display_prefs_upload_t *upload,
    uint32_t revision, uint64_t now, note4_display_prefs_t **output);
void note4_display_prefs_upload_clear(note4_display_prefs_upload_t *upload);
#endif
