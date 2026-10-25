#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quota_order.h"
#include "account_directory.h"
#include "display_prefs.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static size_t calls, fail_at, outstanding;
void *note4_test_malloc(size_t size) { if (++calls == fail_at) return NULL; void *p = malloc(size); if (p) ++outstanding; return p; }
void *note4_test_calloc(size_t count, size_t size) { if (++calls == fail_at) return NULL; void *p = calloc(count, size); if (p) ++outstanding; return p; }
void *note4_test_realloc(void *pointer, size_t size) { if (++calls == fail_at) return NULL; void *p = realloc(pointer, size); if (p && !pointer) ++outstanding; return p; }
void note4_test_free(void *pointer) { if (pointer) { CHECK(outstanding > 0); --outstanding; } free(pointer); }
static void arm(size_t allocation) { calls = 0; fail_at = allocation; }
int main(void) {
    const char *old_json = "[{\"provider\":\"A\",\"windows\":[{\"used\":1}]},{\"provider\":\"B\"}]";
    const char *new_json = "[{\"provider\":\"A\",\"windows\":[{\"used\":1}]},{\"provider\":\"B\",\"balance\":\"$1\"}]";
    note4_quota_snapshot_t *old = NULL;
    CHECK(note4_quota_snapshot_parse(old_json, strlen(old_json), &old) == NOTE4_QUOTA_OK);
    size_t base = outstanding; bool succeeded = false;
    for (size_t allocation = 1; allocation < 1000; ++allocation) {
        arm(allocation); note4_quota_snapshot_t *candidate = old;
        note4_quota_result_t result = note4_quota_snapshot_parse(new_json, strlen(new_json), &candidate);
        if (result == NOTE4_QUOTA_OK) { note4_quota_snapshot_release(candidate); succeeded = true; break; }
        CHECK(result == NOTE4_QUOTA_NO_MEMORY && candidate == old && outstanding == base);
    }
    CHECK(succeeded); arm(0);
    note4_quota_snapshot_t *candidate = NULL;
    CHECK(note4_quota_snapshot_parse(new_json, strlen(new_json), &candidate) == NOTE4_QUOTA_OK);
    note4_quota_order_t order = {0}; CHECK(note4_quota_order_observe(&order, old) == NOTE4_QUOTA_OK);
    base = outstanding; succeeded = false;
    for (size_t allocation = 1; allocation < 10000; ++allocation) {
        arm(allocation); note4_quota_result_t result = note4_quota_order_observe(&order, candidate);
        if (result == NOTE4_QUOTA_OK) { succeeded = true; break; }
        CHECK(result == NOTE4_QUOTA_NO_MEMORY && order.baseline == old && note4_quota_order_index(&order, 0) == 0 && outstanding == base);
    }
    CHECK(succeeded && note4_quota_order_index(&order, 0) == 1); arm(0);
    note4_account_directory_t directory = {0}; uint8_t metadata[33];
    note4_account_directory_update(&directory, old);
    CHECK(note4_account_directory_open(&directory, 0, 0, NULL, 0, metadata) == NOTE4_BLE_OK);
    uint8_t *view = directory.view; const uint32_t token = directory.token;
    note4_account_directory_update(&directory, candidate); base = outstanding;
    for (size_t allocation = 1; allocation <= 2; ++allocation) {
        arm(allocation);
        CHECK(note4_account_directory_open(&directory, 0, 0, NULL, 0, metadata) == NOTE4_BLE_ERR_RESOURCE_EXHAUSTED);
        CHECK(directory.view == view && directory.token == token && outstanding == base);
    }
    arm(0); CHECK(note4_account_directory_open(&directory, 0, 0, NULL, 0, metadata) == NOTE4_BLE_OK);
    note4_account_directory_clear(&directory);
    const uint8_t document[] = {2,1,0,15, 0,0,0,0, 1,0,0,0, 1,0,0,0,'A',
        1,0,0,0, 1,0,0,0,'A', 0,0,0,0, 1,0,0,0, 1,0,0,0,'A', 0,0,0,0};
    note4_display_prefs_t *prefs = NULL; arm(0);
    CHECK(note4_display_prefs_parse(document, sizeof(document), &prefs) == NOTE4_BLE_OK);
    base = outstanding; succeeded = false;
    for (size_t allocation = 1; allocation < 1000; ++allocation) {
        arm(allocation); note4_display_prefs_t *next = prefs;
        note4_ble_status_t result = note4_display_prefs_parse(document, sizeof(document), &next);
        if (result == NOTE4_BLE_OK) { note4_display_prefs_release(next); succeeded = true; break; }
        CHECK(result == NOTE4_BLE_ERR_RESOURCE_EXHAUSTED && next == prefs && outstanding == base);
    }
    CHECK(succeeded); arm(0);
    note4_display_prefs_upload_t upload = {0};
    CHECK(note4_display_prefs_begin(&upload, 0, 20, 0, 0) == NOTE4_BLE_OK);
    uint8_t *staged = upload.bytes; base = outstanding; arm(1);
    CHECK(note4_display_prefs_begin(&upload, 0, 9000, 0, 0) == NOTE4_BLE_ERR_RESOURCE_EXHAUSTED);
    CHECK(upload.bytes == staged && outstanding == base); arm(0);
    note4_display_prefs_upload_clear(&upload); note4_display_prefs_release(prefs);
    note4_quota_order_clear(&order);
    note4_quota_snapshot_release(candidate); note4_quota_snapshot_release(old);
    CHECK(outstanding == 0);
    puts("allocation failures retain complete snapshots, smart baseline and stable directory without leaks"); return 0;
}
