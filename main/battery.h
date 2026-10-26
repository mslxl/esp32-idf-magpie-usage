#ifndef NOTE4_BATTERY_H
#define NOTE4_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t percent;
    bool valid;
    bool charging;
    bool full;
} note4_battery_t;

/* Owned and polled by the screen task; initialization failure is nonfatal. */
void note4_battery_init(void);
note4_battery_t note4_battery_poll(int64_t now_us);

#endif
