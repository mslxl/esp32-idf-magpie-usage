#include "magpie_client.h"
#include "resource_budget.h"
#include "external_memory.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char host[NOTE4_MAGPIE_ENDPOINT_MAX + 1U];
    char authority[NOTE4_MAGPIE_ENDPOINT_MAX + 1U];
    char path[NOTE4_MAGPIE_ENDPOINT_MAX + 2U];
    unsigned port;
    bool secure;
} address_t;

typedef struct {
    const note4_magpie_io_t *io;
    uint64_t start, progress;
    uint8_t buffer[512];
    size_t offset, length, header_bytes;
    note4_magpie_result_t error;
} stream_t;

static bool equal_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return false;
    }
    return *a == *b;
}

static char *trim(char *value) {
    while (*value == ' ' || *value == '\t') ++value;
    size_t length = strlen(value);
    while (length && (value[length - 1U] == ' ' || value[length - 1U] == '\t')) value[--length] = 0;
    return value;
}

static bool decimal(const char *text, uint64_t *value) {
    if (*text == 0) return false;
    uint64_t n = 0U;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9' || n > (UINT64_MAX - (unsigned)(*text - '0')) / 10U) return false;
        n = n * 10U + (unsigned)(*text - '0');
    }
    *value = n;
    return true;
}

