#include <assert.h>
#include <string.h>

#include "ble_protocol.h"

int main(void) {
    const uint8_t payload[] = {0x01U, 0x02U, 0xA5U, 0xFFU};
    const note4_ble_frame_t source = {
        .version = NOTE4_BLE_PROTOCOL_VERSION,
        .opcode = NOTE4_BLE_OP_SET_SLOT,
        .request_id = 0x1234U,
        .slot = 2U,
        .sequence = 0U,
        .total_sequences = 1U,
        .payload_length = sizeof(payload),
        .payload = payload,
    };
    uint8_t encoded[NOTE4_BLE_MAX_FRAME];
    size_t encoded_size = 0U;
    assert(note4_ble_frame_encode(&source, encoded, sizeof(encoded), &encoded_size));
    assert(encoded_size == 16U);

    uint8_t decoded_payload[NOTE4_BLE_MAX_PAYLOAD];
    note4_ble_frame_t decoded;
    assert(note4_ble_frame_decode(encoded, encoded_size, &decoded,
                                  decoded_payload, sizeof(decoded_payload)) == NOTE4_BLE_OK);
    assert(decoded.request_id == source.request_id);
    assert(decoded.slot == source.slot);
    assert(decoded.payload_length == sizeof(payload));
    assert(memcmp(decoded_payload, payload, sizeof(payload)) == 0);

    encoded[encoded_size - 1U] ^= 1U;
    assert(note4_ble_frame_decode(encoded, encoded_size, &decoded,
                                  decoded_payload, sizeof(decoded_payload)) == NOTE4_BLE_ERR_CRC);
    encoded[encoded_size - 1U] ^= 1U;
    encoded[0] = 9U;
    assert(note4_ble_frame_decode(encoded, encoded_size, &decoded,
                                  decoded_payload, sizeof(decoded_payload)) == NOTE4_BLE_ERR_VERSION);
    encoded[0] = NOTE4_BLE_PROTOCOL_VERSION;
    encoded[7] = 0xFFU;
    assert(note4_ble_frame_decode(encoded, encoded_size, &decoded,
                                  decoded_payload, sizeof(decoded_payload)) == NOTE4_BLE_ERR_LENGTH);

    const uint8_t set_slot_payload[] = {1U, NOTE4_BLE_PASSWORD_SET, 4U, 'h', 'o', 'm', 'e', 6U,
                                         's', 'e', 'c', 'r', 'e', 't'};
    note4_ble_set_slot_t set_slot;
    assert(note4_ble_decode_set_slot(set_slot_payload, sizeof(set_slot_payload), &set_slot));
    assert(set_slot.enabled && set_slot.password_mode == NOTE4_BLE_PASSWORD_SET);
    assert(set_slot.ssid_length == 4U && strcmp(set_slot.ssid, "home") == 0);
    assert(set_slot.password_length == 6U && strcmp(set_slot.password, "secret") == 0);
    const uint8_t invalid_slot[] = {1U, NOTE4_BLE_PASSWORD_SET, 0U};
    assert(!note4_ble_decode_set_slot(invalid_slot, sizeof(invalid_slot), &set_slot));
    return 0;
}
