/**
 * @file pwm_dimmer.h
 * @brief MOSFET PWM dimmer driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component handles PWM-based LED/light dimming using a MOSFET.
 * Works with any logic-level N-channel MOSFET (IRLB8721, IRLZ44N, etc.)
 *
 * @note
 * - Uses ESP32 LEDC peripheral for hardware PWM
 * - Supports multiple independent channels
 * - Configurable frequency and resolution
 * - Smooth fading with gamma correction option
 *
 * @par Tested MOSFETs
 * - IRLB8721 (logic level, 30V, 62A)
 * - IRLZ44N (logic level, 55V, 47A)
 * - IRF520 module (needs 10V gate, may be dim at 3.3V)
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: MOSFET PWM DIMMING
 * =============================================================================
 *
 * =============================================================================
 * THE PROBLEM: ESP32 CAN'T POWER LEDS DIRECTLY
 * =============================================================================
 *
 *     ESP32 GPIO pins can only output ~12mA at 3.3V.
 *     LED strips need 1-5+ Amps at 5V or 12V!
 *
 *         ESP32 GPIO ──── LED Strip
 *              │              │
 *            12mA          2000mA    ← NOT POSSIBLE!
 *            3.3V           12V
 *
 *     We need a "switch" that ESP32 can control to turn on/off high power.
 *
 * =============================================================================
 * THE SOLUTION: MOSFET AS A SWITCH
 * =============================================================================
 *
 *     MOSFET = Metal-Oxide-Semiconductor Field-Effect Transistor
 *
 *     Think of it as an electronic switch:
 *         - Small voltage on GATE controls the switch
 *         - DRAIN to SOURCE carries the heavy current
 *
 *                    DRAIN (to load)
 *                      │
 *                      │
 *              ┌───────┴───────┐
 *              │               │
 *     GATE ────┤   MOSFET      │
 *     (control)│   (switch)    │
 *              │               │
 *              └───────┬───────┘
 *                      │
 *                      │
 *                    SOURCE (to GND)
 *
 *     Gate HIGH (3.3V) → Switch ON  → Current flows Drain→Source
 *     Gate LOW  (0V)   → Switch OFF → No current flows
 *
 * =============================================================================
 * MOSFET PIN IDENTIFICATION (IRLB8721)
 * =============================================================================
 *
 *     Looking at the MOSFET with text facing you, metal tab at back:
 *
 *              ┌─────────────────┐
 *              │    (metal tab)  │
 *              │                 │
 *              │    IRLB8721     │
 *              │                 │
 *              └──┬────┬────┬────┘
 *                 │    │    │
 *                 G    D    S
 *                 │    │    │
 *               Gate Drain Source
 *               (1)  (2)   (3)
 *              Left  Mid  Right
 *
 *         G (Gate)   = Control pin, connect to ESP32 GPIO
 *         D (Drain)  = Load negative, connect to LED strip (-)
 *         S (Source) = Ground, connect to common GND
 *
 * =============================================================================
 * WIRING DIAGRAM
 * =============================================================================
 *
 *     ┌─────────────┐
 *     │   ESP32     │
 *     │             │
 *     │  GPIO 25 ───┼──────────────────┐
 *     │             │                  │
 *     │       GND ──┼───────┐          │
 *     └─────────────┘       │          │
 *                           │          │
 *     ┌─────────────┐       │          │
 *     │ 12V Power   │       │          │
 *     │ Supply      │       │          │
 *     │             │       │          │
 *     │        + ───┼───────┼─────┐    │
 *     │             │       │     │    │
 *     │        - ───┼───────┤     │    │
 *     └─────────────┘       │     │    │
 *                           │     │    │
 *                      Common GND │    │
 *                           │     │    │
 *     ┌─────────────┐       │     │    │
 *     │  LED Strip  │       │     │    │
 *     │             │       │     │    │
 *     │        + ───┼───────┼─────┘    │
 *     │             │       │          │
 *     │        - ───┼───┐   │          │
 *     └─────────────┘   │   │          │
 *                       │   │          │
 *                       │   │          │
 *                    ┌──┴───┴──┐       │
 *                    │ D     S │       │
 *                    │         │       │
 *                    │ MOSFET  │       │
 *                    │         │       │
 *                    │    G    │       │
 *                    └────┬────┘       │
 *                         │            │
 *                         └────────────┘
 *
 *     CONNECTIONS SUMMARY:
 *     1. ESP32 GPIO ────────→ MOSFET Gate (left pin)
 *     2. LED Strip (-) ─────→ MOSFET Drain (middle pin)
 *     3. MOSFET Source ─────→ Common GND (right pin)
 *     4. LED Strip (+) ─────→ Power Supply (+)
 *     5. Power Supply (-) ──→ Common GND
 *     6. ESP32 GND ─────────→ Common GND
 *
 * =============================================================================
 * PWM (PULSE WIDTH MODULATION) FOR DIMMING
 * =============================================================================
 *
 *     PWM rapidly switches the MOSFET on/off to control brightness:
 *
 *     100% brightness (always on):
 *         ┌────────────────────────────┐
 *         │                            │
 *     ────┘                            └────
 *
 *     50% brightness (on half the time):
 *         ┌──────┐      ┌──────┐
 *         │      │      │      │
 *     ────┘      └──────┘      └──────
 *
 *     25% brightness (on 1/4 the time):
 *         ┌──┐          ┌──┐
 *         │  │          │  │
 *     ────┘  └──────────┘  └──────────
 *
 *     The "duty cycle" is the percentage of time the signal is HIGH.
 *     Higher duty cycle = brighter light.
 *
 *     PWM frequency should be >1kHz to avoid visible flicker.
 *     We use 5kHz by default.
 *
 * =============================================================================
 * GAMMA CORRECTION
 * =============================================================================
 *
 *     Human eyes don't perceive brightness linearly!
 *
 *     Linear PWM:    50% duty = looks like 75% brightness
 *     Gamma corrected: 50% perceived = ~22% actual duty
 *
 *     This component includes optional gamma correction (gamma = 2.2)
 *     for more natural-looking dimming.
 *
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 *
 *     #include "pwm_dimmer.h"
 *
 *     void app_main(void) {
 *         PWMDimmer light(GPIO_NUM_25);
 *
 *         light.init();
 *
 *         // Set to 50% brightness
 *         light.setBrightness(50);
 *
 *         // Fade from 0 to 100% over 2 seconds
 *         light.fadeTo(100, 2000);
 *
 *         // Turn off
 *         light.off();
 *     }
 *
 * =============================================================================
 */

