#include "magpie_poll.h"
#include <limits.h>
#include <string.h>

static uint64_t add(uint64_t a, uint64_t b) { return a > UINT64_MAX - b ? UINT64_MAX : a + b; }
static uint32_t base_seconds(const note4_magpie_poll_t *poll) {
    return poll->settings.web.interval_seconds > 1800U ? 1800U : poll->settings.web.interval_seconds;
}
static uint64_t interval(const note4_magpie_poll_t *poll) { return (uint64_t)base_seconds(poll) * 1000U; }
static uint64_t retry_interval(const note4_magpie_poll_t *poll) {
    return (uint64_t)poll->settings.web.interval_seconds * 1000U;
}
static void reset_wait(note4_magpie_poll_t *poll) {
    poll->unchanged = 0U; poll->extra_wait_seconds = 0U; poll->cycle_changed = false;
}
void note4_magpie_poll_accept(note4_magpie_poll_t *poll, uint64_t now) {
    poll->cycle_changed |= poll->pending_order.changed || !poll->has_snapshot || poll->failed;
    note4_quota_order_clear(&poll->order); poll->order = poll->pending_order;
    memset(&poll->pending_order, 0, sizeof(poll->pending_order));
    poll->result = NOTE4_MAGPIE_OK; poll->failed = false; poll->failures = 0U; poll->has_snapshot = true;
    if (poll->cycle_changed) { poll->unchanged = 0U; poll->extra_wait_seconds = 0U; }
    if (!poll->reading) {
        if (!poll->cycle_changed && ++poll->unchanged == 3U) {
            const uint32_t limit = 1800U - base_seconds(poll);
            poll->unchanged = 0U;
            poll->extra_wait_seconds = poll->extra_wait_seconds + 60U > limit
                ? limit : poll->extra_wait_seconds + 60U;
        }
        poll->cycle_changed = false;
    }
    const uint64_t delay = poll->following ? 1500U : interval(poll) + (uint64_t)poll->extra_wait_seconds * 1000U;
    poll->next_due = add(now, delay);
}
void note4_magpie_poll_init(note4_magpie_poll_t *poll) {
    memset(poll, 0, sizeof(*poll)); poll->mode = NOTE4_POLL_SETUP;
}
void note4_magpie_poll_configure(note4_magpie_poll_t *poll, const note4_settings_t *settings,
    note4_poll_mode_t mode, uint64_t now) {
    const bool instance_changed = strcmp(poll->settings.magpie.endpoint, settings->magpie.endpoint) != 0;
    const bool saved = poll->settings.revision != settings->revision;
    if (instance_changed) {
        const bool deferred = poll->defer_publication;
        note4_quota_order_clear(&poll->order);
        note4_quota_order_clear(&poll->pending_order);
        note4_magpie_poll_init(poll);
        poll->defer_publication = deferred;
    }
    const bool credentials_changed = strcmp(poll->settings.magpie.key, settings->magpie.key) != 0 ||
        poll->settings.web.allow_plain_http != settings->web.allow_plain_http;
    if (instance_changed || credentials_changed || (saved && poll->halted)) {
        memset(&poll->session, 0, sizeof(poll->session));
        poll->relogin_used = false; poll->halted = false; poll->login_pending = true;
    }
    if (saved || credentials_changed || poll->settings.web.interval_seconds != settings->web.interval_seconds ||
        (mode == NOTE4_POLL_ONLINE && poll->mode != NOTE4_POLL_ONLINE)) reset_wait(poll);
    if (saved) poll->relogin_used = false;
    poll->settings = *settings;
    poll->mode = mode; poll->following = false; poll->followups = 0U;
    if (mode == NOTE4_POLL_OFFLINE && !poll->halted) {
        poll->failed = true;
        poll->result = NOTE4_MAGPIE_NETWORK;
    }
    if (mode == NOTE4_POLL_ONLINE) {
        if (!poll->failed || poll->next_due < now) poll->next_due = now;
        if (poll->retry_after > poll->next_due) poll->next_due = poll->retry_after;
        if (!poll->session.cookie_name[0]) poll->login_pending = true;
    }
}

