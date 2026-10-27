#ifndef NOTE4_BYTES_H
#define NOTE4_BYTES_H

#include <stddef.h>
#include <stdint.h>

/* Little-endian helpers shared by the persisted and BLE wire formats. */
static inline uint16_t note4_get16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8U));
}

static inline uint32_t note4_get32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static inline void note4_put16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

static inline void note4_put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4U; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

/* IEEE 802.3 CRC-32 (reflected, init and final xor 0xffffffff). */
uint32_t note4_crc32(const uint8_t *data, size_t length);

#endif
