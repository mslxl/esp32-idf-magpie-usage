#ifndef NOTE4_CONFIG_STORE_H
#define NOTE4_CONFIG_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "settings.h"

/* Dedicated NVS partition label for settings and display preferences. */
#define NOTE4_CONFIG_PARTITION "note4_cfg"

/* Initializes the default NVS partition and NOTE4_CONFIG_PARTITION, then moves
 * the legacy note4_wifi namespace once and deletes it from the default partition. */
esp_err_t note4_config_store_init(void);
/* True for NO_FREE_PAGES/NEW_VERSION_FOUND: storage needs an explicit erase. */
bool note4_config_store_erase_recommended(esp_err_t err);
/* Erases the partition(s) whose last initialization requested it (settings
 * partition by default). Call note4_config_store_init afterwards. */
esp_err_t note4_config_store_erase(void);
/* Returns ESP_ERR_NVS_NOT_ENOUGH_SPACE when settings plus prefs_length bytes
 * of preferences cannot be committed with the partition's free entries. */
esp_err_t note4_config_store_check_capacity(size_t prefs_length);
esp_err_t note4_config_store_load(note4_settings_t *config, bool *found);
esp_err_t note4_config_store_save(const note4_settings_t *config);
esp_err_t note4_config_store_clear(void);
/* Caller owns returned preference bytes. Both settings and preferences switch together. */
esp_err_t note4_config_store_load_bundle(note4_settings_t *config, bool *found, uint8_t **prefs, size_t *length);
esp_err_t note4_config_store_save_bundle(const note4_settings_t *config, const uint8_t *prefs, size_t length);

#endif
