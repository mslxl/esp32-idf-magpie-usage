#ifndef NOTE4_CONFIG_STORE_H
#define NOTE4_CONFIG_STORE_H

#include <stdbool.h>

#include "esp_err.h"

#include "settings.h"

esp_err_t note4_config_store_init(void);
esp_err_t note4_config_store_load(note4_settings_t *config, bool *found);
esp_err_t note4_config_store_save(const note4_settings_t *config);
esp_err_t note4_config_store_clear(void);
/* Caller owns returned preference bytes. Both settings and preferences switch together. */
esp_err_t note4_config_store_load_bundle(note4_settings_t *config, bool *found, uint8_t **prefs, size_t *length);
esp_err_t note4_config_store_save_bundle(const note4_settings_t *config, const uint8_t *prefs, size_t length);

#endif
