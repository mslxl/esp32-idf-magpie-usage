#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "display_prefs.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void put32(uint8_t *p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (i * 8)); }
int main(void) {
    /* schema/mode/provider mode/kinds/query/providers/hidden tuples/custom tuples */
    uint8_t bytes[32] = {2, 0, 0, 15};
    put32(bytes + 12, 1); put32(bytes + 16, 6); memcpy(bytes + 20, "Codex", 6);
    /* NUL is forbidden in identities, and tuple lengths must be complete. */
    note4_display_prefs_t *prefs = NULL;
    CHECK(note4_display_prefs_parse(bytes, 32, &prefs) == NOTE4_BLE_ERR_VALIDATION && prefs == NULL);
    const uint8_t hidden[] = {2,0,0,15, 0,0,0,0, 0,0,0,0, 1,0,0,0,
        5,0,0,0,'C','o','d','e','x', 0,0,0,0, 0,0,0,0};
    CHECK(note4_display_prefs_parse(hidden, sizeof(hidden), &prefs) == NOTE4_BLE_OK);
    CHECK(note4_display_prefs_hidden(prefs, "Codex", ""));
    CHECK(!note4_display_prefs_hidden(prefs, "codex", ""));
    CHECK(!note4_display_prefs_hidden(prefs, "Codex", "other"));
    note4_display_prefs_upload_t upload = {0};
    const uint32_t crc = note4_account_directory_crc32(hidden, sizeof(hidden));
    CHECK(note4_display_prefs_begin(&upload, 7, sizeof(hidden), crc, 100) == NOTE4_BLE_OK);
    CHECK(note4_display_prefs_put(&upload, 0, hidden, 17, 200) == NOTE4_BLE_OK);
    CHECK(note4_display_prefs_put(&upload, 0, hidden, 17, 300) == NOTE4_BLE_OK);
    uint8_t conflict[17]; memcpy(conflict, hidden, 17); conflict[0] ^= 1;
    CHECK(note4_display_prefs_put(&upload, 0, conflict, 17, 400) == NOTE4_BLE_ERR_SEQUENCE);
    CHECK(note4_display_prefs_put(&upload, 18, hidden, 1, 400) == NOTE4_BLE_ERR_SEQUENCE);
    CHECK(note4_display_prefs_put(&upload, 17, hidden + 17, sizeof(hidden) - 17, 500) == NOTE4_BLE_OK);
    note4_display_prefs_t *staged = NULL;
    CHECK(note4_display_prefs_finish(&upload, 8, 600U, &staged) == NOTE4_BLE_ERR_STALE_REVISION && staged == NULL);
    CHECK(note4_display_prefs_finish(&upload, 7, 600U, &staged) == NOTE4_BLE_OK);
    CHECK(note4_display_prefs_finish(&upload, 7, 120500U, &staged) == NOTE4_BLE_ERR_STATE);
    CHECK(upload.bytes == NULL);
    CHECK(note4_display_prefs_hidden(staged, "Codex", ""));
    CHECK(note4_display_prefs_hidden(prefs, "Codex", ""));
    note4_display_prefs_release(staged); note4_display_prefs_release(prefs);
    note4_display_prefs_upload_clear(&upload);
    const uint8_t filtered[] = {2,1,1,9, 7,0,0,0,'s','t','r','a','s','s','e',
        1,0,0,0, 5,0,0,0,'C','o','d','e','x', 0,0,0,0, 0,0,0,0};
    prefs = NULL; CHECK(note4_display_prefs_parse(filtered, sizeof(filtered), &prefs) == NOTE4_BLE_OK);
    note4_quota_snapshot_t *snapshot = NULL;
    const char *records = "[{\"provider\":\"Codex\",\"user\":\"Straße\",\"kind\":\"subscription\"},"
        "{\"provider\":\"Codex\",\"user\":\"STRASSE\",\"kind\":\"balance\"},"
        "{\"provider\":\"codex\",\"user\":\"Straße\"},"
        "{\"provider\":\"Codex\",\"name\":\"Straße\",\"kind\":\"future-kind\"}]";
    CHECK(note4_quota_snapshot_parse(records, strlen(records), &snapshot) == NOTE4_QUOTA_OK);
    CHECK(note4_display_prefs_visible(prefs, snapshot, 0));
    CHECK(!note4_display_prefs_visible(prefs, snapshot, 1));
    CHECK(!note4_display_prefs_visible(prefs, snapshot, 2));
    CHECK(note4_display_prefs_visible(prefs, snapshot, 3));
    note4_quota_snapshot_release(snapshot); note4_display_prefs_release(prefs);
    const uint8_t bounded[] = {2,0,0,32, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
    prefs = NULL; CHECK(note4_display_prefs_parse(bounded, sizeof(bounded), &prefs) == NOTE4_BLE_ERR_VALIDATION && prefs == NULL);
    const uint8_t hide_empty[] = {2,0,0,15 | NOTE4_DISPLAY_HIDE_EMPTY, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
    CHECK(note4_display_prefs_parse(hide_empty, sizeof(hide_empty), &prefs) == NOTE4_BLE_OK);
    const char *mixed = "[{\"provider\":\"Codex\",\"user\":\"empty\"},"
        "{\"provider\":\"Codex\",\"user\":\"window\",\"windows\":[{\"name\":\"5h\",\"used\":10}]},"
        "{\"provider\":\"Codex\",\"user\":\"money\",\"balance\":\"$5\"},"
        "{\"provider\":\"Codex\",\"user\":\"broken\",\"error\":\"timeout\"}]";
    CHECK(note4_quota_snapshot_parse(mixed, strlen(mixed), &snapshot) == NOTE4_QUOTA_OK);
    CHECK(!note4_display_prefs_visible(prefs, snapshot, 0));
    CHECK(note4_display_prefs_visible(prefs, snapshot, 1));
    CHECK(note4_display_prefs_visible(prefs, snapshot, 2));
    CHECK(note4_display_prefs_visible(prefs, snapshot, 3));
    note4_quota_snapshot_release(snapshot); note4_display_prefs_release(prefs);
    const char *queries[] ={" strasse ", "\tSTRASSE\r\n", "\xc2\xa0strasse\xe3\x80\x80", "\xef\xbb\xbfSTRASSE\xe2\x80\xaf"};
    for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); ++i) {
        uint8_t document[128] = {2,0,0,15};
        const size_t length = strlen(queries[i]); put32(document + 4, (uint32_t)length);
        memcpy(document + 8, queries[i], length);
        prefs = NULL; snapshot = NULL;
        CHECK(note4_display_prefs_parse(document, 20U + length, &prefs) == NOTE4_BLE_OK);
        const char *record = "[{\"provider\":\"Codex\",\"user\":\"Straße\"}]";
        CHECK(note4_quota_snapshot_parse(record, strlen(record), &snapshot) == NOTE4_QUOTA_OK);
        CHECK(note4_display_prefs_visible(prefs, snapshot, 0));
        note4_quota_snapshot_release(snapshot); note4_display_prefs_release(prefs);
    }
    puts("display preferences exact identities and staged chunks passed"); return 0;
}
