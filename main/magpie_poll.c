#include "magpie_poll.h"
#include <limits.h>
#include <string.h>

static uint64_t add(uint64_t a, uint64_t b) { return a > UINT64_MAX - b ? UINT64_MAX : a + b; }
static uint64_t interval(const note4_magpie_poll_t *poll) { return (uint64_t)poll->settings.web.interval_seconds * 1000U; }
static void advance_normal(note4_magpie_poll_t *poll, uint64_t now) {
    const uint64_t step = interval(poll);
    if (poll->normal_due <= now && step) {
        const uint64_t missed = (now - poll->normal_due) / step + 1U;
        poll->normal_due = missed > (UINT64_MAX - poll->normal_due) / step
            ? UINT64_MAX : poll->normal_due + missed * step;
    }
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
        poll->paused_at = now;
    } else if (poll->mode == NOTE4_POLL_SETUP && mode != NOTE4_POLL_SETUP && poll->failed) {
        poll->failed_at = add(poll->failed_at, now - poll->paused_at);
    }
    if (mode == NOTE4_POLL_SETUP && poll->mode != NOTE4_POLL_SETUP) poll->paused_at = now;
    const bool credentials_changed = strcmp(poll->settings.magpie.key, settings->magpie.key) != 0 ||
        poll->settings.web.allow_plain_http != settings->web.allow_plain_http;
    if (instance_changed || credentials_changed || (saved && poll->halted)) {
        memset(&poll->session, 0, sizeof(poll->session));
        poll->relogin_used = false; poll->halted = false; poll->login_pending = true;
    }
    if (saved) poll->relogin_used = false;
    poll->settings = *settings;
    poll->mode = mode; poll->following = false; poll->followups = 0U;
    if (mode == NOTE4_POLL_OFFLINE && !poll->halted) {
        if (!poll->failed) { poll->failed = true; poll->failed_at = now; }
        poll->result = NOTE4_MAGPIE_NETWORK;
    }
    if (mode == NOTE4_POLL_ONLINE) {
        if (!poll->failed || poll->next_due < now) poll->next_due = now;
        if (poll->retry_after > poll->next_due) poll->next_due = poll->retry_after;
        poll->normal_due = poll->next_due;
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
    if (!poll->failed) { poll->failed = true; poll->failed_at = now; }
    poll->result = result; poll->following = false; poll->reading = false;
    if (result == NOTE4_MAGPIE_AUTH_REQUIRED || result == NOTE4_MAGPIE_FORBIDDEN ||
        result == NOTE4_MAGPIE_CERTIFICATE || result == NOTE4_MAGPIE_CONFIG_REQUIRED) {
        poll->halted = true;
        memset(&poll->session, 0, sizeof(poll->session));
        return;
    }
    if (poll->failures != UINT32_MAX) ++poll->failures;
    const uint64_t base = interval(poll), cap = base > 900000U ? base : 900000U;
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
        if (!poll->following) { poll->followups = 0U; advance_normal(poll, started); }
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
    if (!login && result == NOTE4_MAGPIE_OK && (poll->defer_publication
            ? note4_quota_order_prepare(&poll->order, *snapshot, &poll->pending_order)
            : note4_quota_order_observe(&poll->order, *snapshot)) != NOTE4_QUOTA_OK) {
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
    if (!poll->defer_publication) {
        poll->result = NOTE4_MAGPIE_OK; poll->failed = false; poll->failures = 0U;
        poll->has_snapshot = true;
    }
    poll->reading = response.reading;
    advance_normal(poll, completed);
    poll->following = response.reading && poll->followups < 20U;
    poll->next_due = poll->following ? add(completed, 1500U) : poll->normal_due;
    return true;
}

bool note4_magpie_poll_error_page(const note4_magpie_poll_t *poll, uint64_t now) {
    return poll->failed && poll->mode != NOTE4_POLL_SETUP && now >= poll->failed_at && now - poll->failed_at >= 120000U;
}
