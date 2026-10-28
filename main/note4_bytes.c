#include "note4_bytes.h"

#ifdef NOTE4_RUST_LOGIC
#include "note4_logic.h"

uint32_t note4_crc32(const uint8_t *data, size_t length) {
    return note4_rs_crc32(data, length);
}
#else
uint32_t note4_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0U; bit < 8U; ++bit) crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
#endif
