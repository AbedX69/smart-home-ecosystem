/**
 * @file mosfet_driver.cpp
 * @brief High-power MOSFET driver implementation (ESP-IDF).
 *
 * @details
 * Implements hardware PWM using ESP32 LEDC peripheral for controlling
 * high-power MOSFET modules. Includes soft-start for inrush protection.
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: HOW THIS IMPLEMENTATION WORKS
 * =============================================================================
 *
 * This driver is simpler than pwm_dimmer because:
 *     - No gamma correction (linear control)
 *     - Focused on power control, not perception
 *     - Adds soft-start for inrush protection
 *
 * LEDC PERIPHERAL:
 *     ESP32 has hardware PWM called "LEDC" (LED Controller).
 *     Despite the name, it works for any PWM application.
 *
 *     Structure:
 *         Timer (sets frequency) → Channel (sets duty) → GPIO pin
 *
 *     We use Timer 2 and Channel 2 by default to avoid conflicts
 *     with buzzer (0) and vibration (1) drivers.
 *
 * SOFT START IMPLEMENTATION:
 *     Software-based ramp using small steps and delays.
 *     The hardware fade function could also work, but software
 *     gives us more control over the ramp curve.
 *
 *     Ramp from 0% to 100% over 500ms:
 *         - 50 steps (1% each would be 100, but we use larger steps)
 *         - 10ms per step
 *         - Linear increase
 *
 * =============================================================================
 */

#include "mosfet_driver.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


static const char* TAG = "MOSFET";


/*
 * =============================================================================
 * GAMMA CORRECTION TABLE
 * =============================================================================
 *
 * Pre-calculated gamma correction (gamma = 2.2) for 0-100% input.
 * Output is 0-1000 (scaled for easy math with 10-bit resolution).
 *
 * Human eyes perceive brightness logarithmically, not linearly.
 * Without gamma: 50% duty looks like ~75% brightness.
 * With gamma: 50% perceived brightness = ~22% actual duty.
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
MosfetDriver::MosfetDriver(gpio_num_t pin, ledc_channel_t channel, ledc_timer_t timer)
    : pin(pin),
      channel(channel),
      timer(timer),
      resolution(MOSFET_DEFAULT_RES),
      maxDuty(0),
      currentLevel(0),
      initialized(false)
{
}


/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 */
MosfetDriver::~MosfetDriver() {
    if (initialized) {
        off();
        ledc_stop(LEDC_LOW_SPEED_MODE, channel, 0);
    }
}


/*
 * =============================================================================
 * INITIALIZATION
 * =============================================================================
 *
 * Sets up LEDC timer and channel for PWM output.
 *
 * Timer: Generates the PWM frequency (5kHz default)
 * Channel: Controls duty cycle on a specific GPIO pin
 *
 * LOW_SPEED_MODE is used because it works on ALL ESP32 variants.
 * HIGH_SPEED_MODE only exists on original ESP32.
 */
bool MosfetDriver::init(uint32_t frequency, ledc_timer_bit_t res) {
    ESP_LOGI(TAG, "Initializing MOSFET driver (GPIO=%d, Freq=%luHz, Res=%d-bit)",
             pin, frequency, res);

    resolution = res;
    maxDuty = (1 << res) - 1;  // e.g., 10-bit = 1023

    /*
     * Configure LEDC timer
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
     * Configure LEDC channel
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
     * Install fade function for smooth transitions
     */
    ledc_fade_func_install(0);

    initialized = true;
    ESP_LOGI(TAG, "MOSFET driver initialized (maxDuty=%lu)", maxDuty);
    return true;
}


/*
 * =============================================================================
 * SET LEVEL
 * =============================================================================
 *
 * Maps 0-100% to duty cycle.
 * With useGamma=false: Linear mapping (good for motors, heaters)
 * With useGamma=true: Gamma-corrected (good for LEDs)
 */
void MosfetDriver::setLevel(uint8_t percent, bool useGamma) {
    if (!initialized) return;

    if (percent > 100) percent = 100;

    uint32_t duty;
    if (useGamma) {
        duty = applyGamma(percent);
    } else {
        // Linear mapping
        duty = (uint32_t)percent * maxDuty / 100;
    }

    setDuty(duty);
    currentLevel = percent;

    ESP_LOGD(TAG, "Level set to %d%% (duty=%lu, gamma=%s)", 
             percent, duty, useGamma ? "on" : "off");
}


/*
 * =============================================================================
 * SET RAW DUTY
 * =============================================================================
 */
