#include "magpie_config.h"

#include <string.h>
#include <stdio.h>

static bool endpoint_valid(const char *url) {
    const char *host;
    if (strncmp(url, "https://", 8U) == 0) host = url + 8U;
    else if (strncmp(url, "http://", 7U) == 0) host = url + 7U;
    else return false;
    const char *end = strchr(host, '/');
    if (end == NULL) end = host + strlen(host);
    if (end == host) return false;
    for (const unsigned char *p = (const unsigned char *)url; *p; ++p) {
        if (*p <= 32U || *p == 127U || *p == '@' || *p == '?' || *p == '#' || *p == '\\') return false;
    }
    const char *port = NULL;
    if (*host == '[') {
        const char *close = strchr(host, ']');
        if (close == NULL || close >= end || close == host + 1U) return false;
        for (const char *p = host + 1U; p < close; ++p) {
            if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
                  (*p >= 'A' && *p <= 'F') || *p == ':' || *p == '.')) return false;
        }
        if (close + 1U != end) {
            if (close[1] != ':') return false;
            port = close + 2U;
        }
    } else {
        const char *p = host;
        for (; p < end && *p != ':'; ++p) {
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '-' || *p == '.')) return false;
        }
        if (p == host) return false;
        if (p != end) port = p + 1U;
    }
    if (port != NULL) {
        unsigned value = 0U;
        if (port == end) return false;
        for (const char *p = port; p < end; ++p) {
            if (*p < '0' || *p > '9' || value > 6553U) return false;
            value = value * 10U + (unsigned)(*p - '0');
        }
        if (value == 0U || value > 65535U) return false;
    }
    return true;
}

bool note4_magpie_endpoint_canonicalize(const char *endpoint, char *output, size_t capacity) {
    if (endpoint == NULL || output == NULL || capacity == 0U) return false;
    const size_t length = strlen(endpoint);
    if (length > NOTE4_MAGPIE_ENDPOINT_MAX || capacity <= length) return false;
    if (length == 0U) { output[0] = 0; return true; }
    char value[NOTE4_MAGPIE_ENDPOINT_MAX + 1U];
    memcpy(value, endpoint, length + 1U);
    const size_t scheme = strncmp(value, "https://", 8U) == 0 ? 8U : 7U;
    if (!endpoint_valid(value)) return false;
    char *path = strchr(value + scheme, '/');
    char *authority_end = path == NULL ? value + length : path;
    for (char *p = value + scheme; p < authority_end; ++p) {
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + ('a' - 'A'));
    }
    char *port = NULL;
    if (value[scheme] == '[') {
        char *close = strchr(value + scheme, ']');
        if (close != NULL && close + 1U < authority_end) port = close + 1U;
    } else port = memchr(value + scheme, ':', (size_t)(authority_end - value - scheme));
    if (port != NULL) {
        unsigned number = 0U;
        for (char *p = port + 1U; p < authority_end; ++p) number = number * 10U + (unsigned)(*p - '0');
        if ((scheme == 8U && number == 443U) || (scheme == 7U && number == 80U)) {
            memmove(port, authority_end, strlen(authority_end) + 1U);
        } else {
            char normalized_port[7];
            const int port_length = snprintf(normalized_port, sizeof(normalized_port), ":%u", number);
            memmove(port + port_length, authority_end, strlen(authority_end) + 1U);
            memcpy(port, normalized_port, (size_t)port_length);
        }
    }
    size_t normalized_length = strlen(value);
    while (normalized_length > scheme && value[normalized_length - 1U] == '/') value[--normalized_length] = 0;
    const char *segments = strchr(value + scheme, '/');
    if (segments != NULL) {
        for (const char *p = segments; *p; ++p) {
            if (*p == '%' || (*p == '/' && p[1] == '/') ||
                (*p == '/' && p[1] == '.' && (p[2] == 0 || p[2] == '/' ||
                 (p[2] == '.' && (p[3] == 0 || p[3] == '/'))))) return false;
        }
    }
    memcpy(output, value, normalized_length + 1U);
    return true;
}

bool note4_magpie_config_validate(const note4_magpie_config_t *config) {
    if (config == NULL || memchr(config->endpoint, '\0', sizeof(config->endpoint)) == NULL ||
        memchr(config->key, '\0', sizeof(config->key)) == NULL) return false;
    if (config->endpoint[0] == '\0') return config->key[0] == '\0';
    if (!endpoint_valid(config->endpoint)) return false;
    const size_t key_length = strlen(config->key);
    if (key_length != 0U && key_length < 16U) return false;
    for (const unsigned char *p = (const unsigned char *)config->key; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '.' || *p == '_' || *p == '~')) return false;
    }
    return true;
}

bool note4_magpie_config_apply(note4_magpie_config_t *target,
                               const note4_magpie_config_t *saved,
                               const uint8_t *payload, size_t length) {
    if (target == NULL || saved == NULL || payload == NULL || length < 5U) return false;
    const uint8_t mode = payload[0];
    const size_t endpoint_length = payload[1] | ((size_t)payload[2] << 8U);
    const size_t key_length = payload[3] | ((size_t)payload[4] << 8U);
    if (mode > 2U || endpoint_length > NOTE4_MAGPIE_ENDPOINT_MAX || key_length > NOTE4_MAGPIE_KEY_MAX ||
        length != 5U + endpoint_length + key_length ||
        (mode != 1U && key_length != 0U) || (mode == 1U && key_length == 0U) ||
        memchr(payload + 5U, 0, endpoint_length + key_length) != NULL) return false;
    note4_magpie_config_t next = {0};
    memcpy(next.endpoint, payload + 5U, endpoint_length);
    if (mode == 0U) {
        if (strcmp(saved->endpoint, next.endpoint) != 0) return false;
        memcpy(next.key, saved->key, sizeof(next.key));
    } else if (mode == 1U) {
        memcpy(next.key, payload + 5U + endpoint_length, key_length);
    }
    const bool valid = note4_magpie_config_validate(&next);
    if (valid) *target = next;
    memset(&next, 0, sizeof(next));
    return valid;
}

size_t note4_magpie_config_metadata(const note4_magpie_config_t *config,
                                    uint8_t *output, size_t capacity) {
    if (!note4_magpie_config_validate(config)) return 0U;
    const size_t length = strlen(config->endpoint);
    if (output == NULL || capacity < length + 3U) return 0U;
    output[0] = (uint8_t)length;
    output[1] = (uint8_t)(length >> 8U);
    output[2] = config->key[0] != '\0';
    memcpy(output + 3U, config->endpoint, length);
    return length + 3U;
}
