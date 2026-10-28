#ifndef NOTE4_LOGIC_H
#define NOTE4_LOGIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t note4_rs_crc32(const uint8_t *data, size_t length);

size_t note4_rs_ble_frame_encoded_size(size_t payload_length);
int note4_rs_ble_frame_encode(const void *frame,
                              uint8_t *output, size_t output_size,
                              size_t *encoded_size);
int note4_rs_ble_frame_decode(const uint8_t *input, size_t input_size,
                              void *frame, uint8_t *payload,
                              size_t payload_size);
uint16_t note4_rs_ble_crc16(const uint8_t *data, size_t size);
int note4_rs_ble_decode_set_slot(const uint8_t *payload,
                                 size_t payload_length, void *slot);

int note4_rs_magpie_endpoint_canonicalize(const char *endpoint, size_t length,
                                          char *output, size_t capacity);
int note4_rs_magpie_config_validate(const void *config);
int note4_rs_magpie_config_apply(void *target, const void *saved,
                                 const uint8_t *payload, size_t length);
size_t note4_rs_magpie_config_metadata(const void *config,
                                       uint8_t *output, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
