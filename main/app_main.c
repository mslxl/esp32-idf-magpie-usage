#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ble_provisioning.h"
#include "button_input.h"
#include "config_store.h"
#include "note4_screen.h"
#include "provisioning_state.h"
#include "wifi_manager.h"

#define NOTE4_VBAT_LATCH GPIO_NUM_17
#define NOTE4_CONNECT_TIMEOUT_MS 15000U
#define NOTE4_TRANSACTION_TIMEOUT_MS 120000U

static const char *TAG = "note4";

typedef enum { NOTE4_COORD_WIFI, NOTE4_COORD_BUTTON, NOTE4_COORD_ACTION } coord_type_t;
typedef struct {
    coord_type_t type;
    union {
        note4_wifi_event_t wifi;
        note4_button_event_t button;
        note4_provisioning_event_type_t action;
    } data;
} coord_event_t;

typedef struct {
    note4_wifi_config_t committed;
    note4_wifi_config_t staged;
    note4_provisioning_snapshot_t state;
    QueueHandle_t queue;
    bool transaction_active;
    TickType_t transaction_deadline;
    bool waiting_to_connect;
    TickType_t connect_deadline;
    uint32_t attempt;
    uint16_t scan_request;
    bool cached;
    uint16_t cached_conn;
    uint16_t cached_id;
    uint16_t cached_crc;
    uint8_t cached_opcode;
    uint8_t cached_slot;
    note4_ble_status_t cached_status;
} controller_t;

static controller_t s_controller;

static bool configuring(const controller_t *controller) {
    return controller->state.state == NOTE4_PROVISIONING_CONFIGURING ||
        controller->state.state == NOTE4_PROVISIONING_DISCARD_DIALOG;
}

static bool due(TickType_t deadline) {
    return (int32_t)(xTaskGetTickCount() - deadline) >= 0;
}

static void notify(uint8_t opcode, uint16_t request, uint8_t slot,
                    const uint8_t *payload, size_t length) {
    const note4_ble_frame_t frame = {
        .version = NOTE4_BLE_PROTOCOL_VERSION, .opcode = opcode, .request_id = request,
        .slot = slot, .total_sequences = 1U, .payload_length = length, .payload = payload};
    (void)note4_ble_provisioning_notify(&frame);
}

static void send_status(controller_t *controller, uint16_t request);

static void publish_state(controller_t *controller) {
    (void)note4_screen_render(&controller->state);
    note4_ble_provisioning_set_enabled(configuring(controller));
    uint8_t opcode = NOTE4_BLE_OP_STATUS;
    switch (controller->state.state) {
    case NOTE4_PROVISIONING_APPLYING: opcode = NOTE4_BLE_OP_APPLYING; break;
    case NOTE4_PROVISIONING_CONNECTING: opcode = NOTE4_BLE_OP_CONNECTING; break;
    case NOTE4_PROVISIONING_CONNECTED: opcode = NOTE4_BLE_OP_CONNECTED; break;
    case NOTE4_PROVISIONING_FAILURE: opcode = NOTE4_BLE_OP_FAILURE; break;
    default: break;
    }
    if (opcode != NOTE4_BLE_OP_STATUS) {
        notify(opcode, 0U, controller->state.slot, &controller->state.round, 1U);
    } else {
        send_status(controller, 0U);
    }
    ESP_LOGI(TAG, "state=%u slot=%u round=%u", controller->state.state,
             (unsigned)controller->state.slot + 1U, controller->state.round);
}

static void transition(controller_t *controller, note4_provisioning_event_type_t type) {
    const note4_provisioning_event_t event = {.type = type};
    note4_provisioning_apply(&controller->state, &event,
                             note4_wifi_config_enabled_mask(&controller->committed));
}

static void enqueue_wifi(const note4_wifi_event_t *event, void *context) {
    controller_t *controller = context;
    const coord_event_t queued = {.type = NOTE4_COORD_WIFI, .data.wifi = *event};
    (void)xQueueSend(controller->queue, &queued, 0U);
}

static void enqueue_button(const note4_button_event_t *event, void *context) {
    controller_t *controller = context;
    const coord_event_t queued = {.type = NOTE4_COORD_BUTTON, .data.button = *event};
    (void)xQueueSend(controller->queue, &queued, 0U);
}

static void enqueue_action(note4_provisioning_event_type_t action, void *context) {
    controller_t *controller = context;
    const coord_event_t event = {.type = NOTE4_COORD_ACTION, .data.action = action};
    (void)xQueueSend(controller->queue, &event, 0U);
}

