#include "battery.h"

#include <stdatomic.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#define NOTE4_POWER_LED_GPIO GPIO_NUM_3
#define NOTE4_CHARGE_GPIO GPIO_NUM_2
#define NOTE4_FULL_GPIO GPIO_NUM_1
#define NOTE4_BATTERY_CHANNEL ADC_CHANNEL_3
#define NOTE4_POWER_TICK_MS 1000U
#define NOTE4_NEAR_FULL_PERCENT 95U

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_calibration;
static bool s_charge_available;
static note4_battery_t s_battery;
static int64_t s_next_sample, s_next_voltage;
static int s_filtered_mv;
static unsigned s_near_full_samples;
static atomic_uint s_power_state;
static atomic_bool s_charge_suppressed;
static TimerHandle_t s_power_timer;
static bool s_led_available;
static bool s_led_on;

static unsigned power_pins(void) {
    return (gpio_get_level(NOTE4_CHARGE_GPIO) == 0 ? 1U : 0U)
        | (gpio_get_level(NOTE4_FULL_GPIO) != 0 ? 2U : 0U);
}

static void power_interrupts(bool enabled) {
    if (enabled) { gpio_intr_enable(NOTE4_CHARGE_GPIO); gpio_intr_enable(NOTE4_FULL_GPIO); }
    else { gpio_intr_disable(NOTE4_CHARGE_GPIO); gpio_intr_disable(NOTE4_FULL_GPIO); }
}

/* The reference firmware holds the active-low LED level across light sleep. */
static void led_write(bool on) {
    if (!s_led_available || on == s_led_on) return;
    gpio_hold_dis(NOTE4_POWER_LED_GPIO);
    (void)gpio_set_level(NOTE4_POWER_LED_GPIO, on ? 0 : 1);
    gpio_hold_en(NOTE4_POWER_LED_GPIO);
    s_led_on = on;
}

static void power_isr(void *context) {
    (void)context;
    BaseType_t woken = pdFALSE;
    power_interrupts(false);
    (void)xTimerStartFromISR(s_power_timer, &woken);
    portYIELD_FROM_ISR(woken);
}

/* The 1 s sampler owns GPIO1/2. The LED timer owns GPIO3 and changes it only
 * every five 1 s ticks while the debounced state is charging. */
static void power_tick(TimerHandle_t timer) {
    (void)timer;
    static unsigned candidate, stable_samples;
    const unsigned pins = power_pins();
    if (pins != candidate) { candidate = pins; stable_samples = 0U; }
    if (stable_samples < 3U) ++stable_samples;
    unsigned state = atomic_load(&s_power_state);
    const bool changed = stable_samples >= 3U && pins != state;
    if (changed) {
        state = pins;
        atomic_store(&s_power_state, state);
        led_write(state == 1U && !atomic_load(&s_charge_suppressed));
        ESP_LOGI("note4_battery", "power present=%u charging=%u full=%u",
            state != 0U, state == 1U, state == 2U);
    }
    if (stable_samples >= 3U && pins == state && state != 1U) {
        (void)xTimerStop(s_power_timer, 0);
        power_interrupts(true);
        /* An edge between the last sample and re-arming would be lost. */
        if (power_pins() != state) { power_interrupts(false); (void)xTimerStart(s_power_timer, 0); }
    }
}

