#include <stdio.h>
#include <stdlib.h>
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
#include "magpie_worker.h"
#include "account_directory.h"
#include "display_prefs.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "nvs.h"
#include "resource_budget.h"

#define NOTE4_VBAT_LATCH GPIO_NUM_17
#define NOTE4_CONNECT_TIMEOUT_MS 15000U
#define NOTE4_TRANSACTION_TIMEOUT_MS 120000U

static const char *TAG = "note4";

typedef enum { NOTE4_COORD_WIFI, NOTE4_COORD_BUTTON, NOTE4_COORD_ACTION, NOTE4_COORD_MAGPIE, NOTE4_COORD_PUBLISHED } coord_type_t;
typedef struct {
    coord_type_t type;
    union {
        note4_wifi_event_t wifi;
        note4_magpie_event_t magpie;
        note4_button_event_t button;
        note4_provisioning_event_type_t action;
    } data;
} coord_event_t;

typedef struct {
    note4_settings_t committed;
    note4_settings_t staged;
    note4_account_directory_t directory;
    note4_display_prefs_t *prefs, *staged_prefs;
    note4_display_prefs_upload_t prefs_upload;
    note4_provisioning_snapshot_t state;
    QueueHandle_t queue;
    QueueHandle_t publications;
    QueueHandle_t publication_results;
    bool screen_started;
    bool transaction_active;
    bool staged_wifi_changed;
    TickType_t transaction_deadline;
    bool waiting_to_connect;
    TickType_t connect_deadline;
    uint32_t attempt;
    unsigned magpie_epoch;
    uint16_t scan_request;
    bool cached;
    uint16_t cached_conn;
    uint16_t cached_id;
    uint16_t cached_length;
    uint8_t cached_payload[NOTE4_BLE_MAX_TRANSACTION];
    uint8_t cached_opcode;
    uint8_t cached_slot;
    note4_ble_status_t cached_status;
    uint8_t cached_response[NOTE4_BLE_MAX_PAYLOAD];
    size_t cached_response_length;
    uint8_t cached_response_opcode;
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
    memcpy(controller->state.display_timezone, controller->committed.web.timezone, sizeof(controller->state.display_timezone));
    controller->state.display_layout = controller->committed.web.layout;
    controller->state.adaptive_font = controller->committed.web.adaptive_font;
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
                             note4_wifi_config_enabled_mask(&controller->committed.wifi));
}

static void enqueue_wifi(const note4_wifi_event_t *event, void *context) {
    controller_t *controller = context;
    const coord_event_t queued = {.type = NOTE4_COORD_WIFI, .data.wifi = *event};
    (void)xQueueSend(controller->queue, &queued, 0U);
}

static bool quota_published(note4_quota_snapshot_t *snapshot, unsigned epoch, bool accepted, void *context) {
    controller_t *controller = context;
    const coord_event_t queued = {.type = NOTE4_COORD_PUBLISHED, .data.magpie = {
        .epoch = epoch, .snapshot = accepted ? snapshot : NULL, .result = accepted ? NOTE4_MAGPIE_OK : NOTE4_MAGPIE_RESOURCE_EXHAUSTED}};
    if (!accepted) {
        note4_magpie_worker_published(epoch, false);
        return false;
    }
    note4_quota_snapshot_retain(snapshot);
    /* Only the screen produces accepted publications; the coordinator never waits
     * on its own event queue when replacing a pending candidate. */
    (void)xQueueSend(controller->publications, &queued, portMAX_DELAY);
    bool committed = false;
    (void)xQueueReceive(controller->publication_results, &committed, portMAX_DELAY);
    return committed;
}

static bool enqueue_magpie(const note4_magpie_event_t *event, void *context) {
    controller_t *controller = context;
    const coord_event_t queued = {.type = NOTE4_COORD_MAGPIE, .data.magpie = *event};
    note4_quota_snapshot_retain(event->snapshot);
    if (xQueueSend(controller->queue, &queued, pdMS_TO_TICKS(100)) == pdTRUE) return true;
    note4_quota_snapshot_release(event->snapshot);
    return false;
}

