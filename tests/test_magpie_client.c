#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "magpie_client.h"

#define CHECK(value) do { if (!(value)) { fprintf(stderr, "failed: %s:%d %s\n", __FILE__, __LINE__, #value); exit(1); } } while (0)

typedef struct {
    const char *response;
    size_t offset;
    char request[4096];
    size_t request_length;
    unsigned opens;
    uint64_t now;
    bool stall;
    bool cancelled;
    unsigned trickle_ms;
} peer_t;

static int open_peer(void *context, const char *host, unsigned port, bool secure) {
    peer_t *peer = context;
    CHECK(strcmp(host, "magpie.test") == 0 && port == 443U && secure);
    ++peer->opens;
    return 1;
}
static int write_peer(void *context, const void *bytes, size_t length) {
    peer_t *peer = context;
    CHECK(peer->request_length + length < sizeof(peer->request));
    memcpy(peer->request + peer->request_length, bytes, length);
    peer->request_length += length;
    return (int)length;
}
static int read_peer(void *context, void *bytes, size_t capacity) {
    peer_t *peer = context;
    if (peer->stall) return -2;
    const size_t remaining = strlen(peer->response) - peer->offset;
    if (peer->trickle_ms) { capacity = 1U; peer->now += peer->trickle_ms; }
    const size_t length = remaining < capacity ? remaining : capacity;
    memcpy(bytes, peer->response + peer->offset, length);
    peer->offset += length;
    return (int)length;
}
static void close_peer(void *context) { (void)context; }
static uint64_t now_peer(void *context) { return ((peer_t *)context)->now; }
static void wait_peer(void *context) { ((peer_t *)context)->now += 10U; }
static bool cancel_peer(void *context) { return ((peer_t *)context)->cancelled; }

