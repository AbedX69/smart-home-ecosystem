/**
 * @file mosfet_driver.h
 * @brief High-power MOSFET driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component drives high-power MOSFET modules for LED dimming and
 * DC load control. Supports PWM dimming, soft-start, and basic on/off.
 *
 * @note
 * - Uses ESP32 LEDC peripheral for hardware PWM
 * - Works with MOSFET modules (15A/400W boards) and discrete MOSFETs
 * - PWM frequency 0-20kHz (5kHz default, no audible whine)
 *
 * @par Supported hardware
 * - High-power MOSFET trigger boards (5-36V, 15A/30A, 400W)
 * - IRLB8721 discrete MOSFET (logic level, 30V, 62A)
 * - IRF520 modules (needs higher gate voltage, may be dim at 3.3V)
 * - Any MOSFET with digital trigger input
 *
 * @par Tested boards
 * - ESP32D (original ESP32)
 * - ESP32-S3 WROOM
 * - ESP32-S3 Seeed XIAO
 * - ESP32-C6 WROOM
 * - ESP32-C6 Seeed XIAO
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: HIGH-POWER MOSFET DRIVER MODULES
 * =============================================================================
 *
 * =============================================================================
 * WHAT IS A MOSFET DRIVER MODULE?
 * =============================================================================
 *
 *     These are small boards that let you control high-power DC loads
 *     (LEDs, motors, heaters) with a low-power signal from ESP32.
 *
 *     Your module (15A 400W):
 *
 *         ┌─────────────────────────────────┐
 *         │                                 │
 *         │   [MOSFET]  [MOSFET]            │  ← Dual MOSFETs for more current
 *         │                                 │
 *         │   PWM+ PWM- VIN+ VIN-           │
 *         │    ●    ●    ●    ●             │
 *         └────┬────┬────┬────┬─────────────┘
 *              │    │    │    │
 *              │    │    │    └── VIN- : Load negative (LED-, motor-)
 *              │    │    └─────── VIN+ : Load positive (LED+, motor+)
 *              │    └──────────── PWM- : Ground (connect to ESP32 GND)
 *              └───────────────── PWM+ : Signal (connect to ESP32 GPIO)
 *
 *     SPECS:
 *         - Input voltage: DC 5V-36V (for the load, not the signal)
 *         - Signal voltage: DC 3.3V-20V (ESP32's 3.3V works!)
 *         - Max current: 15A continuous, 30A peak
 *         - Max power: 400W
 *         - PWM frequency: 0-20kHz
 *
 * =============================================================================
 * HOW IT WORKS
 * =============================================================================
 *
 *     The module acts as a high-power switch controlled by your ESP32:
 *
 *         ESP32 GPIO                    MOSFET Module
 *         ┌────────┐                   ┌─────────────┐
 *         │        │                   │             │
 *         │   3.3V ├──► PWM+ ──────────┤  TRIGGER    │
 *         │        │                   │     │       │
 *         │   GND  ├──► PWM- ──────────┤     ▼       │
 *         │        │                   │  [MOSFET]   │
 *         └────────┘                   │     │       │
 *                                      │     ▼       │
 *         Power Supply                 │   OUTPUT    │
 *         ┌────────┐                   │             │
 *         │   12V+ ├──────────────────►│ VIN+        │
 *         │        │                   │             │
 *         │   12V- ├──► (through load)─┤ VIN-        │
 *         └────────┘        ▲          └─────────────┘
 *                           │
 *                      LED Strip
 *                      or Motor
 *
 *     Signal HIGH → MOSFET ON  → Current flows → Load is powered
 *     Signal LOW  → MOSFET OFF → No current   → Load is off
 *
 * =============================================================================
 * PWM FOR DIMMING
 * =============================================================================
 *
 *     PWM (Pulse Width Modulation) rapidly switches the MOSFET on/off:
 *
 *         100% duty (always on):
 *         ────────────────────────────  Full brightness
 *
 *         50% duty (on half the time):
 *         ████████        ████████      Medium brightness
 *
 *         25% duty (on 1/4 time):
 *         ████            ████          Dim
 *
 *         0% duty (always off):
 *         ________________________      Off
 *
 *     At 5kHz, this happens 5000 times per second - way too fast
 *     for your eye to see flicker. You just see smooth dimming.
 *
 * =============================================================================
 * SOFT START - PROTECTING YOUR LEDS
 * =============================================================================
 *
 *     When you suddenly apply full power to LEDs, there's an "inrush current"
 *     spike that can stress components:
 *
 *         WITHOUT soft start:
 *
 *         Current
 *           │    ╱╲ ← Inrush spike!
 *         2A│   ╱  ╲────────────────
 *           │  ╱
 *         0 │─╱─────────────────────
 *              │
 *              Turn on
 *
 *         WITH soft start (ramp up over 500ms):
 *
 *         Current
 *           │         ╱────────────
 *         2A│       ╱
 *           │     ╱
 *           │   ╱
 *         0 │─╱─────────────────────
 *              │    │
 *              Start End (500ms later)
 *
 *     Soft start gradually increases power, avoiding the spike.
 *     This is gentler on LEDs, capacitors, and power supplies.
 *
 * =============================================================================
 * COMPLETE WIRING DIAGRAM
 * =============================================================================
 *
 *     ┌─────────────────┐
 *     │     ESP32       │
 *     │                 │
 *     │   GPIO 4 ───────┼──────────────────────┐
 *     │                 │                      │
 *     │      GND ───────┼───────────┐          │
 *     └─────────────────┘           │          │
 *                                   │          │
 *     ┌─────────────────┐           │          │
 *     │  Power Supply   │           │          │
 *     │  (12V / 24V)    │           │          │
 *     │                 │           │          │
 *     │       (+) ──────┼───────────┼────┐     │
 *     │                 │           │    │     │
 *     │       (-) ──────┼───────────┤    │     │
 *     └─────────────────┘           │    │     │
 *                              Common GND│     │
 *                                   │    │     │
 *     ┌─────────────────┐           │    │     │
 *     │   LED Strip     │           │    │     │
 *     │                 │           │    │     │
 *     │       (+) ──────┼───────────┼────┘     │
 *     │                 │           │          │
 *     │       (-) ──────┼───────┐   │          │
 *     └─────────────────┘       │   │          │
 *                               │   │          │
 *     ┌─────────────────────────┼───┼──────────┼─┐
 *     │  MOSFET Module          │   │          │ │
 *     │                         │   │          │ │
 *     │   VIN- ◄────────────────┘   │          │ │
 *     │                             │          │ │
 *     │   VIN+ ◄── (from PSU +) ────┘          │ │
 *     │                                        │ │
 *     │   PWM- ◄───────────────────────────────┘ │
 *     │                                          │
 *     │   PWM+ ◄─────────────────────────────────┘
 *     │                                          │
 *     └──────────────────────────────────────────┘
 *
 *     WIRING SUMMARY:
 *         ESP32 GPIO  → PWM+ (signal)
 *         ESP32 GND   → PWM- (signal ground)
 *         PSU (+)     → VIN+ and LED (+)
 *         PSU (-)     → Common GND
 *         LED (-)     → VIN- (load goes through MOSFET)
 *
 * =============================================================================
 * MOSFET MODULE vs DISCRETE MOSFET
 * =============================================================================
 *
 *     MOSFET MODULE (what you have):
 *         ✓ Built-in gate driver circuit
 *         ✓ Works directly with 3.3V signal
 *         ✓ Has screw terminals for easy wiring
 *         ✓ Often has dual MOSFETs for more current
 *         ✓ Just connect and go!
 *
 *     DISCRETE MOSFET (like IRLB8721):
 *         - Need to wire it yourself
 *         - Need gate resistor (100-470 ohm)
 *         - "Logic level" MOSFETs work at 3.3V
 *         - Standard MOSFETs need 10V gate driver
 *         - More compact, cheaper per unit
 *
 *     For most projects, the module is easier. Discrete MOSFETs
 *     are better for custom PCBs or space-constrained designs.
 *
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 *
 *     #include "mosfet_driver.h"
 *
 *     void app_main(void) {
 *         MosfetDriver led(GPIO_NUM_4);
 *         led.init();
 *
 *         // Basic on/off
 *         led.on();
 *         vTaskDelay(pdMS_TO_TICKS(2000));
 *         led.off();
 *
 *         // Set to 50% brightness
 *         led.setLevel(50);
 *
 *         // Soft start: ramp from 0 to 100% over 1 second
 *         led.softStart(100, 1000);
 *
 *         // Fade to 25% over 500ms
 *         led.fadeTo(25, 500);
 *     }
 *
 * =============================================================================
 * TROUBLESHOOTING
 * =============================================================================
 *
 *     "LED doesn't turn on":
 *         - Check VIN+ and VIN- aren't swapped
 *         - Verify power supply is on and correct voltage
 *         - Make sure PWM- is connected to ESP32 GND
 *         - Try setLevel(100) to rule out PWM issues
 *
 *     "LED flickers":
 *         - Increase PWM frequency (try 10000 or 20000 Hz)
 *         - Check for loose connections
 *         - Power supply may be undersized
 *
 *     "LED only dims slightly":
 *         - Module may need higher gate voltage (IRF520 issue)
 *         - Try a different GPIO pin
 *         - Verify 3.3V is reaching PWM+ pin
 *
 *     "Module gets hot":
 *         - Normal for high currents
 *         - Add heatsink if continuous high load
 *         - Check you're not exceeding 15A
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
#define MOSFET_DEFAULT_FREQ     5000                // 5kHz (no audible whine)
#define MOSFET_DEFAULT_RES      LEDC_TIMER_10_BIT   // 0-1023 steps


/**
 * @class MosfetDriver
 * @brief High-power MOSFET driver using LEDC PWM.
 *
 * @details
 * Provides:
 * - Level control (0-100%)
 * - Soft start (gradual ramp-up)
 * - Smooth fading
 * - Basic on/off
 */
