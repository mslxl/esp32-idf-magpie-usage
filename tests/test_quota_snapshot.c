#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quota_snapshot.h"
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "failed %d: %s\n", __LINE__, #value); exit(1); } } while (0)
int main(void) {
    const char *json = "[{\"provider\":\"Codex\",\"user\":\"A/B\\nC\",\"name\":\"Account\",\"windows\":[{\"name\":\"5h\",\"used\":0},{\"name\":\"5h\",\"used\":110}]},{\"provider\":\"Claude\",\"kind\":\"future-kind\",\"balance\":\"¥12.30\",\"windows\":[]}]";
    note4_quota_snapshot_t *snapshot = NULL;
    CHECK(note4_quota_snapshot_parse(json, strlen(json), &snapshot) == NOTE4_QUOTA_OK);
    CHECK(note4_quota_count(snapshot) == 2U);
    CHECK(strcmp(note4_quota_text(snapshot, 0U, "provider"), "Codex") == 0);
    CHECK(strcmp(note4_quota_text(snapshot, 0U, "user"), "A/B\nC") == 0);
    CHECK(note4_quota_window_count(snapshot, 0U) == 2U);
    CHECK(strcmp(note4_quota_window_text(snapshot, 0U, 0U, "used"), "0") == 0);
    CHECK(strcmp(note4_quota_window_text(snapshot, 0U, 1U, "used"), "110") == 0);
    CHECK(strcmp(note4_quota_text(snapshot, 1U, "kind"), "future-kind") == 0);
    const char *bad[] = {"null", "{}", "[", "[{\"provider\":\"\"}]", "[{\"provider\":\"x\",\"windows\":[{\"used\":\"3\"}]}]", "[{\"provider\":\"x\"}]junk"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        note4_quota_snapshot_t *candidate = snapshot;
        CHECK(note4_quota_snapshot_parse(bad[i], strlen(bad[i]), &candidate) != NOTE4_QUOTA_OK);
        CHECK(candidate == snapshot && note4_quota_count(snapshot) == 2U);
    }
    note4_quota_snapshot_release(snapshot);
    CHECK(note4_quota_snapshot_parse("[]", 2U, &snapshot) == NOTE4_QUOTA_OK);
    CHECK(note4_quota_count(snapshot) == 0U);
    note4_quota_snapshot_release(snapshot);
    const char *invalid_split = "[{\"provider\":\"x\",\"balanceSplit\":[{\"text\":12}]}]";
    snapshot = NULL;
    CHECK(note4_quota_snapshot_parse(invalid_split, strlen(invalid_split), &snapshot) == NOTE4_QUOTA_INVALID);
    return 0;
}
