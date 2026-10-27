#include "esp_err.h"
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
esp_err_t nvs_flash_init_partition(const char *label);
esp_err_t nvs_flash_erase_partition(const char *label);