class MosfetDriver {

public:

    /**
     * @brief Construct a new MosfetDriver instance.
     *
     * @param pin GPIO pin connected to MOSFET PWM+ / gate.
     * @param channel LEDC channel (0-7, default LEDC_CHANNEL_2 to avoid conflicts).
     * @param timer LEDC timer (0-3, default LEDC_TIMER_2).
     */
    MosfetDriver(gpio_num_t pin, 
                 ledc_channel_t channel = LEDC_CHANNEL_2,
                 ledc_timer_t timer = LEDC_TIMER_2);


    /**
     * @brief Destroy the MosfetDriver instance.
     */
    ~MosfetDriver();


    /**
     * @brief Initialize PWM output.
     *
     * @param frequency PWM frequency in Hz (default: 5000).
     * @param resolution Timer resolution (default: 10-bit = 1024 steps).
     * @return true if successful, false on error.
     */
    bool init(uint32_t frequency = MOSFET_DEFAULT_FREQ,
              ledc_timer_bit_t resolution = MOSFET_DEFAULT_RES);


    // =========================== Basic Control ===========================

    /**
     * @brief Set output level.
     *
     * @param percent Level 0-100%.
     * @param useGamma Apply gamma correction for natural LED dimming (default: false).
     */
    void setLevel(uint8_t percent, bool useGamma = false);


