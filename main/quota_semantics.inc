/* Canonical semantic values are temporary: allocation failure aborts observation. */
typedef struct { char *bytes; size_t length, capacity; bool failed; } semantic_buffer_t;
static void semantic_append(semantic_buffer_t *out, const char *text, size_t length) {
    if (out->failed) return;
    if (length > SIZE_MAX - out->length - 1U) { out->failed = true; return; }
    const size_t needed = out->length + length + 1U;
    if (needed > out->capacity) {
        size_t capacity = needed <= SIZE_MAX / 2U ? needed * 2U : needed;
        if (capacity > NOTE4_MODEL_BUDGET || !note4_resource_available(capacity)) { out->failed = true; return; }
        char *next = realloc(out->bytes, capacity);
        if (!next) { out->failed = true; return; }
        out->bytes = next; out->capacity = capacity;
    }
    memcpy(out->bytes + out->length, text, length); out->length += length; out->bytes[out->length] = 0;
}
static void semantic_string(semantic_buffer_t *out, const char *value) {
    for (; *value; ++value) {
        if (*value == '\\' || *value == '|' || *value == '{' || *value == '}' || *value == '[' || *value == ']') semantic_append(out, "\\", 1U);
        semantic_append(out, value, 1U);
    }
    semantic_append(out, "|", 1U);
}
/* Add a bounded mantissa displacement to an arbitrarily long signed exponent. */
static void semantic_exponent(semantic_buffer_t *out, const char *value, size_t displacement, bool subtract) {
    bool negative = *value == '-';
    if (*value == '-' || *value == '+') ++value;
    while (*value == '0') ++value;
    const size_t length = strlen(value), width = length > 3U * sizeof(size_t) ? length : 3U * sizeof(size_t);
    if (width > SIZE_MAX - 2U || width + 2U > NOTE4_MODEL_BUDGET ||
        !note4_resource_available(width + 2U)) { out->failed = true; return; }
    unsigned char *digits = calloc(width + 2U, 1U);
    if (!digits) { out->failed = true; return; }
    for (size_t i = 0; i < length; ++i) digits[i] = (unsigned char)(value[length - i - 1U] - '0');
    unsigned char adjustment[3U * sizeof(size_t)] = {0}; size_t used = 0U;
    do { adjustment[used++] = (unsigned char)(displacement % 10U); displacement /= 10U; } while (displacement);
    bool add = negative == subtract;
    if (!add) {
        int comparison = 0;
        for (size_t i = width; i-- > 0U;) {
            const unsigned char rhs = i < used ? adjustment[i] : 0U;
            if (digits[i] != rhs) { comparison = digits[i] > rhs ? 1 : -1; break; }
        }
        if (comparison < 0) {
            negative = subtract;
            for (size_t i = 0; i < width; ++i) { const unsigned char old = digits[i]; digits[i] = i < used ? adjustment[i] : 0U; if (i < sizeof(adjustment)) adjustment[i] = old; }
            used = sizeof(adjustment);
        }
    }
    int carry = 0;
    for (size_t i = 0; i < width; ++i) {
        int digit = digits[i] + (add ? 1 : -1) * (i < used ? adjustment[i] : 0) + carry;
        carry = digit < 0 ? -1 : digit > 9 ? 1 : 0;
        if (digit < 0) digit += 10; else if (digit > 9) digit -= 10;
        digits[i] = (unsigned char)digit;
    }
    size_t end = width;
    if (carry > 0) digits[end++] = (unsigned char)carry;
    while (end && digits[end - 1U] == 0U) --end;
    if (end && negative) semantic_append(out, "-", 1U);
    if (!end) semantic_append(out, "0", 1U);
    while (end) { char digit = (char)('0' + digits[--end]); semantic_append(out, &digit, 1U); }
    free(digits);
}
static void semantic_decimal(semantic_buffer_t *out, const char *value) {
    const bool negative = *value == '-'; if (negative) ++value;
    const char *end = value; size_t fraction = 0U; bool point = false;
    while (*end && *end != 'e' && *end != 'E') { if (*end == '.') point = true; else if (point) ++fraction; ++end; }
    const char *first = value, *last = end;
    while (first < end && (*first == '0' || *first == '.')) ++first;
    if (first == end) { semantic_append(out, "0", 1U); return; }
    size_t trailing = 0U;
    while (last > first && (last[-1] == '0' || last[-1] == '.')) { if (last[-1] == '0') ++trailing; --last; }
    if (negative) semantic_append(out, "-", 1U);
    for (const char *p = first; p < last; ++p) if (*p != '.') semantic_append(out, p, 1U);
    semantic_append(out, "e", 1U);
    semantic_exponent(out, *end ? end + 1 : "0", trailing >= fraction ? trailing - fraction : fraction - trailing, trailing < fraction);
}
static int semantic_digits(const char *s, size_t n) {
    int result = 0;
    for (size_t i = 0; i < n; ++i) { if (s[i] < '0' || s[i] > '9') return -1; result = result * 10 + s[i] - '0'; }
    return result;
}
static bool semantic_time(semantic_buffer_t *out, const char *value) {
    const size_t length = strlen(value);
    if (length < 20U || value[4] != '-' || value[7] != '-' || (value[10] != 'T' && value[10] != 't') || value[13] != ':' || value[16] != ':') return false;
    int year = semantic_digits(value, 4U), month = semantic_digits(value + 5, 2U), day = semantic_digits(value + 8, 2U);
    int hour = semantic_digits(value + 11, 2U), minute = semantic_digits(value + 14, 2U), second = semantic_digits(value + 17, 2U);
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (year < 1 || month < 1 || month > 12 || day < 1 || day > days[month - 1] + (month == 2 && leap) || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return false;
    size_t cursor = 19U, fraction_start = cursor, fraction_end = cursor;
    if (value[cursor] == '.') {
        fraction_start = ++cursor;
        while (value[cursor] >= '0' && value[cursor] <= '9') ++cursor;
        if (cursor == fraction_start) return false;
        fraction_end = cursor; while (fraction_end > fraction_start && value[fraction_end - 1U] == '0') --fraction_end;
    }
    int offset = 0;
    if ((value[cursor] == 'Z' || value[cursor] == 'z') && cursor + 1U == length) { }
    else if (cursor + 6U == length && (value[cursor] == '+' || value[cursor] == '-') && value[cursor + 3U] == ':') {
        int oh = semantic_digits(value + cursor + 1U, 2U), om = semantic_digits(value + cursor + 4U, 2U);
        if (oh < 0 || oh > 23 || om < 0 || om > 59) return false;
        offset = (oh * 60 + om) * (value[cursor] == '-' ? -1 : 1);
    } else return false;
    int64_t total_days = 0;
    for (int y = 1; y < year; ++y) total_days += 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    for (int m = 1; m < month; ++m) total_days += days[m - 1] + (m == 2 && leap);
    int64_t seconds = ((total_days + day - 1) * 24 + hour) * 3600 + minute * 60 + second - offset * 60;
    char text_value[32]; int written = snprintf(text_value, sizeof(text_value), "%lld", (long long)seconds);
    if (written < 0 || (size_t)written >= sizeof(text_value)) { out->failed = true; return true; }
    semantic_append(out, text_value, (size_t)written); semantic_append(out, ".", 1U);
    semantic_append(out, value + fraction_start, fraction_end - fraction_start); return true;
}
static int semantic_compare_strings(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void semantic_node(semantic_buffer_t *out, const json_node_t *node, unsigned level);
static bool semantic_ignored(const char *name, unsigned level) {
    static const char *const ignored[] = {"readAt", "asOf", "lastServedAt", "readDeferred", "last", "reading", "resetSecs", "runsOut", "seeAt", "balanceTrend"};
    for (size_t i = 0; i < sizeof(ignored) / sizeof(ignored[0]); ++i) if (!strcmp(name, ignored[i])) return true;
    return level == 0U && (!strcmp(name, "name") || !strcmp(name, "alias") || !strcmp(name, "seat") || !strcmp(name, "icon"));
}
static void semantic_node(semantic_buffer_t *out, const json_node_t *node, unsigned level) {
    if (!node) return;
    if (node->type == JSON_OBJECT || node->type == JSON_ARRAY) {
        size_t n = 0U;
        for (const json_node_t *child = node->child; child; child = child->next) if (!child->name || !semantic_ignored(child->name, level)) ++n;
        if (n > NOTE4_MODEL_BUDGET / sizeof(char *) ||
            (n && !note4_resource_available(n * sizeof(char *)))) { out->failed = true; return; }
        char **parts = n ? calloc(n, sizeof(*parts)) : NULL;
        if (n && !parts) { out->failed = true; return; }
        size_t used = 0U;
        for (const json_node_t *child = node->child; child && !out->failed; child = child->next) {
            if (child->name && semantic_ignored(child->name, level)) continue;
            semantic_buffer_t part = {0};
            if (child->name) semantic_string(&part, child->name);
            semantic_node(&part, child, level + 1U);
            if (part.failed) { out->failed = true; free(part.bytes); break; }
            if (!part.bytes) { semantic_append(&part, "", 0U); }
            if (part.failed) { out->failed = true; break; }
            parts[used++] = part.bytes;
        }
        if (used > 1U) qsort(parts, used, sizeof(*parts), semantic_compare_strings);
        semantic_append(out, node->type == JSON_OBJECT ? "{" : "[", 1U);
        for (size_t i = 0; i < used; ++i) { semantic_string(out, parts[i]); free(parts[i]); }
        free(parts); semantic_append(out, node->type == JSON_OBJECT ? "}" : "]", 1U); return;
    }
    if (node->name && (!strcmp(node->name, "error") || !strcmp(node->name, "pluginError") || !strcmp(node->name, "holdsWhy"))) {
        semantic_append(out, text(node)[0] ? "error" : "ok", text(node)[0] ? 5U : 2U); return;
    }
    if (node->type == JSON_NUMBER) { semantic_append(out, "n", 1U); semantic_decimal(out, text(node)); return; }
    if (node->type == JSON_STRING && node->name && (!strcmp(node->name, "until") || !strcmp(node->name, "resetsAt") || !strcmp(node->name, "runsOutAt") || !strcmp(node->name, "startsAt"))) {
        semantic_append(out, "t", 1U); if (semantic_time(out, text(node))) return;
    }
    const char type_value = (char)('0' + node->type); semantic_append(out, &type_value, 1U); semantic_string(out, text(node));
}
note4_quota_result_t note4_quota_record_equal(const note4_quota_snapshot_t *a, size_t ai, const note4_quota_snapshot_t *b, size_t bi, bool *equal) {
    if (!equal || !card_at(a, ai) || !card_at(b, bi)) return NOTE4_QUOTA_INVALID;
    semantic_buffer_t left = {0}, right = {0};
    semantic_node(&left, card_at(a, ai), 0U); semantic_node(&right, card_at(b, bi), 0U);
    bool failed = left.failed || right.failed;
    if (!failed) *equal = left.length == right.length && !memcmp(left.bytes, right.bytes, left.length);
    free(left.bytes); free(right.bytes); return failed ? NOTE4_QUOTA_NO_MEMORY : NOTE4_QUOTA_OK;
}
static bool semantic_identity(const note4_quota_snapshot_t *a, size_t ai, const note4_quota_snapshot_t *b, size_t bi) {
    return !strcmp(note4_quota_text(a, ai, "provider"), note4_quota_text(b, bi, "provider")) && !strcmp(note4_quota_text(a, ai, "user"), note4_quota_text(b, bi, "user"));
}
note4_quota_result_t note4_quota_group_equal(const note4_quota_snapshot_t *a, size_t ai, const note4_quota_snapshot_t *b, size_t bi, bool *equal) {
    if (!equal || !card_at(a, ai) || !card_at(b, bi)) return NOTE4_QUOTA_INVALID;
    *equal = false;
    size_t ac = 0U, bc = 0U;
    for (size_t i = 0; i < a->count; ++i) if (semantic_identity(a, ai, a, i)) ++ac;
    for (size_t i = 0; i < b->count; ++i) if (semantic_identity(a, ai, b, i)) ++bc;
    if (ac != bc) return NOTE4_QUOTA_OK;
    for (size_t i = 0; i < a->count; ++i) {
        if (!semantic_identity(a, ai, a, i)) continue;
        size_t left = 0U, right = 0U;
        for (size_t j = 0; j < a->count; ++j) if (semantic_identity(a, ai, a, j)) {
            bool match; note4_quota_result_t result = note4_quota_record_equal(a, i, a, j, &match);
            if (result != NOTE4_QUOTA_OK) return result;
            if (match) ++left;
        }
        for (size_t j = 0; j < b->count; ++j) if (semantic_identity(a, ai, b, j)) {
            bool match; note4_quota_result_t result = note4_quota_record_equal(a, i, b, j, &match);
            if (result != NOTE4_QUOTA_OK) return result;
            if (match) ++right;
        }
        if (left != right) return NOTE4_QUOTA_OK;
    }
    *equal = true; return NOTE4_QUOTA_OK;
}
