#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "magpie_poll.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed %d: %s\n", __LINE__, #x); exit(1); } } while (0)
typedef struct {
    uint64_t now; const char *response; size_t offset; unsigned requests; unsigned duration;
    uint64_t readable_at, error_observed_at;
    const note4_magpie_poll_t *waiting_poll;
} peer_t;
static int open_peer(void *p, const char *host, unsigned port, bool secure) {
    peer_t *peer = p; (void)host; (void)port; (void)secure; ++peer->requests; peer->offset = 0; return 1;
}
static int write_peer(void *p, const void *bytes, size_t n) { (void)p; (void)bytes; return (int)n; }
static int read_peer(void *p, void *bytes, size_t capacity) {
    peer_t *peer = p;
    if (peer->now < peer->readable_at) return -2;
    size_t n = strlen(peer->response) - peer->offset;
    if (n > capacity) n = capacity;
    memcpy(bytes, peer->response + peer->offset, n); peer->offset += n; return (int)n;
}
static void close_peer(void *p) { peer_t *peer = p; peer->now += peer->duration; }
static uint64_t now_peer(void *p) { return ((peer_t *)p)->now; }
static void wait_peer(void *p) {
    peer_t *peer = p;
    if (peer->waiting_poll != NULL && peer->error_observed_at == 0U &&
        note4_magpie_poll_error_page(peer->waiting_poll, peer->now))
        peer->error_observed_at = peer->now;
    peer->now += 10U;
}
static bool cancelled(void *p) { (void)p; return false; }
static const char *login = "HTTP/1.1 303 See Other\r\nSet-Cookie: magpie_web_8080=web-key-0123456789; Path=/; Secure\r\n\r\n";
static const char *ready = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n[]";
static const char *reading = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nX-Magpie-Reading: 1\r\nContent-Length: 2\r\n\r\n[]";
static void step(note4_magpie_poll_t *poll, note4_magpie_io_t *io, peer_t *peer, const char *response) {
    peer->response = response;
    note4_quota_snapshot_t *snapshot = NULL;
    note4_magpie_poll_step(poll, io, 1792886400, 0U, &snapshot);
    note4_quota_snapshot_release(snapshot);
}
static void replay_order(note4_magpie_poll_t *poll, note4_magpie_io_t *io, peer_t *peer, const char *json, const char *expected, bool emit) {
    char response[4096];
    const int length = snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s", strlen(json), json);
    CHECK(length > 0 && (size_t)length < sizeof(response));
    peer->response = response; peer->now = poll->next_due;
    note4_quota_snapshot_t *snapshot = NULL;
    CHECK(note4_magpie_poll_step(poll, io, 1792886400, 0U, &snapshot));
    CHECK(snapshot && note4_quota_count(snapshot) == strlen(expected));
    if (emit) printf("{\"cards\":%s,\"ranks\":[", json);
    for (size_t i = 0; i < strlen(expected); ++i) {
        const size_t index = note4_quota_order_index(&poll->order, i);
        CHECK(note4_quota_text(snapshot, index, "provider")[0] == expected[i]);
        if (emit) printf("%s%zu", i ? "," : "", note4_quota_smart_rank(snapshot, i));
    }
    if (emit) puts("]}");
    note4_quota_snapshot_release(snapshot);
}
int main(int argc, char **argv) {
    const bool emit = argc == 2 && !strcmp(argv[1], "--order-replay");
    note4_settings_t settings; note4_settings_clear(&settings);
    memcpy(settings.magpie.endpoint, "https://magpie.test", sizeof("https://magpie.test"));
    memcpy(settings.magpie.key, "web-key-0123456789", sizeof("web-key-0123456789"));
    peer_t peer = {0};
    note4_magpie_io_t io = {&peer, open_peer, write_peer, read_peer, close_peer, now_peer, wait_peer, cancelled};
    note4_magpie_poll_t poll;
    note4_magpie_poll_init(&poll);
    note4_magpie_poll_configure(&poll, &settings, NOTE4_POLL_ONLINE, 0U);
    step(&poll, &io, &peer, login);
    step(&poll, &io, &peer, ready);
    CHECK(peer.requests == 2U && poll.next_due == 30000U && poll.has_snapshot);
    peer.now = 29999U; step(&poll, &io, &peer, ready); CHECK(peer.requests == 2U);
    peer.now = 30000U; peer.duration = 35000U; step(&poll, &io, &peer, ready);
    CHECK(poll.next_due == 90000U);
    peer.duration = 0U; peer.now = 90000U; step(&poll, &io, &peer, reading);
    CHECK(poll.next_due == 91500U);
    for (unsigned i = 0; i < 20U; ++i) { peer.now = poll.next_due; step(&poll, &io, &peer, reading); }
    CHECK(poll.followups == 20U && poll.next_due == 150000U && poll.reading);
    peer.now = poll.next_due; step(&poll, &io, &peer, "HTTP/1.1 500 Error\r\n\r\n");
    CHECK(poll.failed && poll.next_due == 210000U && !note4_magpie_poll_error_page(&poll, 269999U));
    CHECK(note4_magpie_poll_error_page(&poll, 270000U));
    note4_magpie_poll_configure(&poll, &settings, NOTE4_POLL_SETUP, 200000U);
    CHECK(!note4_magpie_poll_error_page(&poll, 999999U));
    note4_magpie_poll_configure(&poll, &settings, NOTE4_POLL_ONLINE, 300000U);
    CHECK(!note4_magpie_poll_error_page(&poll, 369999U));
    CHECK(note4_magpie_poll_error_page(&poll, 370000U));
    peer.now = 300000U; step(&poll, &io, &peer, ready); CHECK(!poll.failed);
    peer.now = poll.next_due; step(&poll, &io, &peer, "HTTP/1.1 401 Unauthorized\r\n\r\n");
    CHECK(poll.relogin_used && !poll.halted);
    step(&poll, &io, &peer, login);
    step(&poll, &io, &peer, "HTTP/1.1 401 Unauthorized\r\n\r\n");
    CHECK(poll.halted && poll.result == NOTE4_MAGPIE_AUTH_REQUIRED);
    const unsigned requests = peer.requests;
    peer.now += 1000000U; step(&poll, &io, &peer, ready); CHECK(peer.requests == requests);
    ++settings.revision;
    note4_magpie_poll_configure(&poll, &settings, NOTE4_POLL_ONLINE, peer.now);
    step(&poll, &io, &peer, login);
    step(&poll, &io, &peer, "HTTP/1.1 429 Too Many Requests\r\nRetry-After: 3600\r\n\r\n");
    CHECK(poll.next_due == peer.now + 3600000U);
    note4_magpie_poll_configure(&poll, &settings, NOTE4_POLL_SETUP, peer.now);
    note4_magpie_poll_configure(&poll, &settings, NOTE4_POLL_ONLINE, peer.now + 500U);
    CHECK(poll.next_due == peer.now + 3600000U);
    peer.now = poll.next_due;
    step(&poll, &io, &peer, "HTTP/1.1 429 Too Many Requests\r\nRetry-After: invalid\r\nRetry-After: 7200\r\n\r\n");
    CHECK(poll.result == NOTE4_MAGPIE_RATE_LIMITED && poll.next_due == peer.now + 120000U);
    const uint64_t backoff_due = poll.next_due;
    const unsigned before_setup = peer.requests;
    note4_magpie_poll_configure(&poll, &settings, NOTE4_POLL_SETUP, peer.now + 100U);
    note4_magpie_poll_configure(&poll, &settings, NOTE4_POLL_ONLINE, peer.now + 200U);
    peer.now += 200U; step(&poll, &io, &peer, ready);
    CHECK(peer.requests == before_setup && poll.next_due == backoff_due);
    note4_magpie_response_t no_headers = {0};
    note4_magpie_poll_fail(&poll, NOTE4_MAGPIE_FORBIDDEN, &no_headers, peer.now, 1792886400, 0U);
    note4_magpie_poll_configure(&poll, &settings, NOTE4_POLL_OFFLINE, peer.now + 100U);
    CHECK(poll.halted && poll.result == NOTE4_MAGPIE_FORBIDDEN);
    note4_magpie_poll_configure(&poll, &settings, NOTE4_POLL_ONLINE, peer.now + 200U);
    step(&poll, &io, &peer, ready);
    CHECK(peer.requests == before_setup && poll.result == NOTE4_MAGPIE_FORBIDDEN);
    memcpy(settings.magpie.endpoint, "https://replay.test", sizeof("https://replay.test"));
    note4_magpie_poll_configure(&poll, &settings, NOTE4_POLL_ONLINE, peer.now);
    step(&poll, &io, &peer, login);
    replay_order(&poll, &io, &peer, "[{\"provider\":\"A\",\"windows\":[{\"used\":1}]},{\"provider\":\"B\",\"windows\":[{\"used\":2}]}]", "AB", emit);
    peer.now = poll.next_due; step(&poll, &io, &peer, "HTTP/1.1 500 Error\r\n\r\n");
    replay_order(&poll, &io, &peer, "[{\"provider\":\"A\",\"windows\":[{\"used\":1.0}]},{\"provider\":\"B\",\"windows\":[{\"used\":2.0000000000000001}]}]", "BA", emit);
    replay_order(&poll, &io, &peer, "[{\"provider\":\"B\",\"windows\":[{\"used\":2.00000000000000010}]},{\"provider\":\"A\",\"windows\":[{\"used\":1e0}]}]", "BA", emit);
    peer.now = poll.next_due; step(&poll, &io, &peer, "HTTP/1.1 500 Error\r\n\r\n");
    peer.now = poll.failed_at + 115000U;
    peer.readable_at = peer.now + 10000U;
    peer.waiting_poll = &poll;
    step(&poll, &io, &peer, ready);
    CHECK(peer.error_observed_at == peer.readable_at - 5000U);
    CHECK(peer.error_observed_at < peer.now && !poll.failed);
    peer.waiting_poll = NULL;
    note4_quota_order_clear(&poll.order);
    if (!emit) puts("magpie poll behavior passed");
    return 0;
}