static uint64_t retry_deadline(const note4_magpie_response_t *response, uint64_t now, int64_t wall_time) {
    const char *value = response->retry_after;
    if (!value[0]) return 0U;
    uint64_t seconds = 0U;
    bool numeric = true;
    for (const char *p = value; *p; ++p) {
        if (*p < '0' || *p > '9') { numeric = false; break; }
        const unsigned digit = (unsigned)(*p - '0');
        if (seconds > (UINT64_MAX - digit) / 10U) return 0U;
        seconds = seconds * 10U + digit;
    }
    if (!numeric) {
        int64_t target, base = wall_time;
        if (!note4_magpie_http_date(value, &target)) return 0U;
        if (base < 1704067200 && !note4_magpie_http_date(response->date, &base)) return 0U;
        if (target <= base) return 0U;
        seconds = (uint64_t)(target - base);
    }
    if (seconds > (UINT64_MAX - now) / 1000U) return 0U;
    return now + seconds * 1000U;
}

void note4_magpie_poll_fail(note4_magpie_poll_t *poll, note4_magpie_result_t result,
    const note4_magpie_response_t *response, uint64_t now, int64_t wall_time, unsigned jitter) {
    poll->failed = true;
    poll->result = result; poll->following = false; poll->reading = false;
    reset_wait(poll);
    if (result == NOTE4_MAGPIE_AUTH_REQUIRED || result == NOTE4_MAGPIE_FORBIDDEN ||
        result == NOTE4_MAGPIE_CERTIFICATE || result == NOTE4_MAGPIE_CONFIG_REQUIRED) {
        poll->halted = true;
        memset(&poll->session, 0, sizeof(poll->session));
        return;
    }
    if (poll->failures != UINT32_MAX) ++poll->failures;
    const uint64_t base = retry_interval(poll), cap = base > 900000U ? base : 900000U;
    uint64_t delay = base;
    for (uint32_t n = 0; n < poll->failures && delay < cap; ++n)
        delay = delay > cap / 2U ? cap : delay * 2U;
    const uint64_t extra = delay / 1000U * (jitter % 101U);
    delay = extra > cap - delay ? cap : delay + extra;
    poll->next_due = add(now, delay);
    if (result == NOTE4_MAGPIE_RATE_LIMITED) {
        const uint64_t deadline = retry_deadline(response, now, wall_time);
        if (deadline > poll->retry_after) poll->retry_after = deadline;
    }
    if (poll->retry_after > poll->next_due) poll->next_due = poll->retry_after;
}

bool note4_magpie_poll_step(note4_magpie_poll_t *poll, const note4_magpie_io_t *io,
    int64_t wall_time, unsigned jitter, note4_quota_snapshot_t **snapshot) {
    const uint64_t started = io->now_ms(io->context);
    if (poll->mode != NOTE4_POLL_ONLINE || poll->halted || started < poll->next_due) return false;
    note4_magpie_response_t response = {0};
    note4_magpie_result_t result;
    const bool login = poll->login_pending;
    if (login) result = note4_magpie_login(&poll->settings, io, wall_time, &poll->session);
    else {
        if (!poll->following) poll->followups = 0U;
        else ++poll->followups;
        result = note4_magpie_read_snapshot(&poll->settings, io, wall_time, &poll->session, &response, snapshot);
    }
    const uint64_t completed = io->now_ms(io->context);
    if (result == NOTE4_MAGPIE_CANCELLED) return false;
    if (!login && result == NOTE4_MAGPIE_AUTH_REQUIRED && !poll->relogin_used) {
        poll->relogin_used = true; poll->login_pending = true;
        memset(&poll->session, 0, sizeof(poll->session));
        poll->next_due = completed;
        return true;
    }
    if (!login && result == NOTE4_MAGPIE_OK && note4_quota_order_prepare(&poll->order, *snapshot, &poll->pending_order) != NOTE4_QUOTA_OK) {
        note4_quota_snapshot_release(*snapshot); *snapshot = NULL;
        result = NOTE4_MAGPIE_RESOURCE_EXHAUSTED;
    }
    if (result != NOTE4_MAGPIE_OK) {
        note4_magpie_poll_fail(poll, result, &response, completed, wall_time, jitter);
        return true;
    }
    if (login) {
        poll->login_pending = false; poll->next_due = completed;
        return true;
    }
    poll->reading = response.reading;
    poll->following = response.reading && poll->followups < 20U;
    if (!poll->defer_publication) note4_magpie_poll_accept(poll, completed);
    return true;
}
