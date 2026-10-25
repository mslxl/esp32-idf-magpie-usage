#ifndef NOTE4_MAGPIE_POLL_H
#define NOTE4_MAGPIE_POLL_H
#include "magpie_client.h"
#include "quota_order.h"
typedef enum { NOTE4_POLL_SETUP, NOTE4_POLL_OFFLINE, NOTE4_POLL_ONLINE } note4_poll_mode_t;
typedef struct {
    note4_settings_t settings;
    note4_magpie_session_t session;
    note4_quota_order_t order, pending_order;
    bool defer_publication;
    note4_poll_mode_t mode;
    note4_magpie_result_t result;
    uint64_t next_due, normal_due, retry_after, failed_at, paused_at;
    uint32_t failures;
    unsigned followups;
    bool failed, has_snapshot, reading, halted, relogin_used, login_pending, following;
} note4_magpie_poll_t;
void note4_magpie_poll_init(note4_magpie_poll_t *poll);
void note4_magpie_poll_configure(note4_magpie_poll_t *poll, const note4_settings_t *settings,
    note4_poll_mode_t mode, uint64_t now);
/* Performs at most one HTTP request. A successful GET yields one owned snapshot. */
bool note4_magpie_poll_step(note4_magpie_poll_t *poll, const note4_magpie_io_t *io,
    int64_t wall_time, unsigned jitter, note4_quota_snapshot_t **snapshot);
void note4_magpie_poll_fail(note4_magpie_poll_t *poll, note4_magpie_result_t result,
    const note4_magpie_response_t *response, uint64_t now, int64_t wall_time, unsigned jitter);
bool note4_magpie_poll_error_page(const note4_magpie_poll_t *poll, uint64_t now);
#endif
