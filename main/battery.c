#include "battery.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#define NOTE4_CHARGE_GPIO GPIO_NUM_2
#define NOTE4_FULL_GPIO GPIO_NUM_1
#define NOTE4_BATTERY_CHANNEL ADC_CHANNEL_3

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_calibration;
static bool s_charge_available;
static note4_battery_t s_battery;
static int64_t s_next_sample, s_next_voltage;
static int s_filtered_mv;
static unsigned s_pin_state, s_stable_samples;

void note4_battery_init(void) {
    /* NOTE4 reference ca285c9: ADC1 channel 3, 2:1 divider, active-low
     * charging on GPIO2 and active-high full on GPIO1. No internal pulls. */
    const gpio_config_t pins = {
        .pin_bit_mask = (1ULL << NOTE4_CHARGE_GPIO) | (1ULL << NOTE4_FULL_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    s_charge_available = gpio_config(&pins) == ESP_OK;
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
    if (now_us < s_next_sample) return s_battery;
    s_next_sample = now_us + 5000000;
    bool charging = false, full = false;
    if (s_charge_available) {
        const unsigned state = (gpio_get_level(NOTE4_CHARGE_GPIO) == 0 ? 1U : 0U)
            | (gpio_get_level(NOTE4_FULL_GPIO) != 0 ? 2U : 0U);
        if (state != s_pin_state) { s_pin_state = state; s_stable_samples = 0U; }
        if (s_stable_samples < 3U) ++s_stable_samples;
        if (s_stable_samples >= 3U) { charging = state == 1U; full = state == 2U; }
    }
    const bool power_changed = s_battery.charging != charging || s_battery.full != full;
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