static void pause_magpie(controller_t *controller) {
    controller->magpie_epoch = note4_magpie_worker_update(&controller->committed, NOTE4_POLL_SETUP);
    controller->state.magpie_phase = NOTE4_MAGPIE_IDLE;
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
    note4_display_prefs_upload_clear(&controller->prefs_upload);
    note4_display_prefs_release(controller->staged_prefs); controller->staged_prefs = NULL;
    memset(&controller->staged, 0, sizeof(controller->staged));
    controller->transaction_active = false;
    controller->cached = false;
    memset(controller->cached_payload, 0, sizeof(controller->cached_payload));
    controller->staged_wifi_changed = false;
    controller->state.has_staged_config = false;
}

static void schedule_connect(controller_t *controller) {
    controller->waiting_to_connect = true;
    controller->connect_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(750U);
}

static void connection_failed(controller_t *controller) {
    controller->magpie_epoch = note4_magpie_worker_update(&controller->committed, NOTE4_POLL_OFFLINE);
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
        pause_magpie(controller);
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
    payload[offset++] = note4_wifi_config_enabled_mask(&controller->committed.wifi);
    for (size_t index = 0U; index < NOTE4_WIFI_MAX_SLOTS; ++index) {
        const note4_wifi_slot_t *slot = &controller->committed.wifi.slots[index];
        payload[offset++] = slot->ssid_len;
        memcpy(payload + offset, slot->ssid, slot->ssid_len);
        offset += slot->ssid_len;
        payload[offset++] = slot->password_len != 0U;
    }
    notify(NOTE4_BLE_OP_STATUS, request, 0U, payload, offset);
}

static void send_magpie_status(controller_t *controller, uint16_t request) {
    uint8_t payload[NOTE4_MAGPIE_ENDPOINT_MAX + NOTE4_TIMEZONE_MAX + 12U];
    const size_t length = note4_settings_web_metadata(&controller->committed, payload, sizeof(payload));
    if (length != 0U) notify(NOTE4_BLE_OP_WEB_SETTINGS, request, 0U, payload, length);
}

static bool apply_slot(controller_t *controller, const note4_ble_event_t *event) {
    if (event->frame.slot >= NOTE4_WIFI_MAX_SLOTS) return false;
    note4_ble_set_slot_t slot;
    if (!note4_ble_decode_set_slot(event->payload, event->frame.payload_length, &slot)) return false;
    const note4_wifi_slot_t *old = &controller->committed.wifi.slots[event->frame.slot];
    const char *password = slot.password_mode == NOTE4_BLE_PASSWORD_SET ? slot.password : "";
    if (slot.enabled && slot.password_mode == NOTE4_BLE_PASSWORD_UNCHANGED) {
        if (!old->enabled || old->ssid_len != slot.ssid_length ||
            memcmp(old->ssid, slot.ssid, slot.ssid_length) != 0) return false;
        password = old->password;
    }
    const bool valid = note4_wifi_slot_set(&controller->staged.wifi, event->frame.slot,
                                          slot.enabled ? slot.ssid : "", slot.enabled ? password : "", slot.enabled);
    memset(&slot, 0, sizeof(slot));
    return valid;
}