bool note4_magpie_http_date(const char *value, int64_t *epoch) {
    char weekday[4], month[4], zone[4], extra;
    int day, year, hour, minute, second;
    if (sscanf(value, "%3[A-Za-z], %d %3s %d %d:%d:%d %3s%c", weekday, &day, month, &year,
        &hour, &minute, &second, zone, &extra) != 8 || strlen(weekday) != 3U || strcmp(zone, "GMT") != 0 ||
        year < 1970 || year > 9999 || day < 1 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return false;
    static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    unsigned m = 0U;
    for (; m < 12U && strcmp(month, months[m]) != 0; ++m) {}
    if (m == 12U) return false;
    static const unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if ((unsigned)day > days[m] + (m == 1U && leap ? 1U : 0U)) return false;
    int y = year - (m < 2U ? 1 : 0);
    const int era = y / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned mp = m < 2U ? m + 10U : m - 2U;
    const unsigned doy = (153U * mp + 2U) / 5U + (unsigned)day - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    *epoch = ((int64_t)era * 146097 + doe - 719468) * 86400 + hour * 3600 + minute * 60 + second;
    return true;
}

static bool address(const note4_settings_t *settings, address_t *result) {
    if (!note4_settings_validate(settings) || settings->magpie.endpoint[0] == 0 || settings->magpie.key[0] == 0) return false;
    char canonical[NOTE4_MAGPIE_ENDPOINT_MAX + 1U];
    if (!note4_magpie_endpoint_canonicalize(settings->magpie.endpoint, canonical, sizeof(canonical))) return false;
    memset(result, 0, sizeof(*result));
    result->secure = strncmp(canonical, "https://", 8U) == 0;
    result->port = result->secure ? 443U : 80U;
    const char *host = canonical + (result->secure ? 8U : 7U);
    const char *end = strchr(host, '/');
    if (end == NULL) end = host + strlen(host);
    memcpy(result->authority, host, (size_t)(end - host));
    const char *port = NULL, *host_end = end;
    if (*host == '[') {
        ++host;
        host_end = strchr(host, ']');
        if (host_end == NULL) return false;
        if (host_end + 1U < end) port = host_end + 2U;
    } else {
        const char *colon = memchr(host, ':', (size_t)(end - host));
        if (colon != NULL) { host_end = colon; port = colon + 1U; }
    }
    memcpy(result->host, host, (size_t)(host_end - host));
    if (port != NULL) {
        result->port = 0U;
        for (const char *p = port; p < end; ++p) result->port = result->port * 10U + (unsigned)(*p - '0');
    }
    memcpy(result->path, end, strlen(end) + 1U);
    return true;
}

static bool alive(stream_t *stream) {
    if (stream->error != NOTE4_MAGPIE_OK) return false;
    const note4_magpie_io_t *io = stream->io;
    if (io->cancelled(io->context)) stream->error = NOTE4_MAGPIE_CANCELLED;
    else {
        const uint64_t now = io->now_ms(io->context);
        if (now - stream->start >= 30000U || now - stream->progress >= 15000U) stream->error = NOTE4_MAGPIE_TIMEOUT;
    }
    return stream->error == NOTE4_MAGPIE_OK;
}

/* Refills the buffer; alive() runs once per refill attempt, not per byte. */
static bool refill(stream_t *stream) {
    while (alive(stream)) {
        const int n = stream->io->read(stream->io->context, stream->buffer, sizeof(stream->buffer));
        if (n == -2) { stream->io->wait(stream->io->context); continue; }
        if (n <= 0) { stream->error = n == -4 ? NOTE4_MAGPIE_RESOURCE_EXHAUSTED : NOTE4_MAGPIE_NETWORK; return false; }
        stream->offset = 0U; stream->length = (size_t)n;
        stream->progress = stream->io->now_ms(stream->io->context);
        return true;
    }
    return false;
}

static int byte(stream_t *stream) {
    if (stream->offset == stream->length && !refill(stream)) return -1;
    return stream->buffer[stream->offset++];
}

/* Copies up to length buffered bytes, refilling as needed; returns bytes copied. */
static size_t block(stream_t *stream, char *output, size_t length) {
    if (stream->offset == stream->length && !refill(stream)) return 0U;
    size_t n = stream->length - stream->offset;
    if (n > length) n = length;
    if (output != NULL) memcpy(output, stream->buffer + stream->offset, n);
    stream->offset += n;
    return n;
}

static bool line(stream_t *stream, char *output, size_t capacity, bool header) {
    size_t length = 0U;
    for (;;) {
        const int c = byte(stream);
        if (c < 0) return false;
        if (header && ++stream->header_bytes > 8192U) break;
        if (c == '\r') {
            const int lf = byte(stream);
            if (header && ++stream->header_bytes > 8192U) break;
            if (lf != '\n') break;
            output[length] = 0;
            return true;
        }
        if (c == '\n' || c == 0 || (c < 32 && c != '\t') || c == 127 || length + 1U >= capacity) break;
        output[length++] = (char)c;
    }
    stream->error = NOTE4_MAGPIE_INVALID_RESPONSE;
    return false;
}

static size_t form_encode(const char *input, char *output) {
    static const char hex[] = "0123456789ABCDEF";
    size_t offset = 0U;
    for (const unsigned char *p = (const unsigned char *)input; *p; ++p) {
        if (isalnum(*p) || *p == '-' || *p == '.' || *p == '_' || *p == '~') output[offset++] = (char)*p;
        else { output[offset++] = '%'; output[offset++] = hex[*p >> 4U]; output[offset++] = hex[*p & 15U]; }
    }
    output[offset] = 0;
    return offset;
}

static bool cookie(char *value, const address_t *url, const char *key, int64_t now,
                   note4_magpie_session_t *candidate, bool *seen) {
    char *attributes = strchr(value, ';');
    if (attributes != NULL) *attributes++ = 0;
    char *pair = trim(value), *equals = strchr(pair, '=');
    if (equals == NULL) return false;
    *equals++ = 0;
    if (strncmp(pair, "magpie_web_", 11U) != 0) return true;
    const size_t name_length = strlen(pair);
    uint64_t internal_port;
    if (name_length > 32U || !decimal(pair + 11U, &internal_port) || internal_port < 1U || internal_port > 65535U ||
        strlen(equals) > NOTE4_MAGPIE_KEY_MAX || strcmp(equals, key) != 0) return false;
    bool path_seen = false, domain_seen = false, expires_seen = false, age_seen = false, secure_seen = false;
    int64_t expires = 0;
    while (attributes != NULL) {
        char *next = strchr(attributes, ';');
        if (next != NULL) *next++ = 0;
        char *field = trim(attributes), *argument = strchr(field, '=');
        if (argument != NULL) { *argument++ = 0; argument = trim(argument); }
        if (equal_ci(field, "Secure")) {
            if (secure_seen || !url->secure || argument != NULL) return false;
            secure_seen = true;
        } else if (equal_ci(field, "Path")) {
            if (path_seen || argument == NULL || argument[0] != '/') return false;
            path_seen = true;
            char quota_path[NOTE4_MAGPIE_ENDPOINT_MAX + 32U];
            (void)snprintf(quota_path, sizeof(quota_path), "%s/api/usage/quotas", url->path);
            const size_t n = strlen(argument);
            if (strncmp(argument, quota_path, n) != 0 || (argument[n - 1U] != '/' && quota_path[n] != 0 && quota_path[n] != '/')) return false;
        } else if (equal_ci(field, "Domain")) {
            if (domain_seen || argument == NULL) return false;
            domain_seen = true;
            if (*argument == '.') ++argument;
            if (!equal_ci(argument, url->host)) return false;
        } else if (equal_ci(field, "Max-Age")) {
            uint64_t age;
            if (age_seen || argument == NULL || !decimal(argument, &age) || age == 0U ||
                now <= 0 || age > (uint64_t)(INT64_MAX - now)) return false;
            age_seen = true; expires = now + (int64_t)age;
        } else if (equal_ci(field, "Expires")) {
            int64_t expiry;
            if (expires_seen || argument == NULL || !note4_magpie_http_date(argument, &expiry) || now <= 0 || expiry <= now) return false;
            expires_seen = true;
            if (!age_seen) expires = expiry;
        }
        attributes = next;
    }
    /* Without Path the RFC default is /<prefix>/_magpie, which cannot cover /api. */
    if (!path_seen) return false;
    if (*seen && (strcmp(candidate->cookie_name, pair) != 0 || strcmp(candidate->cookie_value, equals) != 0 ||
                  candidate->expires_at != expires)) return false;
    memcpy(candidate->cookie_name, pair, name_length + 1U);
    memcpy(candidate->cookie_value, equals, strlen(equals) + 1U);
    candidate->expires_at = expires;
    *seen = true;
    return true;
}

static note4_magpie_result_t exchange(const note4_settings_t *settings, const note4_magpie_io_t *io,
    int64_t wall_time, note4_magpie_session_t *session, note4_magpie_response_t *response, bool login) {
    address_t url;
    if (!address(settings, &url)) return NOTE4_MAGPIE_CONFIG_REQUIRED;
    if (!login && (strcmp(session->endpoint, settings->magpie.endpoint) != 0 || session->cookie_name[0] == 0 ||
        (session->expires_at && session->expires_at <= wall_time))) return NOTE4_MAGPIE_AUTH_REQUIRED;
    stream_t stream = {.io = io, .start = io->now_ms(io->context), .progress = io->now_ms(io->context)};
    note4_magpie_session_t candidate = {0};
    bool seen_cookie = false;
    char *request = note4_external_calloc(1U, 4096U);
    char *body = note4_external_calloc(1U, 3U * NOTE4_MAGPIE_ENDPOINT_MAX + 3U * NOTE4_MAGPIE_KEY_MAX + 64U);
    if (request == NULL || body == NULL) { free(request); free(body); return NOTE4_MAGPIE_RESOURCE_EXHAUSTED; }
    if (login) {
        char back[NOTE4_MAGPIE_ENDPOINT_MAX + 2U];
        const size_t path_length = strlen(url.path);
        if (path_length > sizeof(back) - 2U) {
            free(request); free(body); return NOTE4_MAGPIE_CONFIG_REQUIRED;
        }
        memcpy(back, url.path, path_length);
        back[path_length] = '/'; back[path_length + 1U] = 0;
        memcpy(body, "key=", 4U);
        size_t n = 4U + form_encode(settings->magpie.key, body + 4U);
        memcpy(body + n, "&back=", 6U); n += 6U;
        (void)form_encode(back, body + n);
        (void)snprintf(request, 4096U, "POST %s/_magpie/sign-in HTTP/1.1\r\nHost: %s\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %u\r\n\r\n%s",
            url.path, url.authority, (unsigned)strlen(body), body);
    } else {
        (void)snprintf(request, 4096U, "GET %s/api/usage/quotas HTTP/1.1\r\nHost: %s\r\nAccept: application/json\r\nAccept-Encoding: identity\r\nCookie: %s=%s\r\n\r\n",
            url.path, url.authority, session->cookie_name, session->cookie_value);
    }
    while (alive(&stream)) {
        const int rc = io->open(io->context, url.host, url.port, url.secure);
        if (rc == 1) break;
        if (rc < 0) { stream.error = rc == -3 ? NOTE4_MAGPIE_CERTIFICATE : rc == -4 ? NOTE4_MAGPIE_RESOURCE_EXHAUSTED : NOTE4_MAGPIE_NETWORK; break; }
        io->wait(io->context);
    }
    size_t written = 0U, request_length = strlen(request);
    while (written < request_length && alive(&stream)) {
        const int n = io->write(io->context, request + written, request_length - written);
        if (n == -2) { io->wait(io->context); continue; }
        if (n <= 0) { stream.error = n == -4 ? NOTE4_MAGPIE_RESOURCE_EXHAUSTED : NOTE4_MAGPIE_NETWORK; break; }
        written += (size_t)n; stream.progress = io->now_ms(io->context);
    }
    memset(request, 0, 4096U); free(request);
    memset(body, 0, 3U * NOTE4_MAGPIE_ENDPOINT_MAX + 3U * NOTE4_MAGPIE_KEY_MAX + 64U); free(body);
    char header[2049];
    unsigned code = 0U;
    if (alive(&stream) && line(&stream, header, sizeof(header), true)) {
        if ((strncmp(header, "HTTP/1.1 ", 9U) != 0 && strncmp(header, "HTTP/1.0 ", 9U) != 0) ||
            strlen(header) < 12U || header[9] < '1' || header[9] > '5' || header[10] < '0' || header[10] > '9' ||
            header[11] < '0' || header[11] > '9' || (header[12] && header[12] != ' ')) stream.error = NOTE4_MAGPIE_INVALID_RESPONSE;
        else code = (unsigned)(header[9] - '0') * 100U + (unsigned)(header[10] - '0') * 10U + (unsigned)(header[11] - '0');
    }
    bool json = false, content_type_seen = false, length_seen = false, chunked = false, encoding_seen = false, reading_seen = false;
    uint64_t content_length = 0U;
    bool retry_seen = false, retry_invalid = false, date_seen = false, date_invalid = false;
    bool server_closes = code == 0U || strncmp(header, "HTTP/1.1 ", 9U) != 0;
    while (alive(&stream) && line(&stream, header, sizeof(header), true) && header[0]) {
        char *colon = strchr(header, ':');
        if (colon == NULL || colon == header) { stream.error = NOTE4_MAGPIE_INVALID_RESPONSE; break; }
        *colon++ = 0;
        for (const char *p = header; *p; ++p) if (!isalnum((unsigned char)*p) && *p != '-') stream.error = NOTE4_MAGPIE_INVALID_RESPONSE;
        char *value = trim(colon);
        if (equal_ci(header, "Connection")) {
            if (equal_ci(value, "close")) server_closes = true;
        } else if (equal_ci(header, "Set-Cookie") && login) {
            if (!cookie(value, &url, settings->magpie.key, wall_time, &candidate, &seen_cookie)) stream.error = NOTE4_MAGPIE_INVALID_RESPONSE;
        } else if (equal_ci(header, "Content-Type")) {
            if (content_type_seen) { stream.error = NOTE4_MAGPIE_INVALID_RESPONSE; break; }
            content_type_seen = true;
            char *semicolon = strchr(value, ';');
            if (semicolon != NULL) *semicolon = 0;
            json = equal_ci(trim(value), "application/json");
        } else if (equal_ci(header, "Content-Length")) {
            if (length_seen || !decimal(value, &content_length)) stream.error = NOTE4_MAGPIE_INVALID_RESPONSE;
            length_seen = true;
        } else if (equal_ci(header, "Transfer-Encoding")) {
            if (chunked || !equal_ci(value, "chunked")) stream.error = NOTE4_MAGPIE_INVALID_RESPONSE;
            chunked = true;
        } else if (equal_ci(header, "Content-Encoding")) {
            if (encoding_seen || !equal_ci(value, "identity")) stream.error = NOTE4_MAGPIE_INVALID_RESPONSE;
            encoding_seen = true;
        } else if (equal_ci(header, "X-Magpie-Reading") && response != NULL) {
            if (reading_seen) stream.error = NOTE4_MAGPIE_INVALID_RESPONSE;
            reading_seen = true; response->reading = strcmp(value, "1") == 0;
        } else if (equal_ci(header, "Retry-After") && response != NULL) {
            if (retry_invalid || strlen(value) >= sizeof(response->retry_after) ||
                (retry_seen && strcmp(response->retry_after, value) != 0)) {
                retry_invalid = true; response->retry_after[0] = 0;
            } else memcpy(response->retry_after, value, strlen(value) + 1U);
            retry_seen = true;
        } else if (equal_ci(header, "Date") && response != NULL) {
            if (date_invalid || strlen(value) >= sizeof(response->date) ||
                (date_seen && strcmp(response->date, value) != 0)) {
                date_invalid = true; response->date[0] = 0;
            } else memcpy(response->date, value, strlen(value) + 1U);
            date_seen = true;
        }
    }
    if (length_seen && chunked) stream.error = NOTE4_MAGPIE_INVALID_RESPONSE;
    if (response != NULL) response->status = code;
    if (alive(&stream)) {
        if (code == 401U) stream.error = NOTE4_MAGPIE_AUTH_REQUIRED;
        else if (code == 403U) stream.error = NOTE4_MAGPIE_FORBIDDEN;
        else if (code == 429U) stream.error = NOTE4_MAGPIE_RATE_LIMITED;
        else if (login) {
            if (code != 303U || !seen_cookie) stream.error = NOTE4_MAGPIE_INVALID_RESPONSE;
            else {
                memcpy(candidate.endpoint, settings->magpie.endpoint, strlen(settings->magpie.endpoint) + 1U);
                *session = candidate;
            }
        } else if (code != 200U || !json || (!length_seen && !chunked)) stream.error = NOTE4_MAGPIE_INVALID_RESPONSE;
    }
    if (!login && alive(&stream)) {
        size_t capacity = 0U;
        uint64_t remaining = content_length;
        bool complete = false;
        while (alive(&stream) && !complete) {
            if (chunked) {
                if (!line(&stream, header, sizeof(header), false)) break;
                remaining = 0U;
                if (!header[0]) { stream.error = NOTE4_MAGPIE_INVALID_RESPONSE; break; }
                for (const char *p = header; *p; ++p) {
                    unsigned digit;
                    if (*p >= '0' && *p <= '9') digit = (unsigned)(*p - '0');
                    else if (*p >= 'a' && *p <= 'f') digit = (unsigned)(*p - 'a') + 10U;
                    else if (*p >= 'A' && *p <= 'F') digit = (unsigned)(*p - 'A') + 10U;
                    else { stream.error = NOTE4_MAGPIE_INVALID_RESPONSE; break; }
                    if (remaining > (UINT64_MAX - digit) / 16U) { stream.error = NOTE4_MAGPIE_INVALID_RESPONSE; break; }
                    remaining = remaining * 16U + digit;
                }
                if (!remaining && alive(&stream)) {
                    if (!line(&stream, header, sizeof(header), true) || header[0]) stream.error = NOTE4_MAGPIE_INVALID_RESPONSE;
                    break;
                }
            }
            if (remaining > SIZE_MAX - response->length - 1U || response->length >= NOTE4_RESPONSE_BUDGET ||
                remaining >= NOTE4_RESPONSE_BUDGET - response->length) { stream.error = NOTE4_MAGPIE_RESOURCE_EXHAUSTED; break; }
            while (remaining && alive(&stream)) {
                if (response->length + 1U >= capacity) {
                    size_t next_capacity = capacity ? capacity + capacity / 2U : 1024U;
                    if (next_capacity > NOTE4_RESPONSE_BUDGET) next_capacity = NOTE4_RESPONSE_BUDGET;
                    if (next_capacity <= capacity || !note4_resource_available(next_capacity)) { stream.error = NOTE4_MAGPIE_RESOURCE_EXHAUSTED; break; }
                    char *next = note4_external_realloc(response->body, next_capacity);
                    if (next == NULL) { stream.error = NOTE4_MAGPIE_RESOURCE_EXHAUSTED; break; }
                    response->body = next; capacity = next_capacity;
                }
                size_t want = capacity - 1U - response->length;
                if (want > remaining) want = (size_t)remaining;
                const size_t n = block(&stream, response->body + response->length, want);
                if (n == 0U) break;
                response->length += n;
                remaining -= n;
            }
            if (chunked && alive(&stream) && (byte(&stream) != '\r' || byte(&stream) != '\n')) stream.error = NOTE4_MAGPIE_INVALID_RESPONSE;
            if (!chunked) complete = true;
        }
        if (alive(&stream)) {
            if (response->body == NULL) {
                response->body = note4_external_calloc(1U, 1U);
                if (response->body == NULL) stream.error = NOTE4_MAGPIE_RESOURCE_EXHAUSTED;
            } else response->body[response->length] = 0;
        }
    }
    if (login && alive(&stream) && !chunked && length_seen && content_length <= 4096U) {
        /* Drain a small sign-in body so the connection can carry the next request. */
        for (uint64_t left = content_length; left && alive(&stream);) {
            const size_t n = block(&stream, NULL, left > SIZE_MAX ? SIZE_MAX : (size_t)left);
            if (n == 0U) break;
            left -= n;
        }
    } else if (login) server_closes = true;
    const bool reusable = alive(&stream) && !server_closes && stream.offset == stream.length;
    if (io->release != NULL) io->release(io->context, reusable);
    else io->close(io->context);
    memset(&candidate, 0, sizeof(candidate));
    if (stream.error != NOTE4_MAGPIE_OK && response != NULL) {
        free(response->body); response->body = NULL; response->length = 0U;
    }
    return stream.error;
}

note4_magpie_result_t note4_magpie_login(const note4_settings_t *settings,
    const note4_magpie_io_t *io, int64_t wall_time, note4_magpie_session_t *session) {
    memset(session, 0, sizeof(*session));
    return exchange(settings, io, wall_time, session, NULL, true);
}

note4_magpie_result_t note4_magpie_get_quotas(const note4_settings_t *settings,
    const note4_magpie_io_t *io, int64_t wall_time, note4_magpie_session_t *session,
    note4_magpie_response_t *response) {
    memset(response, 0, sizeof(*response));
    return exchange(settings, io, wall_time, session, response, false);
}

note4_magpie_result_t note4_magpie_read_snapshot(const note4_settings_t *settings,
    const note4_magpie_io_t *io, int64_t wall_time, note4_magpie_session_t *session,
    note4_magpie_response_t *response, note4_quota_snapshot_t **snapshot) {
    note4_magpie_result_t result = note4_magpie_get_quotas(settings, io, wall_time, session, response);
    if (result == NOTE4_MAGPIE_OK) {
        const note4_quota_result_t parsed = note4_quota_snapshot_parse(response->body, response->length, snapshot);
        if (parsed != NOTE4_QUOTA_OK) result = parsed == NOTE4_QUOTA_NO_MEMORY
            ? NOTE4_MAGPIE_RESOURCE_EXHAUSTED : NOTE4_MAGPIE_INVALID_RESPONSE;
    }
    free(response->body);
    response->body = NULL;
    response->length = 0U;
    return result;
}

void note4_magpie_response_clear(note4_magpie_response_t *response) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}
