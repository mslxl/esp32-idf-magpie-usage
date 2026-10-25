#ifndef NOTE4_BLE_PROVISIONING_H
#define NOTE4_BLE_PROVISIONING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "ble_protocol.h"

typedef enum {
    NOTE4_BLE_EVENT_REQUEST = 0,
    NOTE4_BLE_EVENT_DISCONNECTED,
    NOTE4_BLE_EVENT_PASSKEY,
    NOTE4_BLE_EVENT_PAIRED,
} note4_ble_event_type_t;

typedef struct {
    note4_ble_event_type_t type;
    uint32_t passkey;
    uint16_t conn_handle;
    note4_ble_frame_t frame;
    uint8_t payload[NOTE4_BLE_MAX_TRANSACTION];
} note4_ble_event_t;

esp_err_t note4_ble_provisioning_init(void);
const char *note4_ble_provisioning_name(void);
void note4_ble_provisioning_set_enabled(bool enabled);
bool note4_ble_provisioning_receive(note4_ble_event_t *event, uint32_t timeout_ms);
esp_err_t note4_ble_provisioning_notify(const note4_ble_frame_t *frame);
esp_err_t note4_ble_provisioning_ack(uint16_t conn_handle, uint16_t request_id,
                                     note4_ble_status_t status);

#endif