static void discard_staged(controller_t *controller) {
    memset(&controller->staged, 0, sizeof(controller->staged));
    controller->transaction_active = false;
    controller->state.has_staged_config = false;
}

static void schedule_connect(controller_t *controller) {
    controller->waiting_to_connect = true;
    controller->connect_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(750U);
}

static void connection_failed(controller_t *controller) {
    notify(NOTE4_BLE_OP_CONNECTION_FAILED, 0U, controller->state.slot, NULL, 0U);
    transition(controller, NOTE4_PROVISIONING_EVENT_CONNECT_FAILED);
    controller->waiting_to_connect = false;
    if (controller->state.state == NOTE4_PROVISIONING_CONNECTING) schedule_connect(controller);
    else (void)note4_wifi_manager_disconnect();
    publish_state(controller);
}

static void action(controller_t *controller, note4_provisioning_event_type_t event) {
    const note4_provisioning_state_t previous = controller->state.state;
    if (event == NOTE4_PROVISIONING_EVENT_LONG_DOWN) {
        (void)note4_wifi_manager_disconnect();
        controller->waiting_to_connect = false;
        discard_staged(controller);
    }
    transition(controller, event);
    if (previous == NOTE4_PROVISIONING_DISCARD_DIALOG && event == NOTE4_PROVISIONING_EVENT_OK) {
        discard_staged(controller);
        controller->state.pairing = false;
        controller->state.passkey = 0U;
        if (controller->state.state == NOTE4_PROVISIONING_CONNECTING) schedule_connect(controller);
    } else if (previous == NOTE4_PROVISIONING_FAILURE && event == NOTE4_PROVISIONING_EVENT_OK) {
        schedule_connect(controller);
    }
    publish_state(controller);
}

static void send_status(controller_t *controller, uint16_t request) {
    uint8_t payload[2U + NOTE4_WIFI_MAX_SLOTS * (NOTE4_WIFI_SSID_MAX + 2U)];
    size_t offset = 0U;
    payload[offset++] = controller->state.state;
    payload[offset++] = note4_wifi_config_enabled_mask(&controller->committed);
    for (size_t index = 0U; index < NOTE4_WIFI_MAX_SLOTS; ++index) {
        const note4_wifi_slot_t *slot = &controller->committed.slots[index];
        payload[offset++] = slot->ssid_len;
        memcpy(payload + offset, slot->ssid, slot->ssid_len);
        offset += slot->ssid_len;
        payload[offset++] = slot->password_len != 0U;
    }
    notify(NOTE4_BLE_OP_STATUS, request, 0U, payload, offset);
}

static bool apply_slot(controller_t *controller, const note4_ble_event_t *event) {
    if (event->frame.slot >= NOTE4_WIFI_MAX_SLOTS) return false;
    note4_ble_set_slot_t slot;
    if (!note4_ble_decode_set_slot(event->payload, event->frame.payload_length, &slot)) return false;
    const note4_wifi_slot_t *old = &controller->committed.slots[event->frame.slot];
    const char *password = slot.password_mode == NOTE4_BLE_PASSWORD_SET ? slot.password : "";
    if (slot.enabled && slot.password_mode == NOTE4_BLE_PASSWORD_UNCHANGED) {
        if (!old->enabled || old->ssid_len != slot.ssid_length ||
            memcmp(old->ssid, slot.ssid, slot.ssid_length) != 0) return false;
        password = old->password;
    }
    const bool valid = note4_wifi_slot_set(&controller->staged, event->frame.slot,
                                          slot.enabled ? slot.ssid : "", slot.enabled ? password : "", slot.enabled);
    memset(&slot, 0, sizeof(slot));
    return valid;
}

