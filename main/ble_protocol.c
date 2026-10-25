#include "ble_protocol.h"

#include <string.h>

#define NOTE4_BLE_HEADER_SIZE 10U
#define NOTE4_BLE_CRC_SIZE 2U

uint16_t note4_ble_crc16(const uint8_t *data, size_t size) {
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            crc = (uint16_t)((crc >> 1U) ^ (0xA001U & (uint16_t)-(int16_t)(crc & 1U)));
        }
    }
    return crc;
}

size_t note4_ble_frame_encoded_size(size_t payload_length) {
    return payload_length > NOTE4_BLE_MAX_PAYLOAD
        ? 0U : NOTE4_BLE_HEADER_SIZE + payload_length + NOTE4_BLE_CRC_SIZE;
}

bool note4_ble_frame_encode(const note4_ble_frame_t *frame,
                            uint8_t *output, size_t output_size,
                            size_t *encoded_size) {
    if (frame == NULL || output == NULL || encoded_size == NULL ||
        frame->version != NOTE4_BLE_PROTOCOL_VERSION ||
        frame->payload_length > NOTE4_BLE_MAX_PAYLOAD ||
        (frame->payload == NULL && frame->payload_length != 0U) ||
        frame->total_sequences == 0U || frame->sequence >= frame->total_sequences) return false;
    const size_t size = note4_ble_frame_encoded_size(frame->payload_length);
    if (output_size < size) return false;
    output[0] = frame->version;
    output[1] = frame->opcode;
    output[2] = (uint8_t)(frame->request_id & 0xFFU);
    output[3] = (uint8_t)(frame->request_id >> 8U);
    output[4] = frame->slot;
    output[5] = frame->sequence;
    output[6] = frame->total_sequences;
    output[7] = (uint8_t)(frame->payload_length & 0xFFU);
    output[8] = (uint8_t)(frame->payload_length >> 8U);
    output[9] = 0U;
    if (frame->payload_length != 0U) memcpy(output + NOTE4_BLE_HEADER_SIZE,
                                            frame->payload, frame->payload_length);
    const uint16_t crc = note4_ble_crc16(output, NOTE4_BLE_HEADER_SIZE + frame->payload_length);
    output[NOTE4_BLE_HEADER_SIZE + frame->payload_length] = (uint8_t)(crc & 0xFFU);
    output[NOTE4_BLE_HEADER_SIZE + frame->payload_length + 1U] = (uint8_t)(crc >> 8U);
    *encoded_size = size;
    return true;
}

note4_ble_status_t note4_ble_frame_decode(const uint8_t *input, size_t input_size,
                                         note4_ble_frame_t *frame,
                                         uint8_t *payload, size_t payload_size) {
    if (input == NULL || frame == NULL || input_size < NOTE4_BLE_HEADER_SIZE + NOTE4_BLE_CRC_SIZE)
        return NOTE4_BLE_ERR_LENGTH;
    if (input[0] != NOTE4_BLE_PROTOCOL_VERSION) return NOTE4_BLE_ERR_VERSION;
    const uint16_t length = (uint16_t)input[7] | (uint16_t)input[8] << 8U;
    const size_t expected = note4_ble_frame_encoded_size(length);
    if (expected == 0U || input_size != expected || payload_size < length)
        return NOTE4_BLE_ERR_LENGTH;
    const uint16_t expected_crc = note4_ble_crc16(input, expected - NOTE4_BLE_CRC_SIZE);
    const uint16_t actual_crc = (uint16_t)input[expected - 2U] |
                                (uint16_t)input[expected - 1U] << 8U;
    if (expected_crc != actual_crc) return NOTE4_BLE_ERR_CRC;
    if (input[6] == 0U || input[5] >= input[6]) return NOTE4_BLE_ERR_SEQUENCE;
    if (length != 0U) memcpy(payload, input + NOTE4_BLE_HEADER_SIZE, length);
    *frame = (note4_ble_frame_t){
        .version = input[0], .opcode = input[1],
        .request_id = (uint16_t)input[2] | (uint16_t)input[3] << 8U,
        .slot = input[4], .sequence = input[5], .total_sequences = input[6],
        .payload_length = length, .payload = payload,
    };
    return NOTE4_BLE_OK;
}

bool note4_ble_decode_set_slot(const uint8_t *payload, size_t payload_length,
                               note4_ble_set_slot_t *slot) {
    if (payload == NULL || slot == NULL || payload_length < 3U) return false;
    const bool enabled = payload[0] != 0U;
    const uint8_t mode = payload[1];
    const uint8_t ssid_length = payload[2];
    if (mode > NOTE4_BLE_PASSWORD_CLEAR || ssid_length > NOTE4_WIFI_SSID_MAX ||
        3U + ssid_length > payload_length || (!enabled && ssid_length != 0U) ||
        (enabled && ssid_length == 0U)) return false;
    const size_t password_offset = 3U + ssid_length;
    uint8_t password_length = 0U;
    if (mode == NOTE4_BLE_PASSWORD_SET) {
        if (password_offset >= payload_length) return false;
        password_length = payload[password_offset];
        if (password_length == 0U || password_length > NOTE4_WIFI_PASSWORD_MAX ||
            password_offset + 1U + password_length != payload_length) return false;
    } else if (password_offset != payload_length) {
        return false;
    }
    memset(slot, 0, sizeof(*slot));
    slot->enabled = enabled;
    slot->password_mode = (note4_ble_password_mode_t)mode;
    slot->ssid_length = ssid_length;
    memcpy(slot->ssid, payload + 3U, ssid_length);
    slot->password_length = password_length;
    if (password_length != 0U) memcpy(slot->password, payload + password_offset + 1U, password_length);
    return true;
}