#pragma once

#include <driver/ledc.h>
#include <driver/gpio.h>
#include <stdint.h>


/**
 * @brief Default PWM configuration
 */
#define PWM_DIMMER_DEFAULT_FREQ     5000    // 5kHz (no visible flicker)
#define PWM_DIMMER_DEFAULT_RES      LEDC_TIMER_10_BIT  // 0-1023 steps


/**
 * @class PWMDimmer
 * @brief MOSFET PWM dimmer driver using LEDC peripheral.
 *
 * @details
 * Provides:
 * - Brightness control (0-100%)
 * - Smooth fading
 * - Gamma correction option
 * - Multiple independent channels
 */
class PWMDimmer {

public:

    /**
     * @brief Construct a new PWMDimmer instance.
     *
     * @param pin GPIO pin connected to MOSFET gate.
     * @param channel LEDC channel (0-7, default auto-assign).
     * @param timer LEDC timer (0-3, default LEDC_TIMER_0).
     */
    PWMDimmer(gpio_num_t pin, ledc_channel_t channel = LEDC_CHANNEL_0,
              ledc_timer_t timer = LEDC_TIMER_0);


    /**
     * @brief Destroy the PWMDimmer instance.
     */
    ~PWMDimmer();


    /**
     * @brief Initialize PWM output.
     *
     * @param frequency PWM frequency in Hz (default: 5000).
     * @param resolution Timer resolution (default: 10-bit = 1024 steps).
     * @return true if successful, false on error.
     */
    bool init(uint32_t frequency = PWM_DIMMER_DEFAULT_FREQ,
              ledc_timer_bit_t resolution = PWM_DIMMER_DEFAULT_RES);


    /**
     * @brief Set brightness level.
     *
     * @param percent Brightness 0-100%.
     * @param useGamma Apply gamma correction for natural dimming (default: true).
     */
    void setBrightness(uint8_t percent, bool useGamma = true);


    /**
     * @brief Set raw duty cycle (no gamma correction).
     *
     * @param duty Raw duty value (0 to maxDuty based on resolution).
     */
    void setDuty(uint32_t duty);


    /**
     * @brief Get current brightness level.
     *
     * @return Brightness 0-100%.
     */
    uint8_t getBrightness() const { return currentBrightness; }


    /**
     * @brief Get current raw duty cycle.
     *
     * @return Raw duty value.
     */
    uint32_t getDuty() const;


    /**
     * @brief Turn light fully on (100%).
     */
    void on();


    /**
     * @brief Turn light off (0%).
     */
    void off();


    /**
     * @brief Toggle between on and off.
     */
    void toggle();


    /**
     * @brief Check if light is on (brightness > 0).
     */
    bool isOn() const { return currentBrightness > 0; }


    /**
     * @brief Fade to target brightness over time.
     *
     * @param targetPercent Target brightness 0-100%.
     * @param durationMs Fade duration in milliseconds.
     * @param useGamma Apply gamma correction (default: true).
     */
    void fadeTo(uint8_t targetPercent, uint32_t durationMs, bool useGamma = true);


    /**
     * @brief Fade in from 0 to 100%.
     *
     * @param durationMs Fade duration in milliseconds.
     */
    void fadeIn(uint32_t durationMs);


    /**
     * @brief Fade out from current to 0%.
     *
     * @param durationMs Fade duration in milliseconds.
     */
    void fadeOut(uint32_t durationMs);


    /**
     * @brief Set PWM frequency.
     *
     * @param frequency New frequency in Hz.
     * @return true if successful.
     */
    bool setFrequency(uint32_t frequency);


    /**
     * @brief Get maximum duty value for current resolution.
     */
    uint32_t getMaxDuty() const { return maxDuty; }


private:

    gpio_num_t pin;
    ledc_channel_t channel;
    ledc_timer_t timer;
    ledc_timer_bit_t resolution;
    uint32_t maxDuty;
    uint8_t currentBrightness;
    bool initialized;


    /**
     * @brief Apply gamma correction to brightness.
     *
     * @param percent Linear brightness 0-100%.
     * @return Gamma-corrected duty cycle.
     */
    uint32_t applyGamma(uint8_t percent);
};
