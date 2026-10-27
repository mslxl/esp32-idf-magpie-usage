#ifndef NOTE4_MAGPIE_TRANSPORT_H
#define NOTE4_MAGPIE_TRANSPORT_H
#include <stdatomic.h>
#include "esp_tls.h"
#include "magpie_client.h"

typedef struct {
    esp_tls_t *tls;
    esp_tls_cfg_t config;
    atomic_uint *epoch;
    unsigned expected_epoch;
    bool connected;
    bool secure;
    bool failed;
    bool want_write;
    unsigned port;
    void (*on_wait)(void *context);
    void *wait_context;
    void *dns;
    char hostname[NOTE4_MAGPIE_ENDPOINT_MAX + 1U];
} note4_magpie_transport_t;

void note4_magpie_transport_init(note4_magpie_transport_t *transport, atomic_uint *epoch,
    unsigned expected_epoch, note4_magpie_io_t *io);
/* Exchanges may leave a keep-alive connection open; this ends it. */
void note4_magpie_transport_close(note4_magpie_transport_t *transport);
/* Drops the cached TLS session ticket, e.g. when the endpoint changes. */
void note4_magpie_transport_forget_session(void);
#endif
