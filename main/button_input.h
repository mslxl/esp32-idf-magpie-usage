#ifndef NOTE4_BUTTON_INPUT_H
#define NOTE4_BUTTON_INPUT_H

#include <stdint.h>

#include "esp_err.h"

typedef enum {
    NOTE4_BUTTON_UP = 0,
    NOTE4_BUTTON_OK,
    NOTE4_BUTTON_DOWN,
} note4_button_id_t;

typedef enum {
    NOTE4_BUTTON_EVENT_PRESS = 0,
    NOTE4_BUTTON_EVENT_LONG_PRESS,
} note4_button_event_type_t;

typedef struct {
    note4_button_id_t button;
    note4_button_event_type_t type;
} note4_button_event_t;

typedef void (*note4_button_event_handler_t)(const note4_button_event_t *event, void *context);

esp_err_t note4_button_input_init(note4_button_event_handler_t handler, void *context);

#endif