static void process_ble(controller_t *controller, const note4_ble_event_t *event) {
    if (event->type == NOTE4_BLE_EVENT_ACTIVITY) {
        if (controller->transaction_active) {
            if (due(controller->transaction_deadline)) discard_staged(controller);
            else {
                controller->transaction_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(NOTE4_TRANSACTION_TIMEOUT_MS);
                if (controller->prefs_upload.bytes != NULL)
                    controller->prefs_upload.active_at = (uint64_t)(esp_timer_get_time() / 1000);
            }
        }
        return;
    }
    if (event->type == NOTE4_BLE_EVENT_DISCONNECTED) {
        discard_staged(controller);
        controller->cached = false;
        note4_account_directory_close(&controller->directory);
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
    if (controller->transaction_active && due(controller->transaction_deadline)) discard_staged(controller);
    note4_account_directory_expire(&controller->directory, (uint64_t)(esp_timer_get_time() / 1000));
    if ((event->frame.opcode == NOTE4_BLE_OP_OPEN_ACCOUNT_DIRECTORY ||
         event->frame.opcode == NOTE4_BLE_OP_READ_ACCOUNT_DIRECTORY) && controller->directory.view == NULL)
        controller->cached = false;
    if (controller->cached && controller->cached_conn == event->conn_handle &&
        controller->cached_id == event->frame.request_id) {
        const bool same = controller->cached_opcode == event->frame.opcode &&
            controller->cached_slot == event->frame.slot && controller->cached_length == event->frame.payload_length &&
            memcmp(controller->cached_payload, event->payload, event->frame.payload_length) == 0;
        if (same && controller->transaction_active) {
            controller->transaction_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(NOTE4_TRANSACTION_TIMEOUT_MS);
            if (controller->prefs_upload.bytes != NULL)
                controller->prefs_upload.active_at = (uint64_t)(esp_timer_get_time() / 1000);
        }
        if (same && controller->directory.view != NULL &&
            (event->frame.opcode == NOTE4_BLE_OP_OPEN_ACCOUNT_DIRECTORY || event->frame.opcode == NOTE4_BLE_OP_READ_ACCOUNT_DIRECTORY))
            controller->directory.active_at = (uint64_t)(esp_timer_get_time() / 1000);
        (void)note4_ble_provisioning_ack(event->conn_handle, event->frame.request_id,
            same ? controller->cached_status : NOTE4_BLE_ERR_SEQUENCE);
        if (same && (event->frame.opcode == NOTE4_BLE_OP_BEGIN || event->frame.opcode == NOTE4_BLE_OP_GET_STATUS)) {
            send_status(controller, event->frame.request_id);
        }
        if (same && event->frame.opcode == NOTE4_BLE_OP_GET_WEB_SETTINGS) send_magpie_status(controller, event->frame.request_id);
        if (same && controller->cached_status == NOTE4_BLE_OK && controller->cached_response_length != 0U)
            notify(controller->cached_response_opcode, event->frame.request_id, 0U,
                controller->cached_response, controller->cached_response_length);
        return;
    }
    note4_ble_status_t status = NOTE4_BLE_OK;
    controller->cached_response_length = 0U;
    bool return_magpie = false;
    bool return_status = false;
    bool changed = false;
    if (!configuring(controller)) status = NOTE4_BLE_ERR_STATE;
    else switch (event->frame.opcode) {
    case NOTE4_BLE_OP_BEGIN:
        if (event->frame.payload_length != 5U || event->payload[0] != NOTE4_WEB_SCHEMA) {
            status = NOTE4_BLE_ERR_UNSUPPORTED_SCHEMA;
            break;
        }
        const uint32_t revision = event->payload[1] | ((uint32_t)event->payload[2] << 8U) |
            ((uint32_t)event->payload[3] << 16U) | ((uint32_t)event->payload[4] << 24U);
        if (revision != controller->committed.revision) {
            status = NOTE4_BLE_ERR_STALE_REVISION;
            break;
        }
        discard_staged(controller);
        controller->staged = controller->committed;
        controller->transaction_active = true;
        controller->staged_wifi_changed = false;
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
        else controller->staged_wifi_changed = true;
        break;
    case NOTE4_BLE_OP_GET_MAGPIE:
    case NOTE4_BLE_OP_SET_MAGPIE:
        status = NOTE4_BLE_ERR_UNSUPPORTED_SCHEMA;
        break;
    case NOTE4_BLE_OP_GET_WEB_SETTINGS:
        return_magpie = true;
        break;
    case NOTE4_BLE_OP_SET_WEB_SETTINGS: {
        if (!controller->transaction_active) { status = NOTE4_BLE_ERR_STATE; break; }
        const note4_settings_result_t result = note4_settings_apply_web(&controller->staged,
            &controller->committed, event->payload, event->frame.payload_length);
        if (result == NOTE4_SETTINGS_UNSUPPORTED_SCHEMA) status = NOTE4_BLE_ERR_UNSUPPORTED_SCHEMA;
        else if (result == NOTE4_SETTINGS_STALE_REVISION) status = NOTE4_BLE_ERR_STALE_REVISION;
        else if (result != NOTE4_SETTINGS_OK) status = NOTE4_BLE_ERR_VALIDATION;
        break;
    }
    case NOTE4_BLE_OP_OPEN_ACCOUNT_DIRECTORY:
        if (event->frame.payload_length != 1U || event->payload[0] != NOTE4_DIRECTORY_SCHEMA) {
            status = NOTE4_BLE_ERR_UNSUPPORTED_SCHEMA; break;
        }
        status = note4_account_directory_open(&controller->directory, controller->committed.revision,
            (uint64_t)(esp_timer_get_time() / 1000), controller->prefs != NULL ? controller->prefs->bytes : NULL,
            controller->prefs != NULL ? controller->prefs->length : 0U, controller->cached_response);
        if (status == NOTE4_BLE_OK) {
            controller->cached_response_length = 33U;
            controller->cached_response_opcode = NOTE4_BLE_OP_ACCOUNT_DIRECTORY;
        }
        break;
    case NOTE4_BLE_OP_READ_ACCOUNT_DIRECTORY: {
        if (event->frame.payload_length != 13U || event->payload[0] != NOTE4_DIRECTORY_SCHEMA) {
            status = NOTE4_BLE_ERR_UNSUPPORTED_SCHEMA; break;
        }
        uint32_t values[3] = {0};
        for (unsigned i = 0U; i < 3U; ++i)
            for (unsigned b = 0U; b < 4U; ++b) values[i] |= (uint32_t)event->payload[1U + i * 4U + b] << (b * 8U);
        status = note4_account_directory_read(&controller->directory, values[0], values[1], values[2],
            (uint64_t)(esp_timer_get_time() / 1000), controller->cached_response,
            sizeof(controller->cached_response), &controller->cached_response_length);
        controller->cached_response_opcode = NOTE4_BLE_OP_ACCOUNT_DIRECTORY_CHUNK;
        break;
    }
    case NOTE4_BLE_OP_BEGIN_DISPLAY_PREFS: {
        if (!controller->transaction_active) { status = NOTE4_BLE_ERR_STATE; break; }
        if (event->frame.payload_length != 13U || event->payload[0] != 2U) { status = NOTE4_BLE_ERR_UNSUPPORTED_SCHEMA; break; }
        uint32_t values[3] = {0};
        for (unsigned i = 0U; i < 3U; ++i)
            for (unsigned b = 0U; b < 4U; ++b) values[i] |= (uint32_t)event->payload[1U + i * 4U + b] << (b * 8U);
        if (values[0] != controller->committed.revision) { status = NOTE4_BLE_ERR_STALE_REVISION; break; }
        status = note4_display_prefs_begin(&controller->prefs_upload, values[0], values[1], values[2],
            (uint64_t)(esp_timer_get_time() / 1000));
        if (status == NOTE4_BLE_OK) {
            note4_display_prefs_release(controller->staged_prefs); controller->staged_prefs = NULL;
        }
        break;
    }
    case NOTE4_BLE_OP_PUT_DISPLAY_PREFS_CHUNK: {
        if (!controller->transaction_active) { status = NOTE4_BLE_ERR_STATE; break; }
        if (event->frame.payload_length <= 5U || event->payload[0] != 2U) { status = NOTE4_BLE_ERR_LENGTH; break; }
        uint32_t offset = 0U;
        for (unsigned b = 0U; b < 4U; ++b) offset |= (uint32_t)event->payload[1U + b] << (b * 8U);
        status = note4_display_prefs_put(&controller->prefs_upload, offset, event->payload + 5U,
            event->frame.payload_length - 5U, (uint64_t)(esp_timer_get_time() / 1000));
        break;
    }
    case NOTE4_BLE_OP_FINISH_DISPLAY_PREFS: {
        if (!controller->transaction_active) { status = NOTE4_BLE_ERR_STATE; break; }
        if (event->frame.payload_length != 1U || event->payload[0] != 2U) { status = NOTE4_BLE_ERR_UNSUPPORTED_SCHEMA; break; }
        note4_display_prefs_t *candidate = NULL;
        status = note4_display_prefs_finish(&controller->prefs_upload, controller->committed.revision,
            (uint64_t)(esp_timer_get_time() / 1000), &candidate);
        if (status == NOTE4_BLE_OK) {
            note4_display_prefs_release(controller->staged_prefs); controller->staged_prefs = candidate;
        }
        break;
    }
    case NOTE4_BLE_OP_COMMIT:
        if (controller->state.state != NOTE4_PROVISIONING_CONFIGURING ||
            !controller->transaction_active || (controller->prefs_upload.bytes != NULL && controller->staged_prefs == NULL) ||
            !note4_settings_validate(&controller->staged) ||
            (controller->staged_wifi_changed && !note4_wifi_config_validate(&controller->staged.wifi))) {
            status = NOTE4_BLE_ERR_VALIDATION;
        } else {
            transition(controller, NOTE4_PROVISIONING_EVENT_COMMIT_ACCEPTED);
            publish_state(controller);
            controller->staged.revision = controller->committed.revision + 1U;
            const bool instance_changed = strcmp(controller->committed.magpie.endpoint, controller->staged.magpie.endpoint) != 0;
            const note4_display_prefs_t *next_prefs = instance_changed ? NULL
                : controller->staged_prefs != NULL ? controller->staged_prefs : controller->prefs;
            const esp_err_t saved = controller->committed.revision == UINT32_MAX ? ESP_ERR_NO_MEM :
                note4_config_store_save_bundle(&controller->staged, next_prefs != NULL ? next_prefs->bytes : NULL,
                    next_prefs != NULL ? next_prefs->length : 0U);
            if (saved != ESP_OK) {
                transition(controller, NOTE4_PROVISIONING_EVENT_COMMIT_FAILED);
                status = saved == ESP_ERR_NO_MEM || saved == ESP_ERR_NVS_NOT_ENOUGH_SPACE
                    ? NOTE4_BLE_ERR_RESOURCE_EXHAUSTED : NOTE4_BLE_ERR_BUSY;
            } else {
                if (strcmp(controller->committed.magpie.endpoint, controller->staged.magpie.endpoint) != 0) {
                    note4_screen_publish_quotas(NULL);
                    note4_account_directory_clear(&controller->directory);
                }
                if (instance_changed || controller->staged_prefs != NULL) {
                    note4_display_prefs_release(controller->prefs);
                    controller->prefs = instance_changed ? NULL : controller->staged_prefs;
                    if (!instance_changed) controller->staged_prefs = NULL;
                }
                note4_screen_publish_prefs(controller->prefs);
                controller->committed = controller->staged;
                discard_staged(controller);
                if (note4_wifi_config_enabled_mask(&controller->committed.wifi) != 0U) {
                    ESP_ERROR_CHECK(note4_wifi_manager_set_config(&controller->committed.wifi));
                }
                transition(controller, NOTE4_PROVISIONING_EVENT_COMMIT_SAVED);
            }
            changed = true;
        }
        break;
    case NOTE4_BLE_OP_EXIT_SETUP:
    case NOTE4_BLE_OP_ABORT:
        controller->state.pairing = false;
        controller->state.passkey = 0U;
        controller->waiting_to_connect = false;
        discard_staged(controller);
        note4_account_directory_close(&controller->directory);
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
    memset(controller->cached_payload, 0, sizeof(controller->cached_payload));
    controller->cached_length = event->frame.payload_length;
    memcpy(controller->cached_payload, event->payload, event->frame.payload_length);
    controller->cached_opcode = event->frame.opcode;
    controller->cached_slot = event->frame.slot;
    controller->cached_status = status;
    if (controller->transaction_active) {
        controller->transaction_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(NOTE4_TRANSACTION_TIMEOUT_MS);
    }
    (void)note4_ble_provisioning_ack(event->conn_handle, event->frame.request_id, status);
    if (return_status && status == NOTE4_BLE_OK) send_status(controller, event->frame.request_id);
    if (return_magpie && status == NOTE4_BLE_OK) send_magpie_status(controller, event->frame.request_id);
    if (status == NOTE4_BLE_OK && controller->cached_response_length != 0U)
        notify(controller->cached_response_opcode, event->frame.request_id, 0U,
            controller->cached_response, controller->cached_response_length);
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
        controller->magpie_epoch = note4_magpie_worker_update(&controller->committed, NOTE4_POLL_ONLINE);
        controller->state.magpie_phase = NOTE4_MAGPIE_SIGNING_IN;
        publish_state(controller);
    } else if (event->type == NOTE4_WIFI_EVENT_DISCONNECTED) {
        connection_failed(controller);
    }
}

static void coordinator_task(void *arg) {
    controller_t *controller = arg;
    note4_settings_clear(&controller->committed);
    note4_settings_clear(&controller->staged);
    bool found = false;
    uint8_t *prefs_bytes = NULL; size_t prefs_length = 0U;
    const esp_err_t loaded = note4_config_store_load_bundle(&controller->committed, &found, &prefs_bytes, &prefs_length);
    if (loaded != ESP_OK || (prefs_length && note4_display_prefs_parse(prefs_bytes, prefs_length, &controller->prefs) != NOTE4_BLE_OK)) {
        ESP_LOGE(TAG, "stored configuration could not be loaded; entering setup");
        note4_settings_clear(&controller->committed); found = false;
    }
    free(prefs_bytes);
    note4_provisioning_init(&controller->state);
    const bool has_wifi = found && note4_wifi_config_enabled_mask(&controller->committed.wifi) != 0U;
    transition(controller, has_wifi ? NOTE4_PROVISIONING_EVENT_CONFIG_LOADED : NOTE4_PROVISIONING_EVENT_NO_CONFIG);
    if (has_wifi) {
        ESP_ERROR_CHECK(note4_wifi_manager_set_config(&controller->committed.wifi));
        schedule_connect(controller);
    }
    if (!controller->screen_started) {
        ESP_ERROR_CHECK(note4_screen_init(enqueue_action, controller));
        controller->screen_started = true;
    }
    note4_screen_publish_prefs(controller->prefs);
    publish_state(controller);
    int64_t next_diagnostic = 0;
    for (;;) {
        if (esp_timer_get_time() >= next_diagnostic) {
            note4_resource_log("coordinator", 0U);
            next_diagnostic = esp_timer_get_time() + 60000000;
        }
        note4_ble_event_t ble;
        if (note4_ble_provisioning_receive(&ble, 0U)) {
            process_ble(controller, &ble);
            memset(&ble, 0, sizeof(ble));
        }
        coord_event_t event;
        if (xQueueReceive(controller->publications, &event, 0U) == pdTRUE ||
            xQueueReceive(controller->queue, &event, pdMS_TO_TICKS(20)) == pdTRUE) {
            switch (event.type) {
            case NOTE4_COORD_WIFI: process_wifi(controller, &event.data.wifi); break;
            case NOTE4_COORD_MAGPIE:
                if (!configuring(controller) && event.data.magpie.epoch == controller->magpie_epoch) {
                    controller->state.magpie_phase = event.data.magpie.phase;
                    controller->state.magpie_result = event.data.magpie.result;
                    controller->state.magpie_error_page = event.data.magpie.error_page;
                    controller->state.magpie_reading = event.data.magpie.reading;
                    controller->state.magpie_retry_seconds = event.data.magpie.retry_seconds;
                    if (event.data.magpie.snapshot != NULL) {
                        note4_screen_stage_quotas(event.data.magpie.snapshot, event.data.magpie.epoch, quota_published, controller);
                    }
                    publish_state(controller);
                }
                note4_quota_snapshot_release(event.data.magpie.snapshot);
                break;
            case NOTE4_COORD_PUBLISHED: {
                const bool accepted = !configuring(controller) && event.data.magpie.epoch == controller->magpie_epoch &&
                    event.data.magpie.result == NOTE4_MAGPIE_OK;
                if (accepted) note4_account_directory_update(&controller->directory, event.data.magpie.snapshot);
                note4_magpie_worker_published(event.data.magpie.epoch, accepted);
                (void)xQueueSend(controller->publication_results, &accepted, 0U);
                note4_quota_snapshot_release(event.data.magpie.snapshot);
                break;
            }
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
        note4_account_directory_expire(&controller->directory, (uint64_t)(esp_timer_get_time() / 1000));
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

static void wait_for_storage(controller_t *controller) {
    esp_err_t err = note4_config_store_init();
    if (err == ESP_OK) return;
    note4_provisioning_init(&controller->state);
    controller->state.storage_error = true;
    ESP_ERROR_CHECK(note4_screen_init(enqueue_action, controller));
    controller->screen_started = true;
    ESP_LOGE(TAG, "settings storage unavailable; waiting for retry");
    (void)note4_screen_render(&controller->state);
    while (err != ESP_OK) {
        coord_event_t event;
        if (xQueueReceive(controller->queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        if ((event.type == NOTE4_COORD_ACTION && event.data.action == NOTE4_PROVISIONING_EVENT_OK) ||
            (event.type == NOTE4_COORD_BUTTON && event.data.button.button == NOTE4_BUTTON_OK &&
             event.data.button.type == NOTE4_BUTTON_EVENT_PRESS)) {
            err = note4_config_store_init();
            if (err != ESP_OK) ESP_LOGE(TAG, "settings storage retry failed");
        }
    }
    controller->state.storage_error = false;
}

void app_main(void) {
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "build version=%s idf=%s elf=%02x%02x%02x%02x%02x%02x%02x%02x",
        app->version, app->idf_ver, app->app_elf_sha256[0], app->app_elf_sha256[1],
        app->app_elf_sha256[2], app->app_elf_sha256[3], app->app_elf_sha256[4],
        app->app_elf_sha256[5], app->app_elf_sha256[6], app->app_elf_sha256[7]);
    const gpio_config_t latch = {
        .pin_bit_mask = 1ULL << NOTE4_VBAT_LATCH, .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&latch));
    gpio_hold_dis(NOTE4_VBAT_LATCH);
    gpio_set_level(NOTE4_VBAT_LATCH, 1);
    gpio_hold_en(NOTE4_VBAT_LATCH);
    s_controller.queue = xQueueCreate(32, sizeof(coord_event_t));
    s_controller.publications = xQueueCreate(1, sizeof(coord_event_t));
    s_controller.publication_results = xQueueCreate(1, sizeof(bool));
    ESP_ERROR_CHECK(s_controller.queue == NULL || s_controller.publications == NULL ||
        s_controller.publication_results == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    ESP_ERROR_CHECK(note4_button_input_init(enqueue_button, &s_controller));
    wait_for_storage(&s_controller);
    ESP_ERROR_CHECK(note4_wifi_manager_init(enqueue_wifi, &s_controller));
    ESP_ERROR_CHECK(note4_ble_provisioning_init());
    ESP_ERROR_CHECK(note4_magpie_worker_init(enqueue_magpie, &s_controller));
    ESP_ERROR_CHECK(xTaskCreate(coordinator_task, "note4_coord", 16384, &s_controller, 6, NULL)
                    == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
