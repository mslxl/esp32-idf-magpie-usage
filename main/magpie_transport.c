#include "magpie_transport.h"
#include "resource_budget.h"
#include "mbedtls/ssl.h"

#include <errno.h>
#include "esp_log.h"
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

#ifdef CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
/* One resumable TLS session for the configured endpoint. Only the worker task
 * touches it. A resumed handshake skips the certificate chain and key exchange. */
static esp_tls_client_session_t *s_session;
static char s_session_host[NOTE4_MAGPIE_ENDPOINT_MAX + 1U];
static unsigned s_session_port;
static void session_forget(void) {
    if (s_session != NULL) esp_tls_free_client_session(s_session);
    s_session = NULL; s_session_host[0] = 0; s_session_port = 0U;
}
#endif

static bool socket_idle(note4_magpie_transport_t *transport) {
    int socket = -1;
    if (esp_tls_get_conn_sockfd(transport->tls, &socket) != ESP_OK || socket < 0) return false;
    if (esp_tls_get_bytes_avail(transport->tls) > 0) return false;
    uint8_t probe;
    errno = 0;
    const ssize_t n = recv(socket, &probe, 1U, MSG_PEEK | MSG_DONTWAIT);
    /* Anything readable on an idle connection is EOF or an alert: reconnect. */
    return n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
}

static void transport_close(void *context);

static int transport_open(void *context, const char *host, unsigned port, bool secure) {
    note4_magpie_transport_t *transport = context;
    if (transport->connected) {
        if (transport->secure == secure && transport->port == port && strcmp(transport->hostname, host) == 0 &&
            socket_idle(transport)) return 1;
        transport_close(transport);
    }
    transport->failed = false;
    if (transport->dns == NULL) {
        dns_request_t *request = calloc(1U, sizeof(*request));
        if (request == NULL) return -4;
        atomic_init(&request->references, 2U);
        atomic_init(&request->state, 0);
        memcpy(request->hostname, host, strlen(host) + 1U);
        memcpy(transport->hostname, host, strlen(host) + 1U);
        transport->secure = secure; transport->port = port;
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
    if (state < 0) { transport->failed = true; return -1; }
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
#ifdef CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
        if (secure && (s_session_port != port || strcmp(s_session_host, transport->hostname) != 0)) session_forget();
        if (secure && s_session != NULL) transport->config.client_session = s_session;
#endif
    }
    transport->want_write = true;
    const int rc = esp_tls_conn_new_async(numeric, (int)strlen(numeric), (int)port,
        &transport->config, transport->tls);
    if (rc == 1) {
        int socket = -1;
        if (esp_tls_get_conn_sockfd(transport->tls, &socket) != ESP_OK || socket < 0) return -1;
        const int flags = fcntl(socket, F_GETFL, 0);
        if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
        transport->connected = true;
        transport->want_write = false;
        note4_resource_log("tls-connected", 0U);
    } else if (rc < 0) transport->failed = true;
    if (rc < 0 && secure) {
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
    note4_magpie_transport_t *transport = context;
    errno = 0;
    const int rc = translate(esp_tls_conn_write(transport->tls, bytes, length));
    transport->want_write = rc == -2;
    if (rc < 0 && rc != -2) transport->failed = true;
    return rc;
}
static int transport_read(void *context, void *bytes, size_t length) {
    note4_magpie_transport_t *transport = context;
    errno = 0;
    const int rc = translate(esp_tls_conn_read(transport->tls, bytes, length));
    transport->want_write = false;
    if (rc <= 0 && rc != -2) transport->failed = true;
    return rc;
}
static void transport_close(void *context) {
    note4_magpie_transport_t *transport = context;
    if (transport->tls != NULL) {
#ifdef CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
        if (transport->secure && transport->failed) session_forget();
        else if (transport->secure && transport->connected) {
            esp_tls_client_session_t *session = esp_tls_get_client_session(transport->tls);
            if (session != NULL) {
                session_forget();
                s_session = session; s_session_port = transport->port;
                memcpy(s_session_host, transport->hostname, strlen(transport->hostname) + 1U);
            }
        }
#endif
        (void)esp_tls_conn_destroy(transport->tls);
    }
    if (transport->dns != NULL) dns_release(transport->dns);
    transport->tls = NULL; transport->dns = NULL; transport->connected = false; transport->failed = false;
}
static void transport_release(void *context, bool reusable) {
    note4_magpie_transport_t *transport = context;
    if (!reusable || !transport->connected || transport->failed) transport_close(transport);
}
static uint64_t transport_now(void *context) { (void)context; return (uint64_t)(esp_timer_get_time() / 1000); }
/* Block in select() until the socket can make progress, bounded so cancellation
 * and client deadlines are still observed. */
#define NOTE4_TRANSPORT_WAIT_MS 200
static void transport_wait(void *context) {
    note4_magpie_transport_t *transport = context;
    if (transport->on_wait != NULL) transport->on_wait(transport->wait_context);
    int socket = -1;
    if (transport->tls == NULL || esp_tls_get_conn_sockfd(transport->tls, &socket) != ESP_OK || socket < 0) {
        /* DNS is still resolving on the lwIP thread. */
        vTaskDelay(pdMS_TO_TICKS(20));
        return;
    }
    fd_set readable, writable;
    FD_ZERO(&readable); FD_ZERO(&writable);
    FD_SET(socket, &readable);
    if (transport->want_write || !transport->connected) FD_SET(socket, &writable);
    struct timeval timeout = {.tv_sec = 0, .tv_usec = NOTE4_TRANSPORT_WAIT_MS * 1000};
    if (select(socket + 1, &readable, &writable, NULL, &timeout) < 0) vTaskDelay(pdMS_TO_TICKS(20));
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
        .wait = transport_wait, .cancelled = transport_cancelled, .release = transport_release};
}

void note4_magpie_transport_close(note4_magpie_transport_t *transport) {
    transport_close(transport);
}

void note4_magpie_transport_forget_session(void) {
#ifdef CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS
    session_forget();
#endif
}
