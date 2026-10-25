#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "account_directory.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint32_t u32(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U); }
int main(void) {
    note4_account_directory_t directory = {0};
    uint8_t metadata[33];
    CHECK(note4_account_directory_open(&directory, 7U, 100U, NULL, 0U, metadata) == NOTE4_BLE_ERR_NO_DIRECTORY);
    char json[8192]; size_t length = 0U;
    json[length++] = '[';
    for (unsigned i = 0; i < 30U; ++i) {
        const char *record = "{\"provider\":\"厂商\",\"user\":\"same\",\"name\":\"visible account\",\"error\":\"secret error body\",\"credentials\":\"secret key\"}";
        if (i) json[length++] = ',';
        memcpy(json + length, record, strlen(record)); length += strlen(record);
    }
    json[length++] = ']';
    note4_quota_snapshot_t *snapshot = NULL;
    CHECK(note4_quota_snapshot_parse(json, length, &snapshot) == NOTE4_QUOTA_OK);
    note4_account_directory_update(&directory, snapshot);
    note4_quota_snapshot_release(snapshot);
    CHECK(note4_account_directory_open(&directory, 7U, 100U, NULL, 0U, metadata) == NOTE4_BLE_OK);
    CHECK(metadata[0] == 2U && u32(metadata + 9U) == 7U);
    CHECK(u32(metadata + 25U) == 1U && u32(metadata + 29U) == 30U);
    const uint32_t token = u32(metadata + 1U), total = u32(metadata + 17U);
    CHECK(total > 1024U);
    uint8_t *document = calloc((size_t)total + 1U, 1U); CHECK(document != NULL);
    /* A newer complete observation must not enter the already opened view. */
    snapshot = NULL; CHECK(note4_quota_snapshot_parse("[]", 2U, &snapshot) == NOTE4_QUOTA_OK);
    note4_account_directory_update(&directory, snapshot); note4_quota_snapshot_release(snapshot);
    for (uint32_t offset = 0U; offset < total;) {
        uint8_t chunk[26]; size_t size = 0U;
        CHECK(note4_account_directory_read(&directory, token, offset, total - offset < 17U ? total - offset : 17U, 101U, chunk, sizeof(chunk), &size) == NOTE4_BLE_OK);
        CHECK(chunk[0] == 2U && u32(chunk + 1U) == token && u32(chunk + 5U) == offset && size > 9U);
        memcpy(document + offset, chunk + 9U, size - 9U); offset += (uint32_t)(size - 9U);
    }
    CHECK(u32(document + 1U) == 30U && u32(document + 5U) == 0U);
    /* Binary strings use lengths; examine the whole stream, including embedded lengths. */
    bool seen_provider = false;
    for (uint32_t i = 0U; i + 6U <= total; ++i) {
        if (!memcmp(document + i, "厂商", 6U)) seen_provider = true;
        CHECK(i + 6U > total || memcmp(document + i, "secret", 6U) != 0);
    }
    CHECK(seen_provider && note4_account_directory_crc32(document, total) == u32(metadata + 21U));
    free(document);
    CHECK(note4_account_directory_open(&directory, 7U, 200U, NULL, 0U, metadata) == NOTE4_BLE_OK);
    CHECK(u32(metadata + 25U) == 0U && u32(metadata + 29U) == 0U && u32(metadata + 17U) == 9U);
    uint8_t chunk[32]; size_t size;
    CHECK(note4_account_directory_read(&directory, token, 0U, 1U, 201U, chunk, sizeof(chunk), &size) == NOTE4_BLE_ERR_STALE_REVISION);
    const uint32_t empty_token = u32(metadata + 1U);
    CHECK(note4_account_directory_read(&directory, empty_token, 0U, 1U, 120200U, chunk, sizeof(chunk), &size) == NOTE4_BLE_ERR_STALE_REVISION);
    note4_account_directory_clear(&directory);
    CHECK(note4_account_directory_open(&directory, 8U, 120201U, NULL, 0U, metadata) == NOTE4_BLE_ERR_NO_DIRECTORY);
    puts("account directory stable chunk transfer passed");
    return 0;
}
