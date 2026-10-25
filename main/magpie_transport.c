#include "magpie_transport.h"
#include "resource_budget.h"
#include "mbedtls/ssl.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/dns.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"

typedef struct {
    atomic_uint references;
    atomic_int state;
    char hostname[NOTE4_MAGPIE_ENDPOINT_MAX + 1U];
    ip_addr_t address;
} dns_request_t;

static void dns_release(dns_request_t *request) {
    if (atomic_fetch_sub(&request->references, 1U) == 1U) free(request);
}

static void dns_complete(const char *name, const ip_addr_t *address, void *argument) {
    (void)name;
    dns_request_t *request = argument;
    if (address != NULL) request->address = *address;
    atomic_store(&request->state, address == NULL ? -1 : 1);
    dns_release(request);
}

static void dns_start(void *argument) {
    dns_request_t *request = argument;
    ip_addr_t address;
    const err_t result = dns_gethostbyname(request->hostname, &address, dns_complete, request);
    if (result != ERR_INPROGRESS) {
        if (result == ERR_OK) request->address = address;
        atomic_store(&request->state, result == ERR_OK ? 1 : -1);
        dns_release(request);
    }
}

static int transport_open(void *context, const char *host, unsigned port, bool secure) {
    note4_magpie_transport_t *transport = context;
    if (transport->connected) return 1;
    if (transport->dns == NULL) {
        dns_request_t *request = calloc(1U, sizeof(*request));
        if (request == NULL) return -4;
        atomic_init(&request->references, 2U);
        atomic_init(&request->state, 0);
        memcpy(request->hostname, host, strlen(host) + 1U);
        memcpy(transport->hostname, host, strlen(host) + 1U);
        transport->dns = request;
        if (tcpip_try_callback(dns_start, request) != ERR_OK) {
            dns_release(request); dns_release(request); transport->dns = NULL;
            return -1;
        }
        return 0;
    }
    dns_request_t *request = transport->dns;
    const int state = atomic_load(&request->state);
    if (state == 0) return 0;
    if (state < 0) return -1;
    char numeric[IPADDR_STRLEN_MAX];
    if (ipaddr_ntoa_r(&request->address, numeric, sizeof(numeric)) == NULL) return -1;
    if (transport->tls == NULL) {
        note4_resource_log("tls-start", 0U);
        if (!note4_resource_available(32768U)) return -4;
        transport->tls = esp_tls_init();
        if (transport->tls == NULL) return -4;
        transport->config = (esp_tls_cfg_t){.non_block = true, .timeout_ms = 20,
            .is_plain_tcp = !secure, .crt_bundle_attach = secure ? esp_crt_bundle_attach : NULL,
            .common_name = transport->hostname};
    }
    const int rc = esp_tls_conn_new_async(numeric, (int)strlen(numeric), (int)port,
        &transport->config, transport->tls);
    if (rc == 1) {
        int socket = -1;
        if (esp_tls_get_conn_sockfd(transport->tls, &socket) != ESP_OK || socket < 0) return -1;
        const int flags = fcntl(socket, F_GETFL, 0);
        if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
        transport->connected = true;
        note4_resource_log("tls-connected", 0U);
    } else if (rc < 0 && secure) {
        esp_tls_error_handle_t handle;
        int code = 0, flags = 0;
        if (esp_tls_get_error_handle(transport->tls, &handle) == ESP_OK) {
            const esp_err_t error = esp_tls_get_and_clear_last_error(handle, &code, &flags);
            if (flags != 0) return -3;
            if (error == ESP_ERR_NO_MEM || code == MBEDTLS_ERR_SSL_ALLOC_FAILED) return -4;
        }
    }
    return rc;
}

static int translate(ssize_t result) {
    if (result == ESP_TLS_ERR_SSL_WANT_READ || result == ESP_TLS_ERR_SSL_WANT_WRITE ||
        (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) return -2;
    if (result == MBEDTLS_ERR_SSL_ALLOC_FAILED || (result < 0 && errno == ENOMEM)) return -4;
    return (int)result;
}

static int transport_write(void *context, const void *bytes, size_t length) {
    errno = 0;
    return translate(esp_tls_conn_write(((note4_magpie_transport_t *)context)->tls, bytes, length));
}
static int transport_read(void *context, void *bytes, size_t length) {
    errno = 0;
    return translate(esp_tls_conn_read(((note4_magpie_transport_t *)context)->tls, bytes, length));
}
static void transport_close(void *context) {
    note4_magpie_transport_t *transport = context;
    if (transport->tls != NULL) (void)esp_tls_conn_destroy(transport->tls);
    if (transport->dns != NULL) dns_release(transport->dns);
    transport->tls = NULL; transport->dns = NULL; transport->connected = false;
}
static uint64_t transport_now(void *context) { (void)context; return (uint64_t)(esp_timer_get_time() / 1000); }
static void transport_wait(void *context) {
    note4_magpie_transport_t *transport = context;
    if (transport->on_wait != NULL) transport->on_wait(transport->wait_context);
    vTaskDelay(pdMS_TO_TICKS(10));
}
static bool transport_cancelled(void *context) {
    note4_magpie_transport_t *transport = context;
    return atomic_load(transport->epoch) != transport->expected_epoch;
}

void note4_magpie_transport_init(note4_magpie_transport_t *transport, atomic_uint *epoch,
    unsigned expected_epoch, note4_magpie_io_t *io) {
    memset(transport, 0, sizeof(*transport));
    transport->epoch = epoch; transport->expected_epoch = expected_epoch;
    *io = (note4_magpie_io_t){.context = transport, .open = transport_open, .read = transport_read,
        .write = transport_write, .close = transport_close, .now_ms = transport_now,
        .wait = transport_wait, .cancelled = transport_cancelled};
}
