#include "ble_provisioning.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

static const char *TAG = "note4_ble";
extern void ble_store_config_init(void);
static QueueHandle_t s_events;
static StaticQueue_t s_event_queue;
#define NOTE4_BLE_EVENT_CAPACITY 12U
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_status_handle;
static bool s_status_subscribed;
static atomic_bool s_enabled;
static atomic_bool s_synced;
static uint8_t s_addr_type;
static char s_device_name[sizeof("magpie-usage-000000")];

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(
    0x00, 0x34, 0x45, 0x54, 0x4f, 0x4e, 0x4d, 0x9a,
    0x54, 0x4d, 0x6e, 0x5b, 0x00, 0x00, 0x2e, 0x7c);
static const ble_uuid128_t s_control_uuid = BLE_UUID128_INIT(
    0x00, 0x34, 0x45, 0x54, 0x4f, 0x4e, 0x4d, 0x9a,
    0x54, 0x4d, 0x6e, 0x5b, 0x01, 0x00, 0x2e, 0x7c);
static const ble_uuid128_t s_data_uuid = BLE_UUID128_INIT(
    0x00, 0x34, 0x45, 0x54, 0x4f, 0x4e, 0x4d, 0x9a,
    0x54, 0x4d, 0x6e, 0x5b, 0x02, 0x00, 0x2e, 0x7c);
static const ble_uuid128_t s_status_uuid = BLE_UUID128_INIT(
    0x00, 0x34, 0x45, 0x54, 0x4f, 0x4e, 0x4d, 0x9a,
    0x54, 0x4d, 0x6e, 0x5b, 0x03, 0x00, 0x2e, 0x7c);
static uint16_t s_control_handle;
static uint16_t s_data_handle;

static struct {
    bool active;
    uint16_t request_id;
    uint8_t opcode;
    uint8_t slot;
    uint8_t total;
    uint8_t next;
    TickType_t deadline;
    size_t length;
    uint8_t payload[NOTE4_BLE_MAX_TRANSACTION];
} s_rx;

static void start_advertising(void);

static bool connection_is_authenticated(uint16_t conn_handle) {
    struct ble_gap_conn_desc desc;
    return ble_gap_conn_find(conn_handle, &desc) == 0 &&
        desc.sec_state.encrypted && desc.sec_state.authenticated;
}

static int characteristic_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR ||
        (attr_handle != s_control_handle && attr_handle != s_data_handle)) return BLE_ATT_ERR_UNLIKELY;
    if (s_conn_handle != conn_handle || !connection_is_authenticated(conn_handle)) {
        (void)ble_gap_security_initiate(conn_handle);
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    uint8_t encoded[NOTE4_BLE_MAX_FRAME];
    uint16_t length = OS_MBUF_PKTLEN(ctxt->om);
    if (length > sizeof(encoded) || os_mbuf_copydata(ctxt->om, 0, length, encoded) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    note4_ble_event_t event = {.conn_handle = conn_handle};
    const note4_ble_status_t status = note4_ble_frame_decode(
        encoded, length, &event.frame, event.payload, sizeof(event.payload));
    if (status != NOTE4_BLE_OK) return BLE_ATT_ERR_INVALID_PDU;
    if (s_rx.active && (int32_t)(xTaskGetTickCount() - s_rx.deadline) >= 0) {
        memset(&s_rx, 0, sizeof(s_rx));
    }
    if (event.frame.sequence == 0U) {
        memset(&s_rx, 0, sizeof(s_rx));
        s_rx.active = true;
        s_rx.request_id = event.frame.request_id;
        s_rx.opcode = event.frame.opcode;
        s_rx.slot = event.frame.slot;
        s_rx.total = event.frame.total_sequences;
    }
    if (!s_rx.active || s_rx.request_id != event.frame.request_id ||
        s_rx.opcode != event.frame.opcode || s_rx.slot != event.frame.slot ||
        s_rx.total != event.frame.total_sequences || s_rx.next != event.frame.sequence ||
        s_rx.length + event.frame.payload_length > sizeof(s_rx.payload)) {
        memset(&s_rx, 0, sizeof(s_rx));
        return BLE_ATT_ERR_INVALID_PDU;
    }
    memcpy(s_rx.payload + s_rx.length, event.payload, event.frame.payload_length);
    s_rx.length += event.frame.payload_length;
    s_rx.next++;
    s_rx.deadline = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
    if (s_rx.next != s_rx.total) {
        const note4_ble_event_t activity = {.type = NOTE4_BLE_EVENT_ACTIVITY, .conn_handle = conn_handle};
        if (xQueueSend(s_events, &activity, 0) != pdTRUE) {
            memset(&s_rx, 0, sizeof(s_rx));
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;
    }
    event.frame.sequence = 0U;
    event.frame.total_sequences = 1U;
    event.frame.payload_length = s_rx.length;
    memcpy(event.payload, s_rx.payload, s_rx.length);
    event.frame.payload = NULL;
    memset(&s_rx, 0, sizeof(s_rx));
    if (xQueueSend(s_events, &event, 0) != pdTRUE) return BLE_ATT_ERR_INSUFFICIENT_RES;
    /* The coordinator ACKs after validation and durable COMMIT. */
    return 0;
}

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {.uuid = &s_control_uuid.u, .access_cb = characteristic_access,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN, .val_handle = &s_control_handle},
            {.uuid = &s_data_uuid.u, .access_cb = characteristic_access,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN, .val_handle = &s_data_handle},
            {.uuid = &s_status_uuid.u, .access_cb = characteristic_access,
             .flags = BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_status_handle},
            {0},
        },
    },
    {0},
};