    /**
     * @brief Get current output level.
     *
     * @return Level 0-100%.
     */
    uint8_t getLevel() const { return currentLevel; }


    /**
     * @brief Turn output fully on (100%).
     */
    void on();


    /**
     * @brief Turn output off (0%).
     */
    void off();


    /**
     * @brief Toggle between on and off.
     */
    void toggle();


    /**
     * @brief Check if output is on (level > 0).
     */
    bool isOn() const { return currentLevel > 0; }


    // =========================== Soft Start ===========================

    /**
     * @brief Soft start - gradually ramp up from 0 to target level.
     *
     * @param targetPercent Target level 0-100%.
     * @param durationMs Ramp duration in milliseconds.
     * @param useGamma Apply gamma correction (default: false).
     *
     * @note Blocks until ramp is complete.
     *       Use for power-on to avoid inrush current spikes.
     */
    void softStart(uint8_t targetPercent, uint32_t durationMs, bool useGamma = false);


    /**
     * @brief Soft start to full power (100%).
     *
     * @param durationMs Ramp duration in milliseconds (default: 500ms).
     * @param useGamma Apply gamma correction (default: false).
     */
    void softStartFull(uint32_t durationMs = 500, bool useGamma = false);


    // =========================== Fading ===========================

    /**
     * @brief Fade to target level over time.
     *
     * @param targetPercent Target level 0-100%.
     * @param durationMs Fade duration in milliseconds.
     * @param useGamma Apply gamma correction (default: false).
     *
     * @note Non-blocking - fade runs in hardware.
     */
    void fadeTo(uint8_t targetPercent, uint32_t durationMs, bool useGamma = false);


    /**
     * @brief Fade in from 0 to 100%.
     *
     * @param durationMs Fade duration in milliseconds.
     * @param useGamma Apply gamma correction (default: false).
     */
    void fadeIn(uint32_t durationMs, bool useGamma = false);


    /**
     * @brief Fade out from current level to 0%.
     *
     * @param durationMs Fade duration in milliseconds.
     */
    void fadeOut(uint32_t durationMs);


    // =========================== Advanced ===========================

    /**
     * @brief Set raw duty cycle directly.
     *
     * @param duty Raw duty value (0 to maxDuty based on resolution).
     */
    void setDuty(uint32_t duty);


    /**
     * @brief Get current raw duty cycle.
     *
     * @return Raw duty value.
     */
    uint32_t getDuty() const;


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
    uint8_t currentLevel;
    bool initialized;

    /**
     * @brief Apply gamma correction to level.
     *
     * @param percent Linear level 0-100%.
     * @return Gamma-corrected duty cycle.
     */
    uint32_t applyGamma(uint8_t percent);
};
