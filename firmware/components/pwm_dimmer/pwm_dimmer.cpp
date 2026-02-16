/**
 * @file pwm_dimmer.cpp
 * @brief MOSFET PWM dimmer implementation (ESP-IDF).
 *
 * @details
 * Implements hardware PWM using ESP32 LEDC peripheral for LED dimming.
 */

#include "pwm_dimmer.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>


static const char* TAG = "PWM_DIMMER";


/*
 * =============================================================================
 * GAMMA CORRECTION TABLE
 * =============================================================================
 *
 * Pre-calculated gamma correction (gamma = 2.2) for 0-100% input.
 * Output is 0-1000 (scaled for easy math with 10-bit resolution).
 *
 * Formula: output = (input/100)^2.2 * 1000
 */
static const uint16_t gammaTable[101] = {
       0,    0,    0,    0,    0,    1,    1,    2,    2,    3,   //  0-9
       4,    5,    6,    8,    9,   11,   13,   15,   18,   20,   // 10-19
      23,   26,   30,   33,   37,   41,   46,   50,   55,   60,   // 20-29
      66,   71,   77,   84,   90,   97,  104,  111,  119,  127,   // 30-39
     135,  144,  153,  162,  171,  181,  191,  202,  212,  224,   // 40-49
     235,  247,  259,  271,  284,  297,  311,  325,  339,  354,   // 50-59
     369,  384,  400,  416,  433,  450,  467,  485,  503,  521,   // 60-69
     540,  560,  579,  600,  620,  641,  663,  684,  707,  729,   // 70-79
     752,  776,  800,  824,  849,  874,  900,  926,  952,  979,   // 80-89
    1006, 1034, 1062, 1091, 1120, 1150, 1180, 1210, 1241, 1272,   // 90-99
    1304                                                          // 100
};


/*
 * =============================================================================
 * CONSTRUCTOR
 * =============================================================================
 */
PWMDimmer::PWMDimmer(gpio_num_t pin, ledc_channel_t channel, ledc_timer_t timer)
    : pin(pin),
      channel(channel),
      timer(timer),
      resolution(PWM_DIMMER_DEFAULT_RES),
      maxDuty(0),
      currentBrightness(0),
      initialized(false)
{
}


/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 */
PWMDimmer::~PWMDimmer() {
    if (initialized) {
        off();
        ledc_stop(LEDC_LOW_SPEED_MODE, channel, 0);
    }
}


/*
 * =============================================================================
 * INITIALIZATION
 * =============================================================================
 */
