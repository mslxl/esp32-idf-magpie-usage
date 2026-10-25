#ifndef NOTE4_QUOTA_SNAPSHOT_H
#define NOTE4_QUOTA_SNAPSHOT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct note4_quota_snapshot note4_quota_snapshot_t;
typedef enum { NOTE4_QUOTA_OK, NOTE4_QUOTA_INVALID, NOTE4_QUOTA_NO_MEMORY } note4_quota_result_t;
note4_quota_result_t note4_quota_snapshot_parse(const char *json, size_t length, note4_quota_snapshot_t **output);
void note4_quota_snapshot_retain(note4_quota_snapshot_t *snapshot);
void note4_quota_snapshot_release(note4_quota_snapshot_t *snapshot);
size_t note4_quota_count(const note4_quota_snapshot_t *snapshot);
size_t note4_quota_smart_rank(const note4_quota_snapshot_t *snapshot, size_t card);
note4_quota_result_t note4_quota_set_smart_order(note4_quota_snapshot_t *snapshot, const size_t *indices);
note4_quota_result_t note4_quota_record_equal(const note4_quota_snapshot_t *a, size_t ai, const note4_quota_snapshot_t *b, size_t bi, bool *equal);
note4_quota_result_t note4_quota_group_equal(const note4_quota_snapshot_t *a, size_t ai, const note4_quota_snapshot_t *b, size_t bi, bool *equal);
const char *note4_quota_text(const note4_quota_snapshot_t *snapshot, size_t card, const char *field);
size_t note4_quota_window_count(const note4_quota_snapshot_t *snapshot, size_t card);
const char *note4_quota_window_text(const note4_quota_snapshot_t *snapshot, size_t card, size_t window, const char *field);
bool note4_quota_flag(const note4_quota_snapshot_t *snapshot, size_t card, const char *field);
bool note4_quota_window_flag(const note4_quota_snapshot_t *snapshot, size_t card, size_t window, const char *field);
size_t note4_quota_balance_count(const note4_quota_snapshot_t *snapshot, size_t card);
const char *note4_quota_balance_text(const note4_quota_snapshot_t *snapshot, size_t card, size_t part, const char *field);
#endif
