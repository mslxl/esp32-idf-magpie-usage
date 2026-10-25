#ifndef NOTE4_WIFI_MANAGER_H
#define NOTE4_WIFI_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "wifi_config.h"

typedef enum {
    NOTE4_WIFI_EVENT_STARTED = 0,
    NOTE4_WIFI_EVENT_CONNECTED,
    NOTE4_WIFI_EVENT_GOT_IP,
    NOTE4_WIFI_EVENT_DISCONNECTED,
    NOTE4_WIFI_EVENT_SCAN_DONE,
} note4_wifi_event_type_t;

typedef struct {
    note4_wifi_event_type_t type;
    uint8_t slot;
    int reason;
    uint32_t attempt;
} note4_wifi_event_t;

typedef void (*note4_wifi_event_handler_t)(const note4_wifi_event_t *event, void *context);

typedef struct {
    char ssid[NOTE4_WIFI_SSID_MAX + 1U];
    int8_t rssi;
    uint8_t channel;
    bool secure;
} note4_wifi_scan_result_t;

esp_err_t note4_wifi_manager_init(note4_wifi_event_handler_t handler, void *context);
esp_err_t note4_wifi_manager_set_config(const note4_wifi_config_t *config);
esp_err_t note4_wifi_manager_connect(size_t slot);
uint32_t note4_wifi_manager_attempt(void);
esp_err_t note4_wifi_manager_disconnect(void);
esp_err_t note4_wifi_manager_scan(void);
esp_err_t note4_wifi_manager_get_scan_results(note4_wifi_scan_result_t *results,
                                               size_t capacity, size_t *count);

#endif