bool PWMDimmer::init(uint32_t frequency, ledc_timer_bit_t res) {
    ESP_LOGI(TAG, "Initializing PWM dimmer (GPIO=%d, Freq=%luHz, Res=%d-bit)",
             pin, frequency, res);

    resolution = res;
    maxDuty = (1 << res) - 1;  // e.g., 10-bit = 1023

    /*
     * -------------------------------------------------------------------------
     * STEP 1: Configure LEDC timer
     * -------------------------------------------------------------------------
     * The timer generates the PWM base frequency. Multiple channels can
     * share the same timer.
     */
    ledc_timer_config_t timerConfig = {};
    timerConfig.speed_mode = LEDC_LOW_SPEED_MODE;
    timerConfig.duty_resolution = resolution;
    timerConfig.timer_num = timer;
    timerConfig.freq_hz = frequency;
    timerConfig.clk_cfg = LEDC_AUTO_CLK;

    esp_err_t err = ledc_timer_config(&timerConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Timer config failed: %s", esp_err_to_name(err));
        return false;
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 2: Configure LEDC channel
     * -------------------------------------------------------------------------
     * The channel connects a GPIO pin to a timer and controls the duty cycle.
     */
    ledc_channel_config_t channelConfig = {};
    channelConfig.speed_mode = LEDC_LOW_SPEED_MODE;
    channelConfig.channel = channel;
    channelConfig.timer_sel = timer;
    channelConfig.gpio_num = pin;
    channelConfig.duty = 0;  // Start off
    channelConfig.hpoint = 0;

    err = ledc_channel_config(&channelConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Channel config failed: %s", esp_err_to_name(err));
        return false;
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 3: Enable fade function (for smooth transitions)
     * -------------------------------------------------------------------------
     */
    ledc_fade_func_install(0);

    initialized = true;
    ESP_LOGI(TAG, "PWM dimmer initialized (maxDuty=%lu)", maxDuty);
    return true;
}


/*
 * =============================================================================
 * SET BRIGHTNESS
 * =============================================================================
 */
void PWMDimmer::setBrightness(uint8_t percent, bool useGamma) {
    if (!initialized) return;

    // Clamp to 0-100
    if (percent > 100) percent = 100;

    uint32_t duty;
    if (useGamma) {
        duty = applyGamma(percent);
    } else {
        // Linear mapping
        duty = (uint32_t)percent * maxDuty / 100;
    }

    setDuty(duty);
    currentBrightness = percent;

    ESP_LOGD(TAG, "Brightness set to %d%% (duty=%lu)", percent, duty);
}


/*
 * =============================================================================
 * SET RAW DUTY
 * =============================================================================
 */
void PWMDimmer::setDuty(uint32_t duty) {
    if (!initialized) return;

    if (duty > maxDuty) duty = maxDuty;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}


/*
 * =============================================================================
 * GET DUTY
 * =============================================================================
 */
uint32_t PWMDimmer::getDuty() const {
    if (!initialized) return 0;
    return ledc_get_duty(LEDC_LOW_SPEED_MODE, channel);
}


/*
 * =============================================================================
 * ON / OFF / TOGGLE
 * =============================================================================
 */
void PWMDimmer::on() {
    setBrightness(100);
}


void PWMDimmer::off() {
    setBrightness(0);
}


void PWMDimmer::toggle() {
    if (isOn()) {
        off();
    } else {
        on();
    }
}


/*
 * =============================================================================
 * FADE FUNCTIONS
 * =============================================================================
 */
void PWMDimmer::fadeTo(uint8_t targetPercent, uint32_t durationMs, bool useGamma) {
    if (!initialized) return;

    if (targetPercent > 100) targetPercent = 100;

    uint32_t targetDuty;
    if (useGamma) {
        targetDuty = applyGamma(targetPercent);
    } else {
        targetDuty = (uint32_t)targetPercent * maxDuty / 100;
    }

    ESP_LOGI(TAG, "Fading to %d%% over %lums", targetPercent, durationMs);

    ledc_set_fade_time_and_start(
        LEDC_LOW_SPEED_MODE,
        channel,
        targetDuty,
        durationMs,
        LEDC_FADE_NO_WAIT
    );

    // Update current brightness after fade starts
    // (Note: actual brightness changes gradually)
    currentBrightness = targetPercent;
}


void PWMDimmer::fadeIn(uint32_t durationMs) {
    setBrightness(0, false);  // Start from 0
    fadeTo(100, durationMs);
}


void PWMDimmer::fadeOut(uint32_t durationMs) {
    fadeTo(0, durationMs);
}


/*
 * =============================================================================
 * SET FREQUENCY
 * =============================================================================
 */
bool PWMDimmer::setFrequency(uint32_t frequency) {
    if (!initialized) return false;

    esp_err_t err = ledc_set_freq(LEDC_LOW_SPEED_MODE, timer, frequency);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set frequency: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Frequency set to %luHz", frequency);
    return true;
}


/*
 * =============================================================================
 * GAMMA CORRECTION
 * =============================================================================
 */
uint32_t PWMDimmer::applyGamma(uint8_t percent) {
    if (percent > 100) percent = 100;

    // Look up gamma-corrected value (0-1304 range)
    uint32_t gammaValue = gammaTable[percent];

    // Scale to actual maxDuty
    // gammaTable max is ~1304, so scale: duty = gammaValue * maxDuty / 1304
    uint32_t duty = (gammaValue * maxDuty) / 1304;

    return duty;
}
