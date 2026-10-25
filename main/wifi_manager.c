#include "wifi_manager.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static note4_wifi_event_handler_t s_handler;
static void *s_context;
static note4_wifi_config_t s_config;
static wifi_scan_config_t s_scan_config;
static bool s_initialized;
static size_t s_connecting_slot;
static bool s_scan_in_progress;
static atomic_bool s_disconnecting;
static atomic_bool s_attempt_active;
static atomic_uint s_attempt;
static EventGroupHandle_t s_disconnect_events;

static void emit(note4_wifi_event_type_t type, uint8_t slot, int reason) {
    if (s_handler == NULL) return;
    const note4_wifi_event_t event = {.type = type, .slot = slot, .reason = reason,
                                      .attempt = atomic_load(&s_attempt)};
    s_handler(&event, s_context);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base != WIFI_EVENT) return;
    if (id == WIFI_EVENT_STA_START) {
        emit(NOTE4_WIFI_EVENT_STARTED, 0U, 0);
    } else if (id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *connected = data;
        emit(NOTE4_WIFI_EVENT_CONNECTED, (uint8_t)s_connecting_slot, connected->channel);
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = data;
        atomic_store(&s_attempt_active, false);
        if (atomic_load(&s_disconnecting)) {
            xEventGroupSetBits(s_disconnect_events, 1U);
        } else {
            emit(NOTE4_WIFI_EVENT_DISCONNECTED, (uint8_t)s_connecting_slot, disconnected->reason);
        }
    } else if (id == WIFI_EVENT_SCAN_DONE) {
        s_scan_in_progress = false;
        emit(NOTE4_WIFI_EVENT_SCAN_DONE, 0U, 0);
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        emit(NOTE4_WIFI_EVENT_GOT_IP, (uint8_t)s_connecting_slot, 0);
    }
}

esp_err_t note4_wifi_manager_init(note4_wifi_event_handler_t handler, void *context) {
    s_handler = handler;
    s_context = context;
    s_disconnect_events = xEventGroupCreate();
    if (s_disconnect_events == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    if (esp_netif_create_default_wifi_sta() == NULL) return ESP_ERR_NO_MEM;
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err == ESP_OK || err == ESP_ERR_WIFI_STATE) s_initialized = true;
    return err == ESP_ERR_WIFI_STATE ? ESP_OK : err;
}

esp_err_t note4_wifi_manager_set_config(const note4_wifi_config_t *config) {
    if (!s_initialized || config == NULL || !note4_wifi_config_validate(config)) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    return ESP_OK;
}

esp_err_t note4_wifi_manager_connect(size_t slot) {
    if (!s_initialized || slot >= NOTE4_WIFI_MAX_SLOTS || !s_config.slots[slot].enabled) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = note4_wifi_manager_disconnect();
    if (err != ESP_OK) return err;
    wifi_config_t wifi_config;
    s_connecting_slot = slot;
    memset(&wifi_config, 0, sizeof(wifi_config));
    memcpy(wifi_config.sta.ssid, s_config.slots[slot].ssid, s_config.slots[slot].ssid_len);
    memcpy(wifi_config.sta.password, s_config.slots[slot].password, s_config.slots[slot].password_len);
    wifi_config.sta.threshold.authmode = s_config.slots[slot].password_len == 0U
        ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) return err;
    atomic_fetch_add(&s_attempt, 1U);
    atomic_store(&s_attempt_active, true);
    err = esp_wifi_connect();
    if (err != ESP_OK) atomic_store(&s_attempt_active, false);
    return err;
}

uint32_t note4_wifi_manager_attempt(void) {
    return atomic_load(&s_attempt);
}

esp_err_t note4_wifi_manager_disconnect(void) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_scan_in_progress) {
        (void)esp_wifi_scan_stop();
        s_scan_in_progress = false;
    }
    if (!atomic_load(&s_attempt_active)) return ESP_OK;
    xEventGroupClearBits(s_disconnect_events, 1U);
    atomic_store(&s_disconnecting, true);
    esp_err_t err = esp_wifi_disconnect();
    if (err == ESP_OK && !(xEventGroupWaitBits(s_disconnect_events, 1U, pdTRUE,
                                              pdFALSE, pdMS_TO_TICKS(2000)) & 1U)) {
        err = ESP_ERR_TIMEOUT;
    }
    if (err == ESP_ERR_WIFI_NOT_CONNECT) err = ESP_OK;
    atomic_store(&s_disconnecting, false);
    if (err == ESP_OK) atomic_store(&s_attempt_active, false);
    return err;
}

esp_err_t note4_wifi_manager_scan(void) {
    if (!s_initialized || s_scan_in_progress) return ESP_ERR_INVALID_STATE;
    memset(&s_scan_config, 0, sizeof(s_scan_config));
    s_scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    esp_err_t err = esp_wifi_scan_start(&s_scan_config, false);
    if (err == ESP_OK) s_scan_in_progress = true;
    return err;
}

esp_err_t note4_wifi_manager_get_scan_results(note4_wifi_scan_result_t *results,
                                               size_t capacity, size_t *count) {
    if (!s_initialized || results == NULL || count == NULL) return ESP_ERR_INVALID_ARG;
    uint16_t found = 0U;
    esp_err_t err = esp_wifi_scan_get_ap_num(&found);
    if (err != ESP_OK) return err;
    const size_t total = found < capacity ? found : capacity;
    wifi_ap_record_t *records = calloc(total == 0U ? 1U : total, sizeof(*records));
    if (records == NULL) return ESP_ERR_NO_MEM;
    uint16_t returned = (uint16_t)total;
    err = esp_wifi_scan_get_ap_records(&returned, records);
    if (err == ESP_OK) {
        for (size_t index = 0; index < returned; ++index) {
            memcpy(results[index].ssid, records[index].ssid, sizeof(records[index].ssid));
            results[index].ssid[NOTE4_WIFI_SSID_MAX] = '\0';
            results[index].rssi = records[index].rssi;
            results[index].channel = records[index].primary;
            results[index].secure = records[index].authmode != WIFI_AUTH_OPEN;
        }
        *count = returned;
    }
    free(records);
    return err;
}
