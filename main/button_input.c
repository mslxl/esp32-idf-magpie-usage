#include "button_input.h"

#include "driver/gpio.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define NOTE4_BUTTON_UP_GPIO GPIO_NUM_39
#define NOTE4_BUTTON_OK_GPIO GPIO_NUM_0
#define NOTE4_BUTTON_DOWN_GPIO GPIO_NUM_18
#define NOTE4_BUTTON_LONG_PRESS_MS 1200U
#define NOTE4_BUTTON_DEBOUNCE_MS 40U

static note4_button_event_handler_t s_handler;
static void *s_context;

static void button_task(void *arg) {
    (void)arg;
    const gpio_num_t pins[] = {NOTE4_BUTTON_UP_GPIO, NOTE4_BUTTON_OK_GPIO, NOTE4_BUTTON_DOWN_GPIO};
    bool raw[3] = {0};
    bool pressed[3] = {0};
    bool long_sent[3] = {0};
    TickType_t changed[3] = {0};
    TickType_t started[3] = {0};
    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        for (size_t index = 0U; index < 3U; ++index) {
            const bool down = gpio_get_level(pins[index]) == 0;
            if (down != raw[index]) {
                raw[index] = down;
                changed[index] = now;
            }
            if (down != pressed[index] && now - changed[index] >= pdMS_TO_TICKS(NOTE4_BUTTON_DEBOUNCE_MS)) {
                pressed[index] = down;
                if (down) {
                    started[index] = now;
                    long_sent[index] = false;
                    if (index != NOTE4_BUTTON_DOWN) {
                        const note4_button_event_t event = {.button = index, .type = NOTE4_BUTTON_EVENT_PRESS};
                        s_handler(&event, s_context);
                    }
                } else if (index == NOTE4_BUTTON_DOWN && !long_sent[index]) {
                    const note4_button_event_t event = {.button = index, .type = NOTE4_BUTTON_EVENT_PRESS};
                    s_handler(&event, s_context);
                }
            }
            if (index == NOTE4_BUTTON_DOWN && pressed[index] && !long_sent[index] &&
                now - started[index] >= pdMS_TO_TICKS(NOTE4_BUTTON_LONG_PRESS_MS)) {
                long_sent[index] = true;
                const note4_button_event_t event = {.button = index, .type = NOTE4_BUTTON_EVENT_LONG_PRESS};
                s_handler(&event, s_context);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t note4_button_input_init(note4_button_event_handler_t handler, void *context) {
    s_handler = handler;
    s_context = context;
    if (handler == NULL) return ESP_ERR_INVALID_ARG;
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << NOTE4_BUTTON_UP_GPIO) |
                        (1ULL << NOTE4_BUTTON_OK_GPIO) |
                        (1ULL << NOTE4_BUTTON_DOWN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) return err;
    return xTaskCreate(button_task, "note4_buttons", 3072, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}
