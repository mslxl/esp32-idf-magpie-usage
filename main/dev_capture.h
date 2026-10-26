#ifndef NOTE4_DEV_CAPTURE_H
#define NOTE4_DEV_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t note4_capture_init(void);
/* Called by the screen task, after presentation; never exports pairing/setup pages. */
void note4_capture_poll(const uint8_t *frame, uint32_t page, uint32_t pages, bool allowed);

#endif
