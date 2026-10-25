#ifndef NOTE4_CONFIG_STORE_H
#define NOTE4_CONFIG_STORE_H

#include <stdbool.h>

#include "esp_err.h"

#include "wifi_config.h"

esp_err_t note4_config_store_init(void);
esp_err_t note4_config_store_load(note4_wifi_config_t *config, bool *found);
esp_err_t note4_config_store_save(const note4_wifi_config_t *config);
esp_err_t note4_config_store_clear(void);

#endif
