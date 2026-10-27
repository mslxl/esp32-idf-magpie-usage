#ifndef NOTE4_MAGPIE_WORKER_H
#define NOTE4_MAGPIE_WORKER_H
#include "esp_err.h"
#include "magpie_poll.h"

typedef enum {
    NOTE4_MAGPIE_IDLE,
    NOTE4_MAGPIE_CLOCK_SYNC,
    NOTE4_MAGPIE_SIGNING_IN,
    NOTE4_MAGPIE_SIGNED_IN,
    NOTE4_MAGPIE_READING,
    NOTE4_MAGPIE_READY,
    NOTE4_MAGPIE_FAILED,
} note4_magpie_phase_t;

typedef struct {
    unsigned epoch;
    note4_magpie_phase_t phase;
    note4_magpie_result_t result;
    /* Borrowed during the callback; a queued consumer must retain its reference. */
    note4_quota_snapshot_t *snapshot;
    bool reading;
    uint32_t retry_seconds;
} note4_magpie_event_t;

typedef bool (*note4_magpie_handler_t)(const note4_magpie_event_t *event, void *context);
void note4_magpie_worker_published(unsigned epoch, bool accepted);
bool note4_magpie_worker_current(unsigned epoch);
esp_err_t note4_magpie_worker_init(note4_magpie_handler_t handler, void *context);
unsigned note4_magpie_worker_update(const note4_settings_t *settings, note4_poll_mode_t mode);
#endif
