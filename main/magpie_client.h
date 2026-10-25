#ifndef NOTE4_MAGPIE_CLIENT_H
#define NOTE4_MAGPIE_CLIENT_H

#include "settings.h"
#include "quota_snapshot.h"

typedef enum {
    NOTE4_MAGPIE_OK,
    NOTE4_MAGPIE_CONFIG_REQUIRED,
    NOTE4_MAGPIE_AUTH_REQUIRED,
    NOTE4_MAGPIE_FORBIDDEN,
    NOTE4_MAGPIE_CERTIFICATE,
    NOTE4_MAGPIE_NETWORK,
    NOTE4_MAGPIE_TIMEOUT,
    NOTE4_MAGPIE_CANCELLED,
    NOTE4_MAGPIE_INVALID_RESPONSE,
    NOTE4_MAGPIE_RESOURCE_EXHAUSTED,
    NOTE4_MAGPIE_RATE_LIMITED,
} note4_magpie_result_t;

/* open: 1 connected, 0 pending, negative failure. read/write: -2 would block. */
typedef struct {
    void *context;
    int (*open)(void *context, const char *host, unsigned port, bool secure);
    int (*write)(void *context, const void *bytes, size_t length);
    int (*read)(void *context, void *bytes, size_t capacity);
    void (*close)(void *context);
    uint64_t (*now_ms)(void *context);
    void (*wait)(void *context);
    bool (*cancelled)(void *context);
} note4_magpie_io_t;

typedef struct {
    char endpoint[NOTE4_MAGPIE_ENDPOINT_MAX + 1U];
    char cookie_name[33];
    char cookie_value[NOTE4_MAGPIE_KEY_MAX + 1U];
    int64_t expires_at;
} note4_magpie_session_t;

typedef struct {
    unsigned status;
    bool reading;
    char retry_after[128];
    char date[64];
    char *body;
    size_t length;
} note4_magpie_response_t;

note4_magpie_result_t note4_magpie_login(const note4_settings_t *settings,
    const note4_magpie_io_t *io, int64_t wall_time, note4_magpie_session_t *session);
note4_magpie_result_t note4_magpie_get_quotas(const note4_settings_t *settings,
    const note4_magpie_io_t *io, int64_t wall_time, note4_magpie_session_t *session,
    note4_magpie_response_t *response);
/* On success returns one owned candidate; on failure leaves *snapshot untouched.
 * The response retains bounded headers, but its body is consumed and freed. */
note4_magpie_result_t note4_magpie_read_snapshot(const note4_settings_t *settings,
    const note4_magpie_io_t *io, int64_t wall_time, note4_magpie_session_t *session,
    note4_magpie_response_t *response, note4_quota_snapshot_t **snapshot);
void note4_magpie_response_clear(note4_magpie_response_t *response);
bool note4_magpie_http_date(const char *value, int64_t *epoch);

#endif
