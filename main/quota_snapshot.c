#include "quota_snapshot.h"
#include "resource_budget.h"

#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* Nodes own decoded UTF-8 strings and exact decimal tokens. Unknown fields remain
 * in the native tree; the guest only receives the allowlisted display fields. */
typedef enum { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT } json_type_t;
typedef struct json_node {
    json_type_t type;
    char *name, *text;
    struct json_node *child, *next;
} json_node_t;
struct note4_quota_snapshot {
    atomic_uint references;
    json_node_t *root;
    json_node_t **cards;
    size_t *smart_ranks;
    size_t count, bytes;
};
typedef struct { const unsigned char *p, *end; unsigned depth; size_t bytes; note4_quota_result_t error; } parser_t;
static void *parse_allocate(parser_t *parser, size_t bytes, bool zero) {
    if (bytes > NOTE4_MODEL_BUDGET - parser->bytes || !note4_resource_available(bytes)) {
        parser->error = NOTE4_QUOTA_NO_MEMORY; return NULL;
    }
    void *memory = zero ? calloc(1U, bytes) : malloc(bytes);
    if (memory == NULL) parser->error = NOTE4_QUOTA_NO_MEMORY;
    else parser->bytes += bytes;
    return memory;
}

static void destroy(json_node_t *node) {
    while (node != NULL) {
        json_node_t *next = node->next;
        destroy(node->child); free(node->name); free(node->text); free(node); node = next;
    }
}
static void whitespace(parser_t *parser) {
    while (parser->p < parser->end && (*parser->p == ' ' || *parser->p == '\r' || *parser->p == '\n' || *parser->p == '\t')) ++parser->p;
}
static unsigned hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10U;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10U;
    return 16U;
}
static bool scalar(parser_t *parser, uint32_t *codepoint) {
    if ((size_t)(parser->end - parser->p) < 4U) return false;
    *codepoint = 0U;
    for (unsigned i = 0; i < 4U; ++i) {
        const unsigned h = hex(*parser->p++);
        if (h == 16U) return false;
        *codepoint = *codepoint * 16U + h;
    }
    return true;
}
static char *string(parser_t *parser) {
    if (parser->p == parser->end || *parser->p++ != '"') return NULL;
    const unsigned char *start = parser->p;
    const unsigned char *scan = start;
    bool escaped = false;
    for (; scan < parser->end; ++scan) {
        if (!escaped && *scan == '"') break;
        if (!escaped && *scan == '\\') escaped = true;
        else escaped = false;
    }
    if (scan == parser->end) return NULL;
    char *text = parse_allocate(parser, (size_t)(scan - start) + 1U, false);
    if (text == NULL) { parser->error = NOTE4_QUOTA_NO_MEMORY; return NULL; }
    size_t length = 0U;
    while (parser->p < scan) {
        unsigned char c = *parser->p++;
        if (c < 32U) goto invalid;
        if (c == '\\') {
            if (parser->p >= scan) goto invalid;
            c = *parser->p++;
            if (c == '"' || c == '\\' || c == '/') text[length++] = (char)c;
            else if (c == 'b') text[length++] = '\b';
            else if (c == 'f') text[length++] = '\f';
            else if (c == 'n') text[length++] = '\n';
            else if (c == 'r') text[length++] = '\r';
            else if (c == 't') text[length++] = '\t';
            else if (c == 'u') {
                uint32_t cp;
                if (!scalar(parser, &cp) || cp == 0U) goto invalid;
                if (cp >= 0xd800U && cp <= 0xdbffU) {
                    uint32_t low;
                    if (parser->p + 2U > scan || parser->p[0] != '\\' || parser->p[1] != 'u') goto invalid;
                    parser->p += 2U;
                    if (!scalar(parser, &low) || low < 0xdc00U || low > 0xdfffU) goto invalid;
                    cp = 0x10000U + ((cp - 0xd800U) << 10U) + low - 0xdc00U;
                } else if (cp >= 0xdc00U && cp <= 0xdfffU) goto invalid;
                if (cp < 0x80U) text[length++] = (char)cp;
                else if (cp < 0x800U) {
                    text[length++] = (char)(0xc0U | (cp >> 6U)); text[length++] = (char)(0x80U | (cp & 63U));
                } else if (cp < 0x10000U) {
                    text[length++] = (char)(0xe0U | (cp >> 12U)); text[length++] = (char)(0x80U | ((cp >> 6U) & 63U)); text[length++] = (char)(0x80U | (cp & 63U));
                } else {
                    text[length++] = (char)(0xf0U | (cp >> 18U)); text[length++] = (char)(0x80U | ((cp >> 12U) & 63U));
                    text[length++] = (char)(0x80U | ((cp >> 6U) & 63U)); text[length++] = (char)(0x80U | (cp & 63U));
                }
            } else goto invalid;
        } else if (c < 0x80U) text[length++] = (char)c;
        else {
            unsigned continuation;
            uint32_t cp;
            if (c >= 0xc2U && c <= 0xdfU) { continuation = 1U; cp = c & 31U; }
            else if (c >= 0xe0U && c <= 0xefU) { continuation = 2U; cp = c & 15U; }
            else if (c >= 0xf0U && c <= 0xf4U) { continuation = 3U; cp = c & 7U; }
            else goto invalid;
            if ((size_t)(scan - parser->p) < continuation) goto invalid;
            const unsigned char *sequence = parser->p - 1U;
            for (unsigned i = 0; i < continuation; ++i) {
                unsigned char next = *parser->p++;
                if ((next & 0xc0U) != 0x80U) goto invalid;
                cp = (cp << 6U) | (next & 63U);
            }
            if ((continuation == 2U && cp < 0x800U) || (continuation == 3U && cp < 0x10000U) ||
                cp > 0x10ffffU || (cp >= 0xd800U && cp <= 0xdfffU)) goto invalid;
            memcpy(text + length, sequence, continuation + 1U); length += continuation + 1U;
        }
    }
    if (parser->p != scan) goto invalid;
    ++parser->p; text[length] = 0; return text;
invalid:
    free(text); return NULL;
}

