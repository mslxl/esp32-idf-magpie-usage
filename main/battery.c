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

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_calibration;
static bool s_charge_available;
static note4_battery_t s_battery;
static int64_t s_next_sample, s_next_voltage;
static int s_filtered_mv;
static atomic_uint s_power_state;
static TimerHandle_t s_power_timer;
static bool s_led_available;

static unsigned power_pins(void) {
    return (gpio_get_level(NOTE4_CHARGE_GPIO) == 0 ? 1U : 0U)
        | (gpio_get_level(NOTE4_FULL_GPIO) != 0 ? 2U : 0U);
}

static void power_interrupts(bool enabled) {
    if (enabled) { gpio_intr_enable(NOTE4_CHARGE_GPIO); gpio_intr_enable(NOTE4_FULL_GPIO); }
    else { gpio_intr_disable(NOTE4_CHARGE_GPIO); gpio_intr_disable(NOTE4_FULL_GPIO); }
}

/* An edge on either status pin masks both interrupts and starts the sampler. */
static void power_isr(void *context) {
    (void)context;
    BaseType_t woken = pdFALSE;
    power_interrupts(false);
    (void)xTimerStartFromISR(s_power_timer, &woken);
    portYIELD_FROM_ISR(woken);
}

/* The 100 ms timer owns GPIO sampling and LED writes, independently of EPD/network
 * work. It runs only while a pin change is being debounced or the LED blinks
 * (charging); otherwise it stops and the pin interrupts are re-armed. */
static void power_tick(TimerHandle_t timer) {
    (void)timer;
    static unsigned candidate, stable_samples, blink_ticks;
    static bool led_on;
    const unsigned pins = power_pins();
    if (pins != candidate) { candidate = pins; stable_samples = 0U; }
    if (stable_samples < 3U) ++stable_samples;
    unsigned state = atomic_load(&s_power_state);
    const bool changed = stable_samples >= 3U && pins != state;
    if (changed) {
        state = pins;
        atomic_store(&s_power_state, state);
        blink_ticks = 0U;
        ESP_LOGI("note4_battery", "power present=%u charging=%u full=%u led_mode=%u",
            state != 0U, state == 1U, state == 2U, state == 1U ? 2U : state != 0U ? 1U : 0U);
    }
    bool on = state != 0U;
    if (state == 1U) {
        if (!changed && ++blink_ticks >= 10U) blink_ticks = 0U;
        on = blink_ticks < 5U;
    }
    if (s_led_available && on != led_on) {
        gpio_set_level(NOTE4_POWER_LED_GPIO, on ? 0 : 1);
        led_on = on;
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
    gpio_hold_dis(NOTE4_POWER_LED_GPIO);
    gpio_set_level(NOTE4_POWER_LED_GPIO, 1);
    const gpio_config_t led = {
        .pin_bit_mask = 1ULL << NOTE4_POWER_LED_GPIO, .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    s_led_available = gpio_config(&led) == ESP_OK;
    if (!s_led_available) ESP_LOGW("note4_battery", "power LED unavailable");
    if (s_charge_available) {
        s_power_timer = xTimerCreate("note4_power", pdMS_TO_TICKS(100), pdTRUE, NULL, power_tick);
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
    const bool charging = state == 1U, full = state == 2U;
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
    const int previous = (int)s_battery.percent;
    if (!s_battery.valid || full || percent == 0 || percent == 100 ||
        percent >= previous + 2 || percent <= previous - 2) s_battery.percent = (uint32_t)percent;
    s_battery.valid = true;
    ESP_LOGI("note4_battery", "sample valid=1 millivolts=%d percent=%u charging=%u full=%u",
        s_filtered_mv, (unsigned)s_battery.percent, (unsigned)charging, (unsigned)full);
    return s_battery;
}