void note4_battery_init(void) {
    /* NOTE4 reference ca285c9: ADC1 channel 3, 2:1 divider, active-low
     * charging on GPIO2 and active-high full on GPIO1. No internal pulls. */
    const gpio_config_t pins = {
        .pin_bit_mask = (1ULL << NOTE4_CHARGE_GPIO) | (1ULL << NOTE4_FULL_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    s_charge_available = gpio_config(&pins) == ESP_OK;
    if (s_charge_available) power_interrupts(false);
    const gpio_config_t led = {
        .pin_bit_mask = 1ULL << NOTE4_POWER_LED_GPIO, .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    s_led_available = gpio_config(&led) == ESP_OK;
    if (!s_led_available) ESP_LOGW("note4_battery", "power LED unavailable");
    if (s_led_available) {
        gpio_hold_dis(NOTE4_POWER_LED_GPIO);
        (void)gpio_set_level(NOTE4_POWER_LED_GPIO, 1);
        gpio_hold_en(NOTE4_POWER_LED_GPIO);
        s_led_on = false;
    }
    if (s_charge_available) {
        s_power_timer = xTimerCreate("note4_power", pdMS_TO_TICKS(NOTE4_POWER_TICK_MS), pdTRUE, NULL, power_tick);
        esp_err_t timer_err = s_power_timer != NULL ? ESP_OK : ESP_ERR_NO_MEM;
        if (timer_err == ESP_OK) {
            timer_err = gpio_install_isr_service(0);
            if (timer_err == ESP_ERR_INVALID_STATE) timer_err = ESP_OK;
        }
        if (timer_err == ESP_OK) timer_err = gpio_isr_handler_add(NOTE4_CHARGE_GPIO, power_isr, NULL);
        if (timer_err == ESP_OK) timer_err = gpio_isr_handler_add(NOTE4_FULL_GPIO, power_isr, NULL);
        /* The first samples settle the initial state, then the timer stops itself. */
        if (timer_err == ESP_OK && xTimerStart(s_power_timer, 0) != pdPASS) timer_err = ESP_FAIL;
        if (timer_err != ESP_OK) ESP_LOGW("note4_battery", "power status timer unavailable: %s", esp_err_to_name(timer_err));
    }
    const adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    esp_err_t err = adc_oneshot_new_unit(&unit, &s_adc);
    if (err == ESP_OK) {
        const adc_oneshot_chan_cfg_t channel = {
            .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12,
        };
        err = adc_oneshot_config_channel(s_adc, NOTE4_BATTERY_CHANNEL, &channel);
    }
    if (err == ESP_OK) {
        const adc_cali_curve_fitting_config_t calibration = {
            .unit_id = ADC_UNIT_1, .chan = NOTE4_BATTERY_CHANNEL,
            .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12,
        };
        err = adc_cali_create_scheme_curve_fitting(&calibration, &s_calibration);
    }
    if (err != ESP_OK) {
        if (s_adc != NULL) { adc_oneshot_del_unit(s_adc); s_adc = NULL; }
        ESP_LOGW("note4_battery", "calibrated battery measurement unavailable: %s", esp_err_to_name(err));
    }
}

note4_battery_t note4_battery_poll(int64_t now_us) {
    /* Edge interrupts can be missed while the GPIO matrix sleeps; resample on poll. */
    if (s_power_timer != NULL && xTimerIsTimerActive(s_power_timer) == pdFALSE &&
        power_pins() != atomic_load(&s_power_state)) {
        power_interrupts(false);
        (void)xTimerStart(s_power_timer, 0);
    }
    const unsigned state = atomic_load(&s_power_state);
    const bool pin_charging = state == 1U;
    const bool full = state == 2U;
    if (!pin_charging) {
        s_near_full_samples = 0U;
        atomic_store(&s_charge_suppressed, false);
    }
    bool charging = pin_charging && !atomic_load(&s_charge_suppressed);
    const bool power_changed = s_battery.charging != charging || s_battery.full != full;
    if (now_us < s_next_sample && !power_changed) return s_battery;
    s_next_sample = now_us + 5000000;
    s_battery.charging = charging;
    s_battery.full = full;
    if (now_us < s_next_voltage && !power_changed) return s_battery;
    s_next_voltage = now_us + 30000000;
    int total_mv = 0;
    bool valid = s_adc != NULL && s_calibration != NULL;
    for (unsigned i = 0U; valid && i < 10U; ++i) {
        int raw, pin_mv;
        valid = adc_oneshot_read(s_adc, NOTE4_BATTERY_CHANNEL, &raw) == ESP_OK
            && adc_cali_raw_to_voltage(s_calibration, raw, &pin_mv) == ESP_OK;
        if (valid) total_mv += pin_mv * 2;
    }
    const int mv = total_mv / 10;
    valid = valid && mv >= 2500 && mv <= 4500;
    if (!valid) {
        s_battery.valid = false;
        s_filtered_mv = 0;
        ESP_LOGI("note4_battery", "sample valid=0 millivolts=0 percent=0 charging=%u full=%u",
            (unsigned)charging, (unsigned)full);
        return s_battery;
    }
    s_filtered_mv = s_filtered_mv == 0 ? mv : (s_filtered_mv * 3 + mv) / 4;
    /* Same voltage estimate as the NOTE4 reference; this is not a fuel gauge. */
    int percent = (-s_filtered_mv * s_filtered_mv + 9016 * s_filtered_mv - 19189000) / 10000;
    if (percent < 0) percent = 0;
    if (percent > 100 || full) percent = 100;
    if (pin_charging && (full || percent >= NOTE4_NEAR_FULL_PERCENT)) {
        /* A full signal or a near-full voltage must never enter blink mode. */
        atomic_store(&s_charge_suppressed, true);
        charging = false;
    } else if (pin_charging) {
        atomic_store(&s_charge_suppressed, false);
        charging = true;
    }
    s_battery.charging = charging;
    const int previous = (int)s_battery.percent;
    if (!s_battery.valid || full || percent == 0 || percent == 100 ||
        percent >= previous + 2 || percent <= previous - 2) s_battery.percent = (uint32_t)percent;
    s_battery.valid = true;
    ESP_LOGI("note4_battery", "sample valid=1 millivolts=%d percent=%u charging=%u full=%u",
        s_filtered_mv, (unsigned)s_battery.percent, (unsigned)charging, (unsigned)full);
    return s_battery;
}