void MosfetDriver::setDuty(uint32_t duty) {
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
uint32_t MosfetDriver::getDuty() const {
    if (!initialized) return 0;
    return ledc_get_duty(LEDC_LOW_SPEED_MODE, channel);
}


/*
 * =============================================================================
 * ON / OFF / TOGGLE
 * =============================================================================
 */
void MosfetDriver::on() {
    setLevel(100);
}


void MosfetDriver::off() {
    setLevel(0);
}


void MosfetDriver::toggle() {
    if (isOn()) {
        off();
    } else {
        on();
    }
}


/*
 * =============================================================================
 * SOFT START
 * =============================================================================
 *
 * Gradually ramps from 0 to target level to avoid inrush current.
 *
 * This is a BLOCKING function - it doesn't return until the ramp
 * is complete. Use it at startup, not during normal operation.
 *
 * Implementation:
 *     - Divide duration into steps (~10ms each)
 *     - Increase level at each step
 *     - Minimum 20 steps for smooth ramp
 */
void MosfetDriver::softStart(uint8_t targetPercent, uint32_t durationMs, bool useGamma) {
    if (!initialized) return;

    if (targetPercent > 100) targetPercent = 100;

    ESP_LOGI(TAG, "Soft start: 0%% -> %d%% over %lums (gamma=%s)", 
             targetPercent, durationMs, useGamma ? "on" : "off");

    // Make sure we start from 0
    setLevel(0, useGamma);

    // Calculate number of steps (aim for ~10ms per step, minimum 20 steps)
    uint32_t numSteps = durationMs / 10;
    if (numSteps < 20) numSteps = 20;
    if (numSteps > 100) numSteps = 100;  // Cap at 100 steps

    uint32_t stepDelayMs = durationMs / numSteps;

    // Ramp up
    for (uint32_t i = 1; i <= numSteps; i++) {
        uint8_t level = (uint8_t)((uint32_t)targetPercent * i / numSteps);
        setLevel(level, useGamma);
        vTaskDelay(pdMS_TO_TICKS(stepDelayMs));
    }

    // Ensure we hit exactly the target
    setLevel(targetPercent, useGamma);

    ESP_LOGI(TAG, "Soft start complete");
}


void MosfetDriver::softStartFull(uint32_t durationMs, bool useGamma) {
    softStart(100, durationMs, useGamma);
}


/*
 * =============================================================================
 * FADE FUNCTIONS
 * =============================================================================
 *
 * Uses hardware fade for smooth, non-blocking transitions.
 * The LEDC peripheral handles the fade in the background.
 */
void MosfetDriver::fadeTo(uint8_t targetPercent, uint32_t durationMs, bool useGamma) {
    if (!initialized) return;

    if (targetPercent > 100) targetPercent = 100;

    uint32_t targetDuty;
    if (useGamma) {
        targetDuty = applyGamma(targetPercent);
    } else {
        targetDuty = (uint32_t)targetPercent * maxDuty / 100;
    }

    ESP_LOGI(TAG, "Fading to %d%% over %lums (gamma=%s)", 
             targetPercent, durationMs, useGamma ? "on" : "off");

    ledc_set_fade_time_and_start(
        LEDC_LOW_SPEED_MODE,
        channel,
        targetDuty,
        durationMs,
        LEDC_FADE_NO_WAIT  // Non-blocking
    );

    currentLevel = targetPercent;
}


void MosfetDriver::fadeIn(uint32_t durationMs, bool useGamma) {
    setLevel(0, useGamma);  // Start from 0
    fadeTo(100, durationMs, useGamma);
}


void MosfetDriver::fadeOut(uint32_t durationMs) {
    fadeTo(0, durationMs, false);  // No gamma needed for fade to 0
}


/*
 * =============================================================================
 * SET FREQUENCY
 * =============================================================================
 *
 * Change PWM frequency at runtime.
 * Higher frequencies reduce flicker but may affect efficiency.
 *
 * Recommended:
 *     - 5000 Hz: Default, good balance
 *     - 10000 Hz: Less flicker on camera
 *     - 20000 Hz: Above human hearing (for audio-sensitive applications)
 */
bool MosfetDriver::setFrequency(uint32_t frequency) {
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
 *
 * Converts linear percentage to gamma-corrected duty cycle.
 * Makes LED dimming look more natural to human eyes.
 */
uint32_t MosfetDriver::applyGamma(uint8_t percent) {
    if (percent > 100) percent = 100;

    // Look up gamma-corrected value (0-1304 range)
    uint32_t gammaValue = gammaTable[percent];

    // Scale to actual maxDuty
    // gammaTable max is ~1304, so scale: duty = gammaValue * maxDuty / 1304
    uint32_t duty = (gammaValue * maxDuty) / 1304;

    return duty;
}
