/**
 * @file touch.h
 * @brief Capacitive touch sensor driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component handles capacitive touch modules like TTP223 and HTTM.
 * These modules have built-in touch detection ICs, so they output
 * a simple digital HIGH/LOW signal - no complex processing needed!
 *
 * @note
 * Unlike mechanical buttons, capacitive touch modules:
 * - Don't need pull-up resistors (they have active outputs)
 * - Don't need debouncing (the IC handles it)
 * - React to proximity, not just contact
 *
 * @par Supported hardware
 * - TTP223 Touch Button Module (3-pin: VCC, GND, OUT)
 * - HTTM Capacitive Touch LED Module (touch + RGB LED)
 * - Any capacitive touch module with digital output
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
 * BEGINNER'S GUIDE: CAPACITIVE TOUCH SENSORS
 * =============================================================================
 * 
 * Capacitive touch is DIFFERENT from mechanical buttons!
 * 
 * =============================================================================
 * HOW CAPACITIVE TOUCH WORKS
 * =============================================================================
 * 
 * Your body is slightly conductive and acts like a capacitor.
 * When you touch (or even get close to) a metal pad, you change
 * the capacitance that the sensor chip measures.
 * 
 *     NO TOUCH:                  TOUCHING:
 *     
 *        ┌─────┐                    ┌─────┐
 *        │ Pad │                    │ Pad │ ← Finger
 *        └──┬──┘                    └──┬──┘
 *           │                          │
 *       ┌───┴───┐                  ┌───┴───┐
 *       │TTP223 │                  │TTP223 │
 *       │  IC   │                  │  IC   │
 *       └───┬───┘                  └───┬───┘
 *           │                          │
 *       OUT = LOW                  OUT = HIGH
 * 
 * The TTP223 chip does all the work:
 *     - Measures capacitance
 *     - Filters noise
 *     - Outputs clean HIGH/LOW signal
 * 
 * =============================================================================
 * TTP223 MODULE PINOUT
 * =============================================================================
 * 
 *     ┌─────────────────────┐
 *     │    ○ Touch Pad      │
 *     │                     │
 *     │  [VCC] [OUT] [GND]  │
 *     └───┬─────┬─────┬─────┘
 *         │     │     │
 *        3.3V  GPIO  GND
 * 
 * Only 3 wires needed!
 *     - VCC → 3.3V (some modules also work with 5V)
 *     - GND → Ground
 *     - OUT → Any GPIO pin
 * 
 * =============================================================================
 * TTP223 ACTIVE HIGH vs ACTIVE LOW
 * =============================================================================
 * 
 * Some TTP223 modules have solder jumpers to configure behavior:
 * 
 *     Mode A (default on most modules):
 *         - Touch → HIGH
 *         - No touch → LOW
 *         - This is "ACTIVE HIGH"
 *     
 *     Mode B (alternate):
 *         - Touch → LOW
 *         - No touch → HIGH
 *         - This is "ACTIVE LOW"
 * 
 * Our code supports both - just set the activeHigh parameter!
 * 
 * =============================================================================
 * HTTM MODULE (TOUCH + LED)
 * =============================================================================
 * 
 * The HTTM module is a TTP223-based touch sensor WITH an RGB LED.
 * 
 *     ┌─────────────────────┐
 *     │    ○ Touch Pad      │
 *     │       (LED)         │
 *     │                     │
 *     │ VCC OUT GND LED+    │  (varies by version)
 *     └──┬───┬───┬───┬──────┘
 * 
 * The touch part works the same as TTP223.
 * The LED can be controlled separately (future feature).
 * 
 * =============================================================================
 * NO DEBOUNCING NEEDED!
 * =============================================================================
 * 
 * Unlike mechanical buttons, capacitive touch modules:
 *     - Have built-in filtering in the IC
 *     - Don't physically bounce (no moving parts!)
 *     - Output clean, stable signals
 * 
 * We still do edge detection, but we don't need debounce timers.
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "touch.h"
 *     
 *     void app_main(void) {
 *         // Create touch sensor on GPIO 4
 *         TouchSensor touch(GPIO_NUM_4);
 *         
 *         // Initialize
 *         touch.init();
 *         
 *         while(1) {
 *             // Update state (call this regularly)
 *             touch.update();
 *             
 *             // Check for touch event
 *             if (touch.wasTouched()) {
 *                 printf("Touched!\n");
 *             }
 *             
 *             // Or check current state
 *             if (touch.isTouched()) {
 *                 printf("Still touching...\n");
 *             }
 *             
 *             vTaskDelay(pdMS_TO_TICKS(10));
 *         }
 *     }
 * 
 * =============================================================================
 */

#pragma once

#include <driver/gpio.h>
#include <esp_timer.h>
#include <stdint.h>


/**
 * @class TouchSensor
 * @brief Capacitive touch sensor driver (TTP223, HTTM, etc.)
 *
 * @details
 * Simple polling-based driver for capacitive touch modules.
 * These modules output digital HIGH/LOW signals, so no complex
 * processing is needed - just read the GPIO!
 */
class TouchSensor {

public:

    /**
     * @brief Construct a new TouchSensor instance.
     *
     * @param pin GPIO pin connected to the module's OUT pin.
     * @param activeHigh true if module outputs HIGH when touched (default).
     *                   false if module outputs LOW when touched.
     *
     * @note
     * Most TTP223 modules are active HIGH by default.
     * Check your module's documentation or test it!
     */
    TouchSensor(gpio_num_t pin, bool activeHigh = true);


    /**
     * @brief Destroy the TouchSensor instance.
     */
    ~TouchSensor();


    /**
     * @brief Initialize GPIO for the touch sensor.
     *
     * @details
     * Configures pin as input (no pull-up needed - module has active output).
     */
    void init();


    /**
     * @brief Update touch state (call this regularly in your loop).
     *
     * @details
     * Reads the GPIO and updates internal state for edge detection.
     */
    void update();


    /**
     * @brief Check if sensor is currently being touched.
     *
     * @return true if touched right now.
     */
    bool isTouched() const;


    /**
     * @brief Check if sensor was just touched (edge detection).
     *
     * @return true once when touch begins.
     */
    bool wasTouched();


    /**
     * @brief Check if sensor was just released (edge detection).
     *
     * @return true once when touch ends.
     */
    bool wasReleased();


    /**
     * @brief Get how long the sensor has been touched (milliseconds).
     *
     * @return Duration in ms, or 0 if not touched.
     */
    uint32_t getTouchedDuration() const;


private:

    gpio_num_t pin;             // GPIO pin number
    bool activeHigh;            // true = HIGH when touched

    bool currentState;          // Current state (true = touched)
    bool lastState;             // Previous state (for edge detection)

    uint64_t touchStartTime;    // When touch began

    bool touchedFlag;           // Flag: just touched
    bool releasedFlag;          // Flag: just released
};
