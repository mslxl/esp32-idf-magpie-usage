#include "account_directory.h"
#include "resource_budget.h"
#include <stdlib.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0U; i < 4U; ++i) p[i] = (uint8_t)(value >> (i * 8U));
}
uint32_t note4_account_directory_crc32(const uint8_t *bytes, size_t length) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0U; bit < 8U; ++bit) crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
void note4_account_directory_close(note4_account_directory_t *directory) {
    free(directory->view); directory->view = NULL; directory->length = 0U;
}
void note4_account_directory_expire(note4_account_directory_t *directory, uint64_t now) {
    if (directory->view != NULL && now >= directory->active_at && now - directory->active_at >= 120000U)
        note4_account_directory_close(directory);
}
void note4_account_directory_clear(note4_account_directory_t *directory) {
    note4_account_directory_close(directory);
    note4_quota_snapshot_release(directory->latest); directory->latest = NULL;
    ++directory->generation; ++directory->instance_revision;
}
void note4_account_directory_update(note4_account_directory_t *directory, note4_quota_snapshot_t *snapshot) {
    note4_quota_snapshot_retain(snapshot);
    note4_quota_snapshot_release(directory->latest); directory->latest = snapshot;
    ++directory->generation;
}
static bool same_group(const note4_quota_snapshot_t *snapshot, size_t a, size_t b) {
    return !strcmp(note4_quota_text(snapshot, a, "provider"), note4_quota_text(snapshot, b, "provider")) &&
        !strcmp(note4_quota_text(snapshot, a, "user"), note4_quota_text(snapshot, b, "user"));
}
note4_ble_status_t note4_account_directory_open(note4_account_directory_t *directory,
    uint32_t settings_revision, uint64_t now, const uint8_t *prefs, size_t prefs_length, uint8_t metadata[33]) {
    if (directory->latest == NULL) return NOTE4_BLE_ERR_NO_DIRECTORY;
    const size_t count = note4_quota_count(directory->latest);
    if (count > UINT32_MAX || count > SIZE_MAX / sizeof(uint32_t) || directory->token == UINT32_MAX)
        return NOTE4_BLE_ERR_RESOURCE_EXHAUSTED;
    static const char *const fields[] = {"provider", "user", "name", "alias", "seat", "from", "kind", "plan"};
    size_t size = 5U;
    for (size_t c = 0U; c < count; ++c) {
        if (size > UINT32_MAX - 4U) return NOTE4_BLE_ERR_RESOURCE_EXHAUSTED;
        size += 4U;
        for (size_t f = 0U; f < 8U; ++f) {
            const size_t length = strlen(note4_quota_text(directory->latest, c, fields[f]));
            if (length > UINT32_MAX - 4U || size > UINT32_MAX - 4U - length) return NOTE4_BLE_ERR_RESOURCE_EXHAUSTED;
            size += 4U + length;
        }
    }
    if ((prefs_length && prefs == NULL) || prefs_length > UINT32_MAX - 4U || size > UINT32_MAX - 4U - prefs_length)
        return NOTE4_BLE_ERR_RESOURCE_EXHAUSTED;
    size += 4U + prefs_length;
    if (size > NOTE4_DOCUMENT_BUDGET || count * sizeof(uint32_t) > SIZE_MAX - size ||
        !note4_resource_available(size + count * sizeof(uint32_t))) return NOTE4_BLE_ERR_RESOURCE_EXHAUSTED;
    uint8_t *candidate = malloc(size);
    uint32_t *groups = count ? malloc(count * sizeof(*groups)) : NULL;
    if (candidate == NULL || (count && groups == NULL)) { free(candidate); free(groups); return NOTE4_BLE_ERR_RESOURCE_EXHAUSTED; }
    candidate[0] = NOTE4_DIRECTORY_SCHEMA;
    put32(candidate + 1U, (uint32_t)count);
    uint32_t group_count = 0U;
    size_t offset = 5U;
    for (size_t c = 0U; c < count; ++c) {
        size_t previous = 0U;
        while (previous < c && !same_group(directory->latest, previous, c)) ++previous;
        groups[c] = previous < c ? groups[previous] : group_count++;
        put32(candidate + offset, groups[c]); offset += 4U;
        for (size_t f = 0U; f < 8U; ++f) {
            const char *text = note4_quota_text(directory->latest, c, fields[f]);
            const size_t length = strlen(text);
            put32(candidate + offset, (uint32_t)length); offset += 4U;
            memcpy(candidate + offset, text, length); offset += length;
        }
    }
    put32(candidate + offset, (uint32_t)prefs_length); offset += 4U;
    if (prefs_length) memcpy(candidate + offset, prefs, prefs_length);
    free(groups);
    note4_account_directory_close(directory);
    directory->view = candidate; directory->length = (uint32_t)size;
    directory->view_generation = directory->generation; directory->settings_revision = settings_revision;
    directory->groups = group_count; directory->cards = (uint32_t)count; directory->active_at = now; ++directory->token;
    metadata[0] = NOTE4_DIRECTORY_SCHEMA;
    put32(metadata + 1U, directory->token); put32(metadata + 5U, directory->view_generation);
    put32(metadata + 9U, settings_revision); put32(metadata + 13U, directory->instance_revision);
    put32(metadata + 17U, directory->length); put32(metadata + 21U, note4_account_directory_crc32(candidate, size));
    put32(metadata + 25U, group_count); put32(metadata + 29U, (uint32_t)count);
    return NOTE4_BLE_OK;
}
note4_ble_status_t note4_account_directory_read(note4_account_directory_t *directory,
    uint32_t token, uint32_t offset, uint32_t count, uint64_t now,
    uint8_t *output, size_t capacity, size_t *length) {
    note4_account_directory_expire(directory, now);
    if (directory->view == NULL || token != directory->token) return NOTE4_BLE_ERR_STALE_REVISION;
    if (count == 0U || offset > directory->length || count > directory->length - offset ||
        capacity < 9U || count > capacity - 9U) return NOTE4_BLE_ERR_LENGTH;
    output[0] = NOTE4_DIRECTORY_SCHEMA; put32(output + 1U, token); put32(output + 5U, offset);
    memcpy(output + 9U, directory->view + offset, count); *length = (size_t)count + 9U;
    directory->active_at = now;
    return NOTE4_BLE_OK;
}