static void on_reset(int reason) {
    atomic_store(&s_synced, false);
    ESP_LOGW(TAG, "NimBLE reset: %d", reason);
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    note4_ble_event_t queued = {0};
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            (void)ble_gap_security_initiate(s_conn_handle);
        } else start_advertising();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        queued.type = NOTE4_BLE_EVENT_DISCONNECTED;
        queued.conn_handle = event->disconnect.conn.conn_handle;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_status_subscribed = false;
        memset(&s_rx, 0, sizeof(s_rx));
        (void)xQueueSend(s_events, &queued, 0);
        start_advertising();
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        if (event->passkey.params.action != BLE_SM_IOACT_DISP) return BLE_HS_ENOTSUP;
        struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = esp_random() % 1000000U};
        queued.type = NOTE4_BLE_EVENT_PASSKEY;
        queued.passkey = io.passkey;
        queued.conn_handle = event->passkey.conn_handle;
        if (xQueueSend(s_events, &queued, 0) != pdTRUE) return BLE_HS_ENOMEM;
        return ble_sm_inject_io(event->passkey.conn_handle, &io);
    }
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status != 0 || !connection_is_authenticated(event->enc_change.conn_handle)) {
            (void)ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_AUTH_FAIL);
        } else {
            queued.type = NOTE4_BLE_EVENT_PAIRED;
            queued.conn_handle = event->enc_change.conn_handle;
            (void)xQueueSend(s_events, &queued, 0);
        }
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* A stale bond must be removed explicitly in the central's Bluetooth settings. */
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_status_handle) s_status_subscribed = event->subscribe.cur_notify != 0;
        break;
    default: break;
    }
    return 0;
}

static void start_advertising(void) {
    if (!atomic_load(&s_synced) || !atomic_load(&s_enabled) ||
        s_conn_handle != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active()) return;
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc == 0) {
        /* The full name and 128-bit service UUID cannot share a 31-byte advertisement. */
        const struct ble_hs_adv_fields response = {
            .name = (uint8_t *)s_device_name,
            .name_len = strlen(s_device_name),
            .name_is_complete = 1,
        };
        rc = ble_gap_adv_rsp_set_fields(&response);
    }
    if (rc == 0) {
        const struct ble_gap_adv_params params = {
            .conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN};
        rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    }
    if (rc != 0) ESP_LOGE(TAG, "advertising failed: %d", rc);
    else ESP_LOGI(TAG, "advertising as %s", s_device_name);
}

void note4_ble_provisioning_set_enabled(bool enabled) {
    atomic_store(&s_enabled, enabled);
    if (enabled) start_advertising();
    else if (atomic_load(&s_synced)) (void)ble_gap_adv_stop();
}

