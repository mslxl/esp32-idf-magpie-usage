#ifndef NOTE4_QUOTA_ORDER_H
#define NOTE4_QUOTA_ORDER_H
#include "quota_snapshot.h"

typedef struct {
    note4_quota_snapshot_t *baseline;
    size_t *indices;
} note4_quota_order_t;

/* Builds a candidate without advancing the committed baseline. Initialize candidate to zero. */
note4_quota_result_t note4_quota_order_prepare(const note4_quota_order_t *order,
    note4_quota_snapshot_t *snapshot, note4_quota_order_t *candidate);
note4_quota_result_t note4_quota_order_observe(note4_quota_order_t *order, note4_quota_snapshot_t *snapshot);
size_t note4_quota_order_index(const note4_quota_order_t *order, size_t position);
void note4_quota_order_clear(note4_quota_order_t *order);
#endif
