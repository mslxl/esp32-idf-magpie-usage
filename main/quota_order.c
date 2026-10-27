#include "quota_order.h"
#include "resource_budget.h"
#include "external_memory.h"
#include <stdlib.h>
#include <string.h>

static bool identity(const note4_quota_snapshot_t *a, size_t ai, const note4_quota_snapshot_t *b, size_t bi) {
    return !strcmp(note4_quota_text(a, ai, "provider"), note4_quota_text(b, bi, "provider")) &&
        !strcmp(note4_quota_text(a, ai, "user"), note4_quota_text(b, bi, "user"));
}

note4_quota_result_t note4_quota_order_prepare(const note4_quota_order_t *order, note4_quota_snapshot_t *snapshot,
    note4_quota_order_t *candidate) {
    if (!order || !snapshot || !candidate || candidate == order) return NOTE4_QUOTA_INVALID;
    const size_t count = note4_quota_count(snapshot), previous_count = note4_quota_count(order->baseline);
    if (count > SIZE_MAX / (sizeof(size_t) * 3U + sizeof(bool)) ||
        !note4_resource_available(count * (sizeof(size_t) * 3U + sizeof(bool)))) return NOTE4_QUOTA_NO_MEMORY;
    size_t *indices = count ? note4_external_malloc(count * sizeof(*indices)) : NULL;
    size_t *matched = count ? note4_external_malloc(count * sizeof(*matched)) : NULL;
    bool *changed = count ? note4_external_calloc(count, sizeof(*changed)) : NULL;
    if (count && (!indices || !matched || !changed)) { free(indices); free(matched); free(changed); return NOTE4_QUOTA_NO_MEMORY; }
    for (size_t i = 0; i < count; ++i) matched[i] = SIZE_MAX;
    note4_quota_result_t result = NOTE4_QUOTA_OK;
    /* Match unchanged duplicate records first, then changed records of the same group. */
    for (unsigned pass = 0U; pass < 2U; ++pass) {
        for (size_t old = 0; old < previous_count; ++old) {
            size_t before = order->indices[old]; bool used = false;
            for (size_t i = 0; i < count; ++i) if (matched[i] == before) { used = true; break; }
            if (used) continue;
            for (size_t i = 0; i < count; ++i) {
                if (matched[i] != SIZE_MAX || !identity(snapshot, i, order->baseline, before)) continue;
                bool equal = false;
                if (!pass) {
                    result = note4_quota_record_equal(snapshot, i, order->baseline, before, &equal);
                    if (result != NOTE4_QUOTA_OK) goto failure;
                }
                if (pass || equal) { matched[i] = before; break; }
            }
        }
    }
    size_t length = 0U;
    for (size_t old = 0; old < previous_count; ++old) {
        for (size_t i = 0; i < count; ++i) if (matched[i] == order->indices[old]) { indices[length++] = i; break; }
    }
    for (size_t i = 0; i < count; ++i) if (matched[i] == SIZE_MAX) indices[length++] = i;
    if (order->baseline) {
        for (size_t i = 0; i < count; ++i) {
            size_t group = 0U;
            while (group < i && !identity(snapshot, group, snapshot, i)) ++group;
            if (group < i) { changed[i] = changed[group]; continue; }
            size_t old = 0U;
            while (old < previous_count && !identity(snapshot, i, order->baseline, old)) ++old;
            bool equal = false;
            if (old < previous_count) {
                result = note4_quota_group_equal(snapshot, i, order->baseline, old, &equal);
                if (result != NOTE4_QUOTA_OK) goto failure;
            }
            changed[i] = !equal;
        }
        size_t position = 0U;
        for (unsigned pass = 0U; pass < 2U; ++pass) for (size_t i = 0; i < count; ++i)
            if (changed[indices[i]] == (pass == 0U)) matched[position++] = indices[i];
        if (count) memcpy(indices, matched, count * sizeof(*indices));
    }
    result = note4_quota_set_smart_order(snapshot, indices);
    if (result != NOTE4_QUOTA_OK) goto failure;
    note4_quota_snapshot_retain(snapshot);
    note4_quota_order_clear(candidate);
    candidate->baseline = snapshot; candidate->indices = indices;
    candidate->changed = !order->baseline || count != previous_count;
    for (size_t i = 0; i < count; ++i) if (changed[i]) candidate->changed = true;
    free(matched); free(changed); return NOTE4_QUOTA_OK;
failure:
    free(indices); free(matched); free(changed); return result;
}
note4_quota_result_t note4_quota_order_observe(note4_quota_order_t *order, note4_quota_snapshot_t *snapshot) {
    note4_quota_order_t candidate = {0};
    const note4_quota_result_t result = note4_quota_order_prepare(order, snapshot, &candidate);
    if (result == NOTE4_QUOTA_OK) {
        note4_quota_order_clear(order);
        *order = candidate;
    }
    return result;
}
size_t note4_quota_order_index(const note4_quota_order_t *order, size_t position) {
    return order && position < note4_quota_count(order->baseline) ? order->indices[position] : SIZE_MAX;
}
void note4_quota_order_clear(note4_quota_order_t *order) {
    if (!order) return;
    note4_quota_snapshot_release(order->baseline); free(order->indices);
    memset(order, 0, sizeof(*order));
}