static void on_sync(void) {
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &s_addr_type) != 0) return;
    atomic_store(&s_synced, true);
    start_advertising();
}

static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

const char *note4_ble_provisioning_name(void) {
    return s_device_name;
}

esp_err_t note4_ble_provisioning_init(void) {
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_BT);
    if (err != ESP_OK) return err;
    snprintf(s_device_name, sizeof(s_device_name), "magpie-usage-%02x%02x%02x",
             (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
    /* These events are exchanged only between tasks. Keep queue control in
     * internal BSS; the copied transaction payloads do not require DMA memory. */
    uint8_t *storage = heap_caps_malloc(NOTE4_BLE_EVENT_CAPACITY * sizeof(note4_ble_event_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (storage == NULL) return ESP_ERR_NO_MEM;
    s_events = xQueueCreateStatic(NOTE4_BLE_EVENT_CAPACITY, sizeof(note4_ble_event_t),
        storage, &s_event_queue);
    if (s_events == NULL) { heap_caps_free(storage); return ESP_ERR_NO_MEM; }
    err = nimble_port_init();
    if (err != ESP_OK) {
        vQueueDelete(s_events); s_events = NULL; heap_caps_free(storage);
        return err;
    }
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_svc_gap_device_name_set(s_device_name) != 0 ||
        ble_gatts_count_cfg(s_services) != 0 || ble_gatts_add_svcs(s_services) != 0) return ESP_FAIL;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

bool note4_ble_provisioning_receive(note4_ble_event_t *event, uint32_t timeout_ms) {
    return event != NULL && xQueueReceive(s_events, event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

esp_err_t note4_ble_provisioning_notify(const note4_ble_frame_t *frame) {
    if (frame == NULL || !s_status_subscribed || s_conn_handle == BLE_HS_CONN_HANDLE_NONE ||
        !connection_is_authenticated(s_conn_handle)) return ESP_ERR_INVALID_STATE;
    /* 8 payload bytes + 12 framing bytes fit even the default ATT MTU of 23. */
    const size_t chunk_size = 8U;
    const size_t count = frame->payload_length == 0U ? 1U : (frame->payload_length + chunk_size - 1U) / chunk_size;
    if (count > 255U) return ESP_ERR_INVALID_SIZE;
    for (size_t sequence = 0U; sequence < count; ++sequence) {
        note4_ble_frame_t chunk = *frame;
        const size_t offset = sequence * chunk_size;
        chunk.sequence = sequence;
        chunk.total_sequences = count;
        chunk.payload_length = frame->payload_length - offset;
        if (chunk.payload_length > chunk_size) chunk.payload_length = chunk_size;
        chunk.payload = frame->payload_length == 0U ? NULL : frame->payload + offset;
        uint8_t encoded[20];
        size_t size;
        if (!note4_ble_frame_encode(&chunk, encoded, sizeof(encoded), &size)) return ESP_ERR_INVALID_ARG;
        int rc = BLE_HS_ENOMEM;
        for (unsigned attempt = 0U; attempt < 50U; ++attempt) {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(encoded, size);
            rc = om == NULL ? BLE_HS_ENOMEM : ble_gatts_notify_custom(s_conn_handle, s_status_handle, om);
            if (rc != BLE_HS_ENOMEM && rc != BLE_HS_EBUSY) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (rc != 0) return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t note4_ble_provisioning_ack(uint16_t conn_handle, uint16_t request_id,
                                     note4_ble_status_t status) {
    const uint8_t payload[] = {status};
    const note4_ble_frame_t frame = {
        .version = NOTE4_BLE_PROTOCOL_VERSION,
        .opcode = status == NOTE4_BLE_OK ? NOTE4_BLE_OP_ACK : NOTE4_BLE_OP_NACK,
        .request_id = request_id, .total_sequences = 1U,
        .payload_length = sizeof(payload), .payload = payload,
    };
    if (conn_handle != s_conn_handle) return ESP_ERR_INVALID_STATE;
    return note4_ble_provisioning_notify(&frame);
}
