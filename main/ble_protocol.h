#ifndef NOTE4_BLE_PROTOCOL_H
#define NOTE4_BLE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wifi_config.h"

#define NOTE4_BLE_PROTOCOL_VERSION 1U
#define NOTE4_BLE_MAX_PAYLOAD 192U
#define NOTE4_BLE_MAX_FRAME (12U + NOTE4_BLE_MAX_PAYLOAD)
#define NOTE4_BLE_MAX_TRANSACTION 1024U

typedef enum {
    NOTE4_BLE_OP_BEGIN = 1,
    NOTE4_BLE_OP_GET_STATUS,
    NOTE4_BLE_OP_SCAN,
    NOTE4_BLE_OP_SET_SLOT,
    NOTE4_BLE_OP_COMMIT,
    NOTE4_BLE_OP_ABORT,
    NOTE4_BLE_OP_SET_MAGPIE,
    NOTE4_BLE_OP_GET_MAGPIE,
    NOTE4_BLE_OP_EXIT_SETUP,
    NOTE4_BLE_OP_GET_WEB_SETTINGS,
    NOTE4_BLE_OP_SET_WEB_SETTINGS,
    NOTE4_BLE_OP_OPEN_ACCOUNT_DIRECTORY,
    NOTE4_BLE_OP_READ_ACCOUNT_DIRECTORY,
    NOTE4_BLE_OP_BEGIN_DISPLAY_PREFS,
    NOTE4_BLE_OP_PUT_DISPLAY_PREFS_CHUNK,
    NOTE4_BLE_OP_FINISH_DISPLAY_PREFS,
    NOTE4_BLE_OP_ACK = 0x80,
    NOTE4_BLE_OP_NACK,
    NOTE4_BLE_OP_STATUS,
    NOTE4_BLE_OP_SCAN_RESULT,
    NOTE4_BLE_OP_SCAN_DONE,
    NOTE4_BLE_OP_APPLYING,
    NOTE4_BLE_OP_CONNECTING,
    NOTE4_BLE_OP_CONNECTED,
    NOTE4_BLE_OP_CONNECTION_FAILED,
    NOTE4_BLE_OP_FAILURE,
    NOTE4_BLE_OP_MAGPIE_STATUS,
    NOTE4_BLE_OP_WEB_SETTINGS,
    NOTE4_BLE_OP_ACCOUNT_DIRECTORY,
    NOTE4_BLE_OP_ACCOUNT_DIRECTORY_CHUNK,
} note4_ble_opcode_t;

typedef enum {
    NOTE4_BLE_OK = 0,
    NOTE4_BLE_ERR_VERSION = 1,
    NOTE4_BLE_ERR_LENGTH,
    NOTE4_BLE_ERR_CRC,
    NOTE4_BLE_ERR_SEQUENCE,
    NOTE4_BLE_ERR_STATE,
    NOTE4_BLE_ERR_VALIDATION,
    NOTE4_BLE_ERR_BUSY,
    NOTE4_BLE_ERR_UNAUTHORIZED,
    NOTE4_BLE_ERR_UNSUPPORTED_SCHEMA,
    NOTE4_BLE_ERR_STALE_REVISION,
    NOTE4_BLE_ERR_NO_DIRECTORY,
    NOTE4_BLE_ERR_RESOURCE_EXHAUSTED,
} note4_ble_status_t;

typedef struct {
    uint8_t version;
    uint8_t opcode;
    uint16_t request_id;
    uint8_t slot;
    uint8_t sequence;
    uint8_t total_sequences;
    uint16_t payload_length;
    const uint8_t *payload;
} note4_ble_frame_t;

typedef enum {
    NOTE4_BLE_PASSWORD_UNCHANGED = 0,
    NOTE4_BLE_PASSWORD_SET = 1,
    NOTE4_BLE_PASSWORD_CLEAR = 2,
} note4_ble_password_mode_t;

typedef struct {
    bool enabled;
    note4_ble_password_mode_t password_mode;
    uint8_t ssid_length;
    char ssid[NOTE4_WIFI_SSID_MAX + 1U];
    uint8_t password_length;
    char password[NOTE4_WIFI_PASSWORD_MAX + 1U];
} note4_ble_set_slot_t;

size_t note4_ble_frame_encoded_size(size_t payload_length);
bool note4_ble_frame_encode(const note4_ble_frame_t *frame,
                            uint8_t *output, size_t output_size,
                            size_t *encoded_size);
note4_ble_status_t note4_ble_frame_decode(const uint8_t *input, size_t input_size,
                                         note4_ble_frame_t *frame,
                                         uint8_t *payload, size_t payload_size);
uint16_t note4_ble_crc16(const uint8_t *data, size_t size);
bool note4_ble_decode_set_slot(const uint8_t *payload, size_t payload_length,
                               note4_ble_set_slot_t *slot);

#endif