static json_node_t *parse(parser_t *parser) {
    whitespace(parser);
    if (parser->p >= parser->end || parser->depth >= 64U) return NULL;
    json_node_t *node = parse_allocate(parser, sizeof(*node), true);
    if (node == NULL) { parser->error = NOTE4_QUOTA_NO_MEMORY; return NULL; }
    ++parser->depth;
    const unsigned char c = *parser->p;
    if (c == '[' || c == '{') {
        node->type = c == '[' ? JSON_ARRAY : JSON_OBJECT;
        const unsigned char close = c == '[' ? ']' : '}';
        ++parser->p; whitespace(parser);
        json_node_t **tail = &node->child;
        if (parser->p < parser->end && *parser->p == close) ++parser->p;
        else for (;;) {
            char *name = NULL;
            if (node->type == JSON_OBJECT) {
                name = string(parser);
                if (name == NULL) goto invalid;
                for (json_node_t *field = node->child; field != NULL; field = field->next) {
                    if (strcmp(field->name, name) == 0) { free(name); goto invalid; }
                }
                whitespace(parser);
                if (parser->p == parser->end || *parser->p++ != ':') { free(name); goto invalid; }
            }
            json_node_t *child = parse(parser);
            if (child == NULL) { free(name); goto invalid; }
            child->name = name; *tail = child; tail = &child->next;
            whitespace(parser);
            if (parser->p == parser->end) goto invalid;
            if (*parser->p == close) { ++parser->p; break; }
            if (*parser->p++ != ',') goto invalid;
            whitespace(parser);
        }
    } else if (c == '"') {
        node->type = JSON_STRING; node->text = string(parser);
        if (node->text == NULL) goto invalid;
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        const unsigned char *start = parser->p;
        if (*parser->p == '-') ++parser->p;
        if (parser->p == parser->end) goto invalid;
        if (*parser->p == '0') ++parser->p;
        else {
            if (*parser->p < '1' || *parser->p > '9') goto invalid;
            while (parser->p < parser->end && *parser->p >= '0' && *parser->p <= '9') ++parser->p;
        }
        if (parser->p < parser->end && *parser->p == '.') {
            ++parser->p;
            if (parser->p == parser->end || *parser->p < '0' || *parser->p > '9') goto invalid;
            while (parser->p < parser->end && *parser->p >= '0' && *parser->p <= '9') ++parser->p;
        }
        if (parser->p < parser->end && (*parser->p == 'e' || *parser->p == 'E')) {
            ++parser->p;
            if (parser->p < parser->end && (*parser->p == '+' || *parser->p == '-')) ++parser->p;
            if (parser->p == parser->end || *parser->p < '0' || *parser->p > '9') goto invalid;
            while (parser->p < parser->end && *parser->p >= '0' && *parser->p <= '9') ++parser->p;
        }
        const size_t length = (size_t)(parser->p - start);
        node->text = parse_allocate(parser, length + 1U, false);
        if (node->text == NULL) { parser->error = NOTE4_QUOTA_NO_MEMORY; goto invalid; }
        memcpy(node->text, start, length); node->text[length] = 0; node->type = JSON_NUMBER;
    } else {
        const char *literal = c == 't' ? "true" : c == 'f' ? "false" : "null";
        const size_t length = strlen(literal);
        if ((size_t)(parser->end - parser->p) < length || memcmp(parser->p, literal, length) != 0) goto invalid;
        parser->p += length; node->type = c == 'n' ? JSON_NULL : JSON_BOOL;
        if (node->type == JSON_BOOL) {
            node->text = parse_allocate(parser, length + 1U, false);
            if (node->text == NULL) { parser->error = NOTE4_QUOTA_NO_MEMORY; goto invalid; }
            memcpy(node->text, literal, length + 1U);
        }
    }
    --parser->depth; return node;
invalid:
    --parser->depth; destroy(node); return NULL;
}

