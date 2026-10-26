#include "dev_capture.h"

#ifndef NOTE4_DEV_CAPTURE
#error "USB screenshot transport is only permitted in development firmware"
#endif

#include <stdio.h>
#include <string.h>
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t note4_capture_init(void) {
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    return usb_serial_jtag_driver_install(&config);
}

void note4_capture_poll(const uint8_t *frame, uint32_t page, uint32_t pages, bool allowed) {
    static char command[16];
    static size_t length;
    static bool overflow;
    uint8_t input[32];
    const int count = usb_serial_jtag_read_bytes(input, sizeof(input), 0);
    bool requested = false;
    for (int i = 0; i < count; ++i) {
        if (input[i] == '\r') continue;
        if (input[i] != '\n') {
            if (length + 1U < sizeof(command)) command[length++] = (char)input[i];
            else overflow = true;
            continue;
        }
        command[length] = 0;
        requested |= !overflow && strcmp(command, "SHOT") == 0;
        length = 0U;
        overflow = false;
    }
    if (!requested) return;
    if (!allowed || frame == NULL || pages == 0U) {
        puts("N4SHOT WAIT");
        fflush(stdout);
        return;
    }
    uint32_t hash = 2166136261U;
    for (size_t i = 0U; i < 15000U; ++i) hash = (hash ^ frame[i]) * 16777619U;
    printf("N4SHOT BEGIN 400 300 15000 %u %u %08lx\n",
        (unsigned)page, (unsigned)pages, (unsigned long)hash);
    for (unsigned y = 0U; y < 300U; ++y) {
        char row[128];
        int pos = snprintf(row, sizeof(row), "N4SHOT ROW %03u ", y);
        for (unsigned x = 0U; x < 50U; ++x)
            pos += snprintf(row + pos, sizeof(row) - (size_t)pos, "%02x", frame[y * 50U + x]);
        row[pos++] = '\n';
        row[pos] = 0;
        fputs(row, stdout);
        fflush(stdout);
        vTaskDelay(1);
    }
    printf("N4SHOT END %08lx\n", (unsigned long)hash);
    fflush(stdout);
}
