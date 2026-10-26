#include "display_prefs.h"
#include "resource_budget.h"
#include "external_memory.h"
#include <stdlib.h>
#include <string.h>
#include "casefold_data.h"

typedef struct { const unsigned char *source; const char *replacement; unsigned remaining; } fold_cursor_t;
static unsigned fold_byte(fold_cursor_t *cursor) {
    if (cursor->replacement != NULL && *cursor->replacement) return (unsigned char)*cursor->replacement++;
    cursor->replacement = NULL;
    if (!*cursor->source) return 0U;
    if (cursor->remaining) { --cursor->remaining; return *cursor->source++; }
    const unsigned char *start = cursor->source;
    uint32_t cp = *cursor->source++;
    unsigned continuation = cp < 128U ? 0U : cp < 224U ? 1U : cp < 240U ? 2U : 3U;
    if (continuation) {
        cp &= continuation == 1U ? 31U : continuation == 2U ? 15U : 7U;
        for (unsigned i = 0U; i < continuation; ++i) cp = (cp << 6U) | (*cursor->source++ & 63U);
    }
    size_t lo = 0U, hi = sizeof(NOTE4_CASEFOLD) / sizeof(NOTE4_CASEFOLD[0]);
    while (lo < hi) { const size_t mid = lo + (hi - lo) / 2U; if (NOTE4_CASEFOLD[mid].codepoint < cp) lo = mid + 1U; else hi = mid; }
    if (lo < sizeof(NOTE4_CASEFOLD) / sizeof(NOTE4_CASEFOLD[0]) && NOTE4_CASEFOLD[lo].codepoint == cp) {
        cursor->replacement = NOTE4_CASEFOLD[lo].replacement;
        return (unsigned char)*cursor->replacement++;
    }
    cursor->source = start + 1U; cursor->remaining = continuation;
    return *start;
}
static bool contains_folded(const char *value, const char *query) {
    fold_cursor_t at = {.source = (const unsigned char *)value};
    for (;;) {
        fold_cursor_t haystack = at, needle = {.source = (const unsigned char *)query};
        unsigned next;
        while ((next = fold_byte(&needle)) != 0U) if (fold_byte(&haystack) != next) break;
        if (!next) return true;
        if (!fold_byte(&at)) return false;
    }
}

static bool query_space(uint32_t cp) {
    return (cp >= 9U && cp <= 13U) || cp == 0x20U || cp == 0xa0U || cp == 0x1680U ||
        (cp >= 0x2000U && cp <= 0x200aU) || cp == 0x2028U || cp == 0x2029U ||
        cp == 0x202fU || cp == 0x205fU || cp == 0x3000U || cp == 0xfeffU;
}
static const char *trim_query(char *value) {
    char *cursor = value, *first = NULL, *end = value;
    while (*cursor) {
        char *start = cursor;
        uint32_t cp = (unsigned char)*cursor++;
        unsigned n = cp < 128U ? 0U : cp < 224U ? 1U : cp < 240U ? 2U : 3U;
        if (n) {
            cp &= n == 1U ? 31U : n == 2U ? 15U : 7U;
            while (n--) cp = (cp << 6U) | ((unsigned char)*cursor++ & 63U);
        }
        if (!query_space(cp)) { if (first == NULL) first = start; end = cursor; }
    }
    *end = 0;
    return first != NULL ? first : value;
}

typedef struct { const uint8_t *bytes; size_t length, offset, used; char *text; bool valid; } reader_t;
static uint32_t read32(reader_t *r) {
    if (r->offset > r->length || r->length - r->offset < 4U) { r->valid = false; return 0U; }
    const uint8_t *p = r->bytes + r->offset; r->offset += 4U;
    return p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}