static const json_node_t *field(const json_node_t *object, const char *name) {
    if (object == NULL || object->type != JSON_OBJECT) return NULL;
    for (const json_node_t *node = object->child; node != NULL; node = node->next) {
        if (strcmp(node->name, name) == 0) return node;
    }
    return NULL;
}
static const json_node_t *at(const json_node_t *array, size_t index) {
    if (array == NULL || array->type != JSON_ARRAY) return NULL;
    const json_node_t *node = array->child;
    while (node != NULL && index--) node = node->next;
    return node;
}
static size_t count(const json_node_t *array) {
    size_t length = 0U;
    if (array != NULL && array->type == JSON_ARRAY) for (const json_node_t *node = array->child; node; node = node->next) ++length;
    return length;
}
static bool typed(const json_node_t *object, const char *const *names, size_t length, json_type_t type, bool nullable) {
    for (size_t i = 0; i < length; ++i) {
        const json_node_t *node = field(object, names[i]);
        if (node != NULL && node->type != type && !(nullable && node->type == JSON_NULL)) return false;
    }
    return true;
}
static bool validate_card(const json_node_t *card) {
    static const char *const strings[] = {"provider", "user", "name", "alias", "seat", "from", "kind", "plan", "balance", "error", "renew"};
    static const char *const times[] = {"asOf", "readAt", "until", "lastServedAt"};
    static const char *const flags[] = {"readDeferred", "last"};
    if (card->type != JSON_OBJECT || !typed(card, strings, sizeof(strings) / sizeof(strings[0]), JSON_STRING, false) ||
        !typed(card, times, sizeof(times) / sizeof(times[0]), JSON_STRING, true) ||
        !typed(card, flags, sizeof(flags) / sizeof(flags[0]), JSON_BOOL, false)) return false;
    const json_node_t *provider = field(card, "provider");
    if (provider == NULL || provider->type != JSON_STRING || !provider->text[0]) return false;
    const json_node_t *windows = field(card, "windows");
    if (windows != NULL && windows->type != JSON_ARRAY) return false;
    if (windows != NULL) for (const json_node_t *window = windows->child; window; window = window->next) {
        static const char *const texts[] = {"name", "display", "unit", "pool", "holdsWhy"};
        static const char *const numbers[] = {"used", "remaining", "amount", "limit", "resetSecs"};
        static const char *const resets[] = {"resetsAt"};
        static const char *const unlimited[] = {"unlimited"};
        if (window->type != JSON_OBJECT || !typed(window, texts, sizeof(texts) / sizeof(texts[0]), JSON_STRING, false) ||
            !typed(window, numbers, sizeof(numbers) / sizeof(numbers[0]), JSON_NUMBER, true) ||
            !typed(window, resets, 1U, JSON_STRING, true) || !typed(window, unlimited, 1U, JSON_BOOL, false)) return false;
    }
    const json_node_t *split = field(card, "balanceSplit");
    if (split != NULL && split->type != JSON_ARRAY) return false;
    if (split != NULL) for (const json_node_t *part = split->child; part; part = part->next) {
        static const char *const texts[] = {"label", "text", "error"};
        static const char *const percentages[] = {"percent"};
        if (part->type != JSON_OBJECT || !typed(part, texts, 3U, JSON_STRING, false) ||
            !typed(part, percentages, 1U, JSON_NUMBER, true)) return false;
        const json_node_t *amount = field(part, "text");
        if (amount == NULL || amount->type != JSON_STRING) return false;
    }
    return true;
}

