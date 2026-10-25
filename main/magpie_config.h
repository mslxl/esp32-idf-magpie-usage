#ifndef NOTE4_MAGPIE_CONFIG_H
#define NOTE4_MAGPIE_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOTE4_MAGPIE_ENDPOINT_MAX 512U
#define NOTE4_MAGPIE_KEY_MAX 256U

typedef struct {
    char endpoint[NOTE4_MAGPIE_ENDPOINT_MAX + 1U];
    char key[NOTE4_MAGPIE_KEY_MAX + 1U];
} note4_magpie_config_t;

bool note4_magpie_endpoint_canonicalize(const char *endpoint, char *output, size_t capacity);
bool note4_magpie_config_validate(const note4_magpie_config_t *config);
bool note4_magpie_config_apply(note4_magpie_config_t *target,
                               const note4_magpie_config_t *saved,
                               const uint8_t *payload, size_t length);
size_t note4_magpie_config_metadata(const note4_magpie_config_t *config,
                                    uint8_t *output, size_t capacity);

#endif