static bool utf8(const uint8_t *bytes, size_t length) {
    for (size_t i = 0U; i < length;) {
        const uint8_t first = bytes[i++];
        if (first == 0U) return false;
        if (first < 128U) continue;
        unsigned n; uint32_t cp, minimum;
        if (first >= 0xc2U && first <= 0xdfU) { n = 1U; cp = first & 31U; minimum = 0x80U; }
        else if (first >= 0xe0U && first <= 0xefU) { n = 2U; cp = first & 15U; minimum = 0x800U; }
        else if (first >= 0xf0U && first <= 0xf4U) { n = 3U; cp = first & 7U; minimum = 0x10000U; }
        else return false;
        if (n > length - i) return false;
        while (n--) { const uint8_t next = bytes[i++]; if ((next & 0xc0U) != 0x80U) return false; cp = (cp << 6U) | (next & 63U); }
        if (cp < minimum || cp > 0x10ffffU || (cp >= 0xd800U && cp <= 0xdfffU)) return false;
    }
    return true;
}
static const char *text(reader_t *r) {
    const uint32_t size = read32(r);
    if (!r->valid || size > r->length - r->offset || !utf8(r->bytes + r->offset, size)) { r->valid = false; return ""; }
    char *value = r->text + r->used;
    memcpy(value, r->bytes + r->offset, size); value[size] = 0;
    r->used += (size_t)size + 1U; r->offset += size;
    return value;
}
void note4_display_prefs_retain(note4_display_prefs_t *prefs) {
    if (prefs != NULL) atomic_fetch_add(&prefs->references, 1U);
}
void note4_display_prefs_release(note4_display_prefs_t *prefs) {
    if (prefs == NULL || atomic_fetch_sub(&prefs->references, 1U) != 1U) return;
    free(prefs->providers); free(prefs->hidden); free(prefs->custom);
    free(prefs->bytes); free(prefs->text); free(prefs);
}
static bool identity_equal(const note4_account_identity_t *a, const note4_account_identity_t *b) {
    return !strcmp(a->provider, b->provider) && !strcmp(a->user, b->user);
}
static note4_ble_status_t identities(reader_t *r, note4_account_identity_t **output, uint32_t *count) {
    *count = read32(r);
    if (!r->valid || *count > (r->length - r->offset) / 8U || *count > SIZE_MAX / sizeof(**output)) return NOTE4_BLE_ERR_VALIDATION;
    if (!*count) return NOTE4_BLE_OK;
    *output = note4_external_calloc(*count, sizeof(**output));
    if (*output == NULL) return NOTE4_BLE_ERR_RESOURCE_EXHAUSTED;
    for (uint32_t i = 0U; i < *count; ++i) {
        (*output)[i].provider = text(r); (*output)[i].user = text(r);
        if (!r->valid || !(*output)[i].provider[0]) return NOTE4_BLE_ERR_VALIDATION;
        for (uint32_t j = 0U; j < i; ++j) if (identity_equal(&(*output)[i], &(*output)[j])) return NOTE4_BLE_ERR_VALIDATION;
    }
    return NOTE4_BLE_OK;
}
note4_ble_status_t note4_display_prefs_parse(const uint8_t *bytes, size_t length, note4_display_prefs_t **output) {
    if (bytes == NULL || output == NULL || length < 20U || length > UINT32_MAX || length == SIZE_MAX) return NOTE4_BLE_ERR_VALIDATION;
    if (length > NOTE4_DOCUMENT_BUDGET || !note4_resource_available(length * 4U + sizeof(note4_display_prefs_t)))
        return NOTE4_BLE_ERR_RESOURCE_EXHAUSTED;
    if (bytes[0] != 2U) return NOTE4_BLE_ERR_UNSUPPORTED_SCHEMA;
    if (bytes[1] > 2U || bytes[2] > 1U || bytes[3] > 15U) return NOTE4_BLE_ERR_VALIDATION;
    note4_display_prefs_t *prefs = note4_external_calloc(1U, sizeof(*prefs));
    if (prefs == NULL) return NOTE4_BLE_ERR_RESOURCE_EXHAUSTED;
    atomic_init(&prefs->references, 1U); prefs->bytes = note4_external_malloc(length); prefs->text = note4_external_malloc(length + 1U);
    note4_ble_status_t status = NOTE4_BLE_ERR_RESOURCE_EXHAUSTED;
    if (prefs->bytes == NULL || prefs->text == NULL) goto fail;
    memcpy(prefs->bytes, bytes, length); prefs->length = (uint32_t)length;
    prefs->order = bytes[1]; prefs->provider_mode = bytes[2]; prefs->kinds = bytes[3];
    reader_t r = {.bytes = bytes, .length = length, .offset = 4U, .text = prefs->text, .valid = true};
    prefs->query = text(&r); prefs->provider_count = read32(&r);
    status = NOTE4_BLE_ERR_VALIDATION;
    if (!r.valid || prefs->provider_count > (length - r.offset) / 4U || prefs->provider_count > SIZE_MAX / sizeof(*prefs->providers)) goto fail;
    if (prefs->provider_count) {
        prefs->providers = note4_external_calloc(prefs->provider_count, sizeof(*prefs->providers));
        if (prefs->providers == NULL) { status = NOTE4_BLE_ERR_RESOURCE_EXHAUSTED; goto fail; }
    }
    for (uint32_t i = 0U; i < prefs->provider_count; ++i) {
        prefs->providers[i] = text(&r);
        if (!r.valid || !prefs->providers[i][0]) goto fail;
        for (uint32_t j = 0U; j < i; ++j) if (!strcmp(prefs->providers[i], prefs->providers[j])) goto fail;
    }
    status = identities(&r, &prefs->hidden, &prefs->hidden_count); if (status != NOTE4_BLE_OK) goto fail;
    status = identities(&r, &prefs->custom, &prefs->custom_count); if (status != NOTE4_BLE_OK) goto fail;
    if (!r.valid || r.offset != length) { status = NOTE4_BLE_ERR_VALIDATION; goto fail; }
    prefs->query = trim_query((char *)prefs->query);
    *output = prefs; return NOTE4_BLE_OK;
fail:
    note4_display_prefs_release(prefs); return status;
}
bool note4_display_prefs_hidden(const note4_display_prefs_t *prefs, const char *provider, const char *user) {
    if (prefs == NULL) return false;
    const note4_account_identity_t identity = {provider, user};
    for (uint32_t i = 0U; i < prefs->hidden_count; ++i) if (identity_equal(&prefs->hidden[i], &identity)) return true;
    return false;
}
bool note4_display_prefs_visible(const note4_display_prefs_t *prefs, const note4_quota_snapshot_t *snapshot, size_t card) {
    if (prefs == NULL) return true;
    const char *provider = note4_quota_text(snapshot, card, "provider"), *user = note4_quota_text(snapshot, card, "user");
    if (note4_display_prefs_hidden(prefs, provider, user)) return false;
    if (prefs->provider_mode) {
        uint32_t i = 0U; while (i < prefs->provider_count && strcmp(provider, prefs->providers[i])) ++i;
        if (i == prefs->provider_count) return false;
    }
    const char *kind = note4_quota_text(snapshot, card, "kind");
    unsigned bit = !strcmp(kind, "subscription") ? 1U : !strcmp(kind, "plan") ? 2U : !strcmp(kind, "balance") ? 4U : 8U;
    if (!kind[0] && note4_quota_text(snapshot, card, "balance")[0] && !note4_quota_window_count(snapshot, card)) bit = 4U;
    if (!(prefs->kinds & bit)) return false;
    if (!prefs->query[0]) return true;
    static const char *const fields[] = {"user", "alias", "seat", "name"};
    for (unsigned i = 0U; i < 4U; ++i) if (contains_folded(note4_quota_text(snapshot, card, fields[i]), prefs->query)) return true;
    return false;
}
size_t note4_display_prefs_rank(const note4_display_prefs_t *prefs, const char *provider, const char *user) {
    if (prefs == NULL || prefs->order != 1U) return 0U;
    const note4_account_identity_t identity = {provider, user};
    for (uint32_t i = 0U; i < prefs->custom_count; ++i) if (identity_equal(&prefs->custom[i], &identity)) return i;
    return prefs->custom_count;
}
void note4_display_prefs_upload_clear(note4_display_prefs_upload_t *upload) {
    free(upload->bytes); memset(upload, 0, sizeof(*upload));
}
note4_ble_status_t note4_display_prefs_begin(note4_display_prefs_upload_t *upload,
    uint32_t revision, uint32_t length, uint32_t crc, uint64_t now) {
    if (length < 20U) return NOTE4_BLE_ERR_LENGTH;
    if (length > NOTE4_DOCUMENT_BUDGET || !note4_resource_available(length)) return NOTE4_BLE_ERR_RESOURCE_EXHAUSTED;
    uint8_t *candidate = note4_external_malloc(length);
    if (candidate == NULL) return NOTE4_BLE_ERR_RESOURCE_EXHAUSTED;
    note4_display_prefs_upload_clear(upload);
    upload->bytes = candidate; upload->length = length; upload->crc = crc; upload->revision = revision; upload->active_at = now;
    return NOTE4_BLE_OK;
}
note4_ble_status_t note4_display_prefs_put(note4_display_prefs_upload_t *upload,
    uint32_t offset, const uint8_t *bytes, size_t length, uint64_t now) {
    if (upload->bytes == NULL) return NOTE4_BLE_ERR_STATE;
    if (now >= upload->active_at && now - upload->active_at >= 120000U) { note4_display_prefs_upload_clear(upload); return NOTE4_BLE_ERR_STATE; }
    if (!length || offset > upload->length || length > upload->length - offset) return NOTE4_BLE_ERR_LENGTH;
    if (offset < upload->received) {
        if (length > upload->received - offset || memcmp(upload->bytes + offset, bytes, length)) return NOTE4_BLE_ERR_SEQUENCE;
    } else {
        if (offset != upload->received) return NOTE4_BLE_ERR_SEQUENCE;
        memcpy(upload->bytes + offset, bytes, length); upload->received += (uint32_t)length;
    }
    upload->active_at = now; return NOTE4_BLE_OK;
}
note4_ble_status_t note4_display_prefs_finish(note4_display_prefs_upload_t *upload,
    uint32_t revision, uint64_t now, note4_display_prefs_t **output) {
    if (upload->bytes != NULL && now >= upload->active_at && now - upload->active_at >= 120000U) {
        note4_display_prefs_upload_clear(upload);
        return NOTE4_BLE_ERR_STATE;
    }
    if (revision != upload->revision) return NOTE4_BLE_ERR_STALE_REVISION;
    if (upload->bytes == NULL || upload->received != upload->length) return NOTE4_BLE_ERR_STATE;
    if (note4_account_directory_crc32(upload->bytes, upload->length) != upload->crc) return NOTE4_BLE_ERR_CRC;
    return note4_display_prefs_parse(upload->bytes, upload->length, output);
}