static void process_ble(controller_t *controller, const note4_ble_event_t *event) {
    if (event->type == NOTE4_BLE_EVENT_DISCONNECTED) {
        discard_staged(controller);
        controller->cached = false;
        controller->scan_request = 0U;
        controller->state.pairing = false;
        controller->state.passkey = 0U;
        publish_state(controller);
        return;
    }
    if (event->type == NOTE4_BLE_EVENT_PASSKEY || event->type == NOTE4_BLE_EVENT_PAIRED) {
        controller->state.pairing = event->type == NOTE4_BLE_EVENT_PASSKEY;
        controller->state.passkey = controller->state.pairing ? event->passkey : 0U;
        publish_state(controller);
        return;
    }
    const uint16_t crc = note4_ble_crc16(event->payload, event->frame.payload_length);
    if (controller->cached && controller->cached_conn == event->conn_handle &&
        controller->cached_id == event->frame.request_id) {
        const bool same = controller->cached_opcode == event->frame.opcode &&
            controller->cached_slot == event->frame.slot && controller->cached_crc == crc;
        (void)note4_ble_provisioning_ack(event->conn_handle, event->frame.request_id,
            same ? controller->cached_status : NOTE4_BLE_ERR_SEQUENCE);
        if (same && (event->frame.opcode == NOTE4_BLE_OP_BEGIN || event->frame.opcode == NOTE4_BLE_OP_GET_STATUS)) {
            send_status(controller, event->frame.request_id);
        }
        return;
    }
    note4_ble_status_t status = NOTE4_BLE_OK;
    bool return_status = false;
    bool changed = false;
    if (!configuring(controller)) status = NOTE4_BLE_ERR_STATE;
    else switch (event->frame.opcode) {
    case NOTE4_BLE_OP_BEGIN:
        controller->staged = controller->committed;
        controller->transaction_active = true;
        controller->state.has_staged_config = true;
        return_status = true;
        break;
    case NOTE4_BLE_OP_GET_STATUS:
        return_status = true;
        break;
    case NOTE4_BLE_OP_SCAN:
        if (controller->scan_request != 0U || note4_wifi_manager_scan() != ESP_OK) status = NOTE4_BLE_ERR_BUSY;
        else controller->scan_request = event->frame.request_id;
        break;
    case NOTE4_BLE_OP_SET_SLOT:
        if (!controller->transaction_active || !apply_slot(controller, event)) status = NOTE4_BLE_ERR_VALIDATION;
        break;
    case NOTE4_BLE_OP_COMMIT:
        if (!controller->transaction_active || !note4_wifi_config_validate(&controller->staged)) {
            status = NOTE4_BLE_ERR_VALIDATION;
        } else {
            transition(controller, NOTE4_PROVISIONING_EVENT_COMMIT_ACCEPTED);
            publish_state(controller);
            if (note4_config_store_save(&controller->staged) != ESP_OK) {
                transition(controller, NOTE4_PROVISIONING_EVENT_COMMIT_FAILED);
                status = NOTE4_BLE_ERR_BUSY;
            } else {
                controller->committed = controller->staged;
                discard_staged(controller);
                ESP_ERROR_CHECK(note4_wifi_manager_set_config(&controller->committed));
                transition(controller, NOTE4_PROVISIONING_EVENT_CONFIG_LOADED);
                schedule_connect(controller);
            }
            changed = true;
        }
        break;
    case NOTE4_BLE_OP_ABORT:
        discard_staged(controller);
        if (controller->state.state == NOTE4_PROVISIONING_CONFIGURING) transition(controller, NOTE4_PROVISIONING_EVENT_OK);
        transition(controller, NOTE4_PROVISIONING_EVENT_OK);
        if (controller->state.state == NOTE4_PROVISIONING_CONNECTING) schedule_connect(controller);
        changed = true;
        break;
    default: status = NOTE4_BLE_ERR_STATE; break;
    }
    controller->cached = true;
    controller->cached_conn = event->conn_handle;
    controller->cached_id = event->frame.request_id;
    controller->cached_crc = crc;
    controller->cached_opcode = event->frame.opcode;
    controller->cached_slot = event->frame.slot;
    controller->cached_status = status;
    if (controller->transaction_active) {
        controller->transaction_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(NOTE4_TRANSACTION_TIMEOUT_MS);
    }
    (void)note4_ble_provisioning_ack(event->conn_handle, event->frame.request_id, status);
    if (return_status && status == NOTE4_BLE_OK) send_status(controller, event->frame.request_id);
    if (changed) publish_state(controller);
}