int main(void) {
    note4_settings_t settings;
    note4_settings_clear(&settings);
    memcpy(settings.magpie.endpoint, "https://magpie.test/proxy", sizeof("https://magpie.test/proxy"));
    memcpy(settings.magpie.key, "web-key-0123456789", sizeof("web-key-0123456789"));
    peer_t peer = {.response = "HTTP/1.1 303 See Other\r\nLocation: https://outside.test/\r\nSet-Cookie: magpie_web_8080=web-key-0123456789; Path=/proxy; HttpOnly; Secure\r\nContent-Length: 0\r\n\r\n"};
    note4_magpie_io_t io = {.context = &peer, .open = open_peer, .write = write_peer, .read = read_peer,
        .close = close_peer, .now_ms = now_peer, .wait = wait_peer, .cancelled = cancel_peer};
    note4_magpie_session_t session = {0};
    CHECK(note4_magpie_login(&settings, &io, 1792886400, &session) == NOTE4_MAGPIE_OK);
    CHECK(peer.opens == 1U);
    CHECK(strncmp(peer.request, "POST /proxy/_magpie/sign-in HTTP/1.1\r\n", 37U) == 0);
    CHECK(strstr(peer.request, "key=web-key-0123456789&back=%2Fproxy%2F") != NULL);
    CHECK(strcmp(session.cookie_name, "magpie_web_8080") == 0);
    CHECK(strcmp(session.cookie_value, "web-key-0123456789") == 0);
    CHECK(strcmp(session.endpoint, settings.magpie.endpoint) == 0);
    peer = (peer_t){.response = "HTTP/1.1 303 See Other\r\nSet-Cookie: magpie_web_8080=web-key-0123456789; Path=/; Secure; Expires=Wed, 21 Oct 2037 07:28:00 GMT\r\nContent-Length: 0\r\n\r\n"};
    CHECK(note4_magpie_login(&settings, &io, 1792886400, &session) == NOTE4_MAGPIE_OK);
    CHECK(session.expires_at == 2139722880);
    const char *invalid[] = {
        "HTTP/1.1 303 See Other\r\nSet-Cookie: magpie_web_8080=other-key-0123456; Path=/; Secure\r\n\r\n",
        "HTTP/1.1 303 See Other\r\nSet-Cookie: magpie_web_8080=web-key-0123456789; Path=/wrong; Secure\r\n\r\n",
        "HTTP/1.1 303 See Other\r\nSet-Cookie: magpie_web_8080=web-key-0123456789; Path=/; Domain=other.test\r\n\r\n",
        "HTTP/1.1 303 See Other\r\nSet-Cookie: magpie_web_8080=web-key-0123456789; Path=/; Max-Age=0\r\n\r\n",
        "HTTP/1.1 302 Found\r\nSet-Cookie: magpie_web_8080=web-key-0123456789; Path=/\r\n\r\n",
        "HTTP/1.1 303 See Other\r\nSet-Cookie: magpie_web_8080=web-key-0123456789; Path=/\r\nSet-Cookie: magpie_web_9090=web-key-0123456789; Path=/\r\n\r\n"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        peer = (peer_t){.response = invalid[i]};
        CHECK(note4_magpie_login(&settings, &io, 1792886400, &session) == NOTE4_MAGPIE_INVALID_RESPONSE);
        CHECK(session.cookie_name[0] == 0);
    }
    peer = (peer_t){.response = "HTTP/1.1 401 Unauthorized\r\n\r\n"};
    CHECK(note4_magpie_login(&settings, &io, 1792886400, &session) == NOTE4_MAGPIE_AUTH_REQUIRED);
    peer = (peer_t){.response = "HTTP/1.1 403 Forbidden\r\n\r\n"};
    CHECK(note4_magpie_login(&settings, &io, 1792886400, &session) == NOTE4_MAGPIE_FORBIDDEN);
    char oversized[2300];
    memset(oversized, 'a', sizeof(oversized));
    memcpy(oversized, "HTTP/1.1 303 See Other\r\nX-Test: ", 30U);
    oversized[sizeof(oversized) - 1U] = 0;
    peer = (peer_t){.response = oversized};
    CHECK(note4_magpie_login(&settings, &io, 1792886400, &session) == NOTE4_MAGPIE_INVALID_RESPONSE);
    peer = (peer_t){.stall = true};
    CHECK(note4_magpie_login(&settings, &io, 1792886400, &session) == NOTE4_MAGPIE_TIMEOUT);
    CHECK(peer.now == 15000U);
    peer = (peer_t){.response = "HTTP/1.1 303 See Other\r\nSet-Cookie: magpie_web_8080=web-key-0123456789; Path=/\r\n\r\n", .trickle_ms = 1000U};
    CHECK(note4_magpie_login(&settings, &io, 1792886400, &session) == NOTE4_MAGPIE_TIMEOUT);
    CHECK(peer.now == 30000U);
    peer = (peer_t){.response = "HTTP/1.1 303 See Other\r\nSet-Cookie: magpie_web_8080=web-key-0123456789; Path=/proxy; Secure\r\n\r\n"};
    CHECK(note4_magpie_login(&settings, &io, 1792886400, &session) == NOTE4_MAGPIE_OK);
    peer = (peer_t){.response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 45\r\n\r\n[{\"provider\":\"Codex\",\"windows\":[{\"used\":0}]}]"};
    note4_quota_snapshot_t *snapshot = NULL;
    note4_magpie_response_t response = {0};
    CHECK(note4_magpie_read_snapshot(&settings, &io, 1792886400, &session, &response, &snapshot) == NOTE4_MAGPIE_OK);
    CHECK(note4_quota_count(snapshot) == 1U);
    CHECK(strcmp(note4_quota_window_text(snapshot, 0U, 0U, "used"), "0") == 0);
    CHECK(strstr(peer.request, "GET /proxy/api/usage/quotas HTTP/1.1\r\n") != NULL);
    CHECK(strstr(peer.request, "Cookie: magpie_web_8080=web-key-0123456789\r\n") != NULL);
    CHECK(strstr(peer.request, "Accept: application/json\r\n") != NULL);
    CHECK(strstr(peer.request, "Accept-Encoding: identity\r\n") != NULL);
    note4_quota_snapshot_t *previous = snapshot;
    peer = (peer_t){.response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 4\r\n\r\nnull"};
    CHECK(note4_magpie_read_snapshot(&settings, &io, 1792886400, &session, &response, &snapshot) == NOTE4_MAGPIE_INVALID_RESPONSE);
    CHECK(snapshot == previous && note4_quota_count(snapshot) == 1U);
    note4_quota_snapshot_release(snapshot);
    snapshot = NULL;
    peer = (peer_t){.response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n[]"};
    CHECK(note4_magpie_read_snapshot(&settings, &io, 1792886400, &session, &response, &snapshot) == NOTE4_MAGPIE_OK);
    CHECK(snapshot != NULL && note4_quota_count(snapshot) == 0U);
    note4_quota_snapshot_release(snapshot);
    peer = (peer_t){.cancelled = true};
    CHECK(note4_magpie_login(&settings, &io, 1792886400, &session) == NOTE4_MAGPIE_CANCELLED);
    CHECK(peer.opens == 0U);
    return 0;
}
