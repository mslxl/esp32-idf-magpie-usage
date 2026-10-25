#ifndef NOTE4_ACCOUNT_DIRECTORY_H
#define NOTE4_ACCOUNT_DIRECTORY_H
#include "ble_protocol.h"
#include "quota_snapshot.h"
#define NOTE4_DIRECTORY_SCHEMA 2U

typedef struct {
    note4_quota_snapshot_t *latest;
    uint8_t *view;
    uint32_t length, token, generation, view_generation, settings_revision, instance_revision, groups, cards;
    uint64_t active_at;
} note4_account_directory_t;
/* Coordinator-owned. An open byte view remains stable across quota updates. */
void note4_account_directory_update(note4_account_directory_t *directory, note4_quota_snapshot_t *snapshot);
void note4_account_directory_close(note4_account_directory_t *directory);
void note4_account_directory_expire(note4_account_directory_t *directory, uint64_t now);
void note4_account_directory_clear(note4_account_directory_t *directory);
note4_ble_status_t note4_account_directory_open(note4_account_directory_t *directory,
    uint32_t settings_revision, uint64_t now, const uint8_t *prefs, size_t prefs_length, uint8_t metadata[33]);
note4_ble_status_t note4_account_directory_read(note4_account_directory_t *directory,
    uint32_t token, uint32_t offset, uint32_t count, uint64_t now,
    uint8_t *output, size_t capacity, size_t *length);
uint32_t note4_account_directory_crc32(const uint8_t *bytes, size_t length);
#endif