static void process_wifi(controller_t *controller, const note4_wifi_event_t *event) {
    if (event->type == NOTE4_WIFI_EVENT_SCAN_DONE) {
        note4_wifi_scan_result_t results[32];
        size_t count = 0U;
        if (note4_wifi_manager_get_scan_results(results, 32U, &count) == ESP_OK && controller->scan_request != 0U) {
            for (size_t index = 0U; index < count; ++index) {
                if (results[index].channel > 14U || results[index].ssid[0] == '\0') continue;
                uint8_t payload[NOTE4_WIFI_SSID_MAX + 4U];
                const size_t length = strlen(results[index].ssid);
                payload[0] = length;
                memcpy(payload + 1U, results[index].ssid, length);
                payload[length + 1U] = (uint8_t)results[index].rssi;
                payload[length + 2U] = results[index].channel;
                payload[length + 3U] = results[index].secure;
                notify(NOTE4_BLE_OP_SCAN_RESULT, controller->scan_request, 0U, payload, length + 4U);
            }
            notify(NOTE4_BLE_OP_SCAN_DONE, controller->scan_request, 0U, NULL, 0U);
        }
        controller->scan_request = 0U;
        return;
    }
    if (event->attempt != controller->attempt || controller->waiting_to_connect ||
        (controller->state.state != NOTE4_PROVISIONING_CONNECTING &&
         controller->state.state != NOTE4_PROVISIONING_CONNECTED)) return;
    if (event->type == NOTE4_WIFI_EVENT_GOT_IP) {
        transition(controller, NOTE4_PROVISIONING_EVENT_GOT_IP);
        publish_state(controller);
    } else if (event->type == NOTE4_WIFI_EVENT_DISCONNECTED) {
        connection_failed(controller);
    }
}

static void coordinator_task(void *arg) {
    controller_t *controller = arg;
    note4_wifi_config_clear(&controller->committed);
    note4_wifi_config_clear(&controller->staged);
    bool found = false;
    ESP_ERROR_CHECK(note4_config_store_load(&controller->committed, &found));
    note4_provisioning_init(&controller->state);
    transition(controller, found ? NOTE4_PROVISIONING_EVENT_CONFIG_LOADED : NOTE4_PROVISIONING_EVENT_NO_CONFIG);
    if (found) {
        ESP_ERROR_CHECK(note4_wifi_manager_set_config(&controller->committed));
        schedule_connect(controller);
    }
    ESP_ERROR_CHECK(note4_screen_init(enqueue_action, controller));
    publish_state(controller);
    for (;;) {
        note4_ble_event_t ble;
        if (note4_ble_provisioning_receive(&ble, 0U)) {
            process_ble(controller, &ble);
            memset(&ble, 0, sizeof(ble));
        }
        coord_event_t event;
        if (xQueueReceive(controller->queue, &event, pdMS_TO_TICKS(20)) == pdTRUE) {
            switch (event.type) {
            case NOTE4_COORD_WIFI: process_wifi(controller, &event.data.wifi); break;
            case NOTE4_COORD_ACTION: action(controller, event.data.action); break;
            case NOTE4_COORD_BUTTON:
                if (event.data.button.button == NOTE4_BUTTON_DOWN &&
                    event.data.button.type == NOTE4_BUTTON_EVENT_LONG_PRESS) {
                    action(controller, NOTE4_PROVISIONING_EVENT_LONG_DOWN);
                } else if (event.data.button.type == NOTE4_BUTTON_EVENT_PRESS) {
                    note4_screen_button(event.data.button.button);
                }
                break;
            }
        }
        if (controller->transaction_active && due(controller->transaction_deadline)) discard_staged(controller);
        if (controller->state.state == NOTE4_PROVISIONING_CONNECTING && due(controller->connect_deadline)) {
            if (controller->waiting_to_connect) {
                controller->waiting_to_connect = false;
                const esp_err_t err = note4_wifi_manager_connect(controller->state.slot);
                controller->attempt = note4_wifi_manager_attempt();
                controller->connect_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(NOTE4_CONNECT_TIMEOUT_MS);
                if (err != ESP_OK) connection_failed(controller);
            } else {
                (void)note4_wifi_manager_disconnect();
                connection_failed(controller);
            }
        }
    }
}

void app_main(void) {
    const gpio_config_t latch = {
        .pin_bit_mask = 1ULL << NOTE4_VBAT_LATCH, .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&latch));
    gpio_hold_dis(NOTE4_VBAT_LATCH);
    gpio_set_level(NOTE4_VBAT_LATCH, 1);
    gpio_hold_en(NOTE4_VBAT_LATCH);
    ESP_ERROR_CHECK(note4_config_store_init());
    s_controller.queue = xQueueCreate(32, sizeof(coord_event_t));
    ESP_ERROR_CHECK(s_controller.queue == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    ESP_ERROR_CHECK(note4_wifi_manager_init(enqueue_wifi, &s_controller));
    ESP_ERROR_CHECK(note4_ble_provisioning_init());
    ESP_ERROR_CHECK(note4_button_input_init(enqueue_button, &s_controller));
    ESP_ERROR_CHECK(xTaskCreate(coordinator_task, "note4_coord", 16384, &s_controller, 6, NULL)
                    == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
