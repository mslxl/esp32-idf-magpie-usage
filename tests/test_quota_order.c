#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quota_order.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static note4_quota_snapshot_t *parse(const char *json) { note4_quota_snapshot_t *s = NULL; CHECK(note4_quota_snapshot_parse(json, strlen(json), &s) == NOTE4_QUOTA_OK); return s; }
static void observe(note4_quota_order_t *order, const char *json, const char *providers) {
    note4_quota_snapshot_t *snapshot = parse(json);
    CHECK(note4_quota_order_observe(order, snapshot) == NOTE4_QUOTA_OK);
    CHECK(note4_quota_count(snapshot) == strlen(providers));
    for (size_t i = 0; providers[i]; ++i) {
        const char actual = note4_quota_text(snapshot, note4_quota_order_index(order, i), "provider")[0];
        if (actual != providers[i]) fprintf(stderr, "expected %s at %zu, got %c for %s\n", providers, i, actual, json);
        CHECK(actual == providers[i]);
    }
    note4_quota_snapshot_release(snapshot);
}
int main(void) {
    note4_quota_order_t order = {0};
    observe(&order, "[{\"provider\":\"A\",\"windows\":[{\"used\":1},{\"used\":0}]},{\"provider\":\"B\",\"windows\":[{\"used\":2}]}]", "AB");
    observe(&order, "[{\"provider\":\"A\",\"windows\":[{\"used\":1.0},{\"used\":-0}]},{\"provider\":\"B\",\"windows\":[{\"used\":2.0000000000000001}]}]", "BA");
    observe(&order, "[{\"provider\":\"A\",\"name\":\"renamed\",\"readAt\":\"2026-01-01T00:00:00Z\",\"windows\":[{\"used\":-0.0,\"resetSecs\":5},{\"used\":1e0}]},{\"provider\":\"B\",\"windows\":[{\"used\":2.00000000000000010}]}]", "BA");
    observe(&order, "[{\"provider\":\"A\",\"until\":\"2026-01-01T00:00:00Z\"},{\"provider\":\"B\"},{\"provider\":\"C\"}]", "BAC");
    observe(&order, "[{\"provider\":\"A\",\"until\":\"2025-12-31T19:00:00-05:00\"},{\"provider\":\"B\",\"error\":\"request 123\"},{\"provider\":\"C\"}]", "BAC");
    observe(&order, "[{\"provider\":\"A\",\"until\":\"2026-01-01T00:00:00.000Z\"},{\"provider\":\"B\",\"error\":\"request 456\"},{\"provider\":\"C\"}]", "BAC");
    observe(&order, "[{\"provider\":\"A\",\"until\":\"2026-01-01T00:00:00Z\"},{\"provider\":\"C\"}]", "AC");
    observe(&order, "[{\"provider\":\"A\",\"until\":\"2026-01-01T00:00:00Z\"},{\"provider\":\"B\"},{\"provider\":\"C\"}]", "BAC");
    observe(&order, "[]", ""); note4_quota_order_clear(&order);
    observe(&order, "[{\"provider\":\"A\",\"windows\":[{\"used\":1},{\"used\":1}]},{\"provider\":\"B\"},{\"provider\":\"A\",\"balance\":\"$2\"}]", "ABA");
    observe(&order, "[{\"provider\":\"B\"},{\"provider\":\"A\",\"balance\":\"$2\"},{\"provider\":\"A\",\"windows\":[{\"used\":1}]}]", "AAB");
    observe(&order, "[{\"provider\":\"A\",\"windows\":[{\"used\":1.00}]},{\"provider\":\"B\"},{\"provider\":\"A\",\"balance\":\"$2\"}]", "AAB");
    note4_quota_order_clear(&order);
    observe(&order, "[{\"provider\":\"A\",\"windows\":[{\"used\":1e999999999999999999999}]},{\"provider\":\"B\"}]", "AB");
    observe(&order, "[{\"provider\":\"A\",\"windows\":[{\"used\":10e999999999999999999998}]},{\"provider\":\"B\",\"balance\":\"$1\"}]", "BA");
    observe(&order, "[{\"provider\":\"A\",\"windows\":[{\"used\":100e999999999999999999997}]},{\"provider\":\"B\",\"balance\":\"$1\"}]", "BA");
    note4_quota_order_clear(&order);
    puts("smart quota order exact semantics and stable partition passed"); return 0;
}