note4_quota_result_t note4_quota_snapshot_parse(const char *json, size_t length, note4_quota_snapshot_t **output) {
    if (json == NULL || output == NULL || length == 0U) return NOTE4_QUOTA_INVALID;
    parser_t parser = {.p = (const unsigned char *)json, .end = (const unsigned char *)json + length, .error = NOTE4_QUOTA_INVALID};
    json_node_t *root = parse(&parser);
    whitespace(&parser);
    if (root == NULL || root->type != JSON_ARRAY || parser.p != parser.end) { destroy(root); return parser.error; }
    for (const json_node_t *card = root->child; card; card = card->next) {
        if (!validate_card(card)) { destroy(root); return NOTE4_QUOTA_INVALID; }
    }
    note4_quota_snapshot_t *snapshot = parse_allocate(&parser, sizeof(*snapshot), true);
    if (snapshot == NULL) { destroy(root); return NOTE4_QUOTA_NO_MEMORY; }
    atomic_init(&snapshot->references, 1U); snapshot->root = root; snapshot->count = count(root);
    if (snapshot->count > SIZE_MAX / sizeof(*snapshot->cards)) { note4_quota_snapshot_release(snapshot); return NOTE4_QUOTA_NO_MEMORY; }
    if (snapshot->count) {
        snapshot->cards = parse_allocate(&parser, snapshot->count * sizeof(*snapshot->cards), false);
        if (snapshot->cards == NULL) { note4_quota_snapshot_release(snapshot); return NOTE4_QUOTA_NO_MEMORY; }
        size_t i = 0U;
        for (json_node_t *card = root->child; card; card = card->next) snapshot->cards[i++] = card;
    }
    snapshot->bytes = parser.bytes;
    note4_resource_log("snapshot", parser.bytes);
    *output = snapshot; return NOTE4_QUOTA_OK;
}
void note4_quota_snapshot_retain(note4_quota_snapshot_t *snapshot) { if (snapshot) (void)atomic_fetch_add(&snapshot->references, 1U); }
void note4_quota_snapshot_release(note4_quota_snapshot_t *snapshot) {
    if (snapshot && atomic_fetch_sub(&snapshot->references, 1U) == 1U) { destroy(snapshot->root); free(snapshot->cards); free(snapshot->smart_ranks); free(snapshot); }
}
size_t note4_quota_count(const note4_quota_snapshot_t *snapshot) { return snapshot == NULL ? 0U : snapshot->count; }
size_t note4_quota_smart_rank(const note4_quota_snapshot_t *snapshot, size_t card) {
    return snapshot && card < snapshot->count && snapshot->smart_ranks ? snapshot->smart_ranks[card] : card;
}
note4_quota_result_t note4_quota_set_smart_order(note4_quota_snapshot_t *snapshot, const size_t *indices) {
    if (!snapshot || (snapshot->count && !indices)) return NOTE4_QUOTA_INVALID;
    if (snapshot->count > (NOTE4_MODEL_BUDGET - snapshot->bytes) / sizeof(size_t) ||
        !note4_resource_available(snapshot->count * sizeof(size_t))) return NOTE4_QUOTA_NO_MEMORY;
    size_t *ranks = snapshot->count ? malloc(snapshot->count * sizeof(*ranks)) : NULL;
    if (snapshot->count && !ranks) return NOTE4_QUOTA_NO_MEMORY;
    for (size_t i = 0; i < snapshot->count; ++i) ranks[i] = SIZE_MAX;
    for (size_t i = 0; i < snapshot->count; ++i) {
        if (indices[i] >= snapshot->count || ranks[indices[i]] != SIZE_MAX) { free(ranks); return NOTE4_QUOTA_INVALID; }
        ranks[indices[i]] = i;
    }
    free(snapshot->smart_ranks); snapshot->smart_ranks = ranks; return NOTE4_QUOTA_OK;
}
static const json_node_t *card_at(const note4_quota_snapshot_t *snapshot, size_t card) {
    return snapshot != NULL && card < snapshot->count ? snapshot->cards[card] : NULL;
}
static const char *text(const json_node_t *node) { return node != NULL && node->text != NULL ? node->text : ""; }
const char *note4_quota_text(const note4_quota_snapshot_t *snapshot, size_t card, const char *name) { return text(field(card_at(snapshot, card), name)); }
size_t note4_quota_window_count(const note4_quota_snapshot_t *snapshot, size_t card) { return count(field(card_at(snapshot, card), "windows")); }
const char *note4_quota_window_text(const note4_quota_snapshot_t *snapshot, size_t card, size_t window, const char *name) {
    return text(field(at(field(card_at(snapshot, card), "windows"), window), name));
}
bool note4_quota_flag(const note4_quota_snapshot_t *snapshot, size_t card, const char *name) { return strcmp(note4_quota_text(snapshot, card, name), "true") == 0; }
bool note4_quota_window_flag(const note4_quota_snapshot_t *snapshot, size_t card, size_t window, const char *name) { return strcmp(note4_quota_window_text(snapshot, card, window, name), "true") == 0; }
size_t note4_quota_balance_count(const note4_quota_snapshot_t *snapshot, size_t card) { return count(field(card_at(snapshot, card), "balanceSplit")); }
const char *note4_quota_balance_text(const note4_quota_snapshot_t *snapshot, size_t card, size_t part, const char *name) {
    return text(field(at(field(card_at(snapshot, card), "balanceSplit"), part), name));
}

#include "quota_semantics.inc"
