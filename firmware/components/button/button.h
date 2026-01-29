/**
 * @file button.h
 * @brief Simple button/switch driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component handles physical buttons and mechanical switches.
 * Supports both momentary (push) buttons and toggle switches.
 * Includes software debouncing and edge detection.
 *
 * @note
 * Electrical assumptions:
 * - Internal pull-up resistors are enabled
 * - Buttons connect to GND when pressed (active LOW)
 *
 * @par Supported hardware
 * - Tactile push buttons (12x12mm, 6x6mm, etc.)
 * - Mechanical keyboard switches (Cherry MX, Outemu, Gateron, etc.)
 * - Any momentary SPST switch
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
 * BEGINNER'S GUIDE: BUTTONS AND SWITCHES
 * =============================================================================
 * 
 * This is simpler than the rotary encoder! A button is just an on/off switch.
 * 
 * =============================================================================
 * HOW A BUTTON WORKS ELECTRICALLY
 * =============================================================================
 * 
 * A button is just two pieces of metal that touch when you press it:
 * 
 *     NOT PRESSED:              PRESSED:
 *     
 *         3.3V                      3.3V
 *          │                         │
 *          ├── Pull-up resistor      ├── Pull-up resistor
 *          │                         │
 *     Pin ─┤                    Pin ─┤
 *          │                         │
 *          ○  ← Gap (open)          ─┴─ ← Connected (closed)
 *          │                         │
 *         GND                       GND
 *     
 *     Pin reads: HIGH (1)       Pin reads: LOW (0)
 * 
 * This is called "ACTIVE LOW" - the button is "active" (pressed) when LOW.
 * 
 * =============================================================================
 * WHY DO WE NEED A PULL-UP RESISTOR?
 * =============================================================================
 * 
 * Without a pull-up, when the button is NOT pressed, the pin is connected
 * to nothing - it "floats" and picks up random electrical noise.
 * 
 *     WITHOUT PULL-UP:              WITH PULL-UP:
 *     
 *     Pin ─┤                         3.3V
 *          │                          │
 *          ○  ← Button open          ├── Pull-up resistor
 *          │                          │
 *         GND                    Pin ─┤
 *                                     │
 *     Pin reads: ??? (random!)        ○  ← Button open
 *                                     │
 *                                    GND
 *                                
 *                                Pin reads: HIGH (stable!)
 * 
 * ESP32 has internal pull-up resistors we can enable in software.
 * 
 * =============================================================================
 * BUTTON TYPES YOU HAVE
 * =============================================================================
 * 
 * TACTILE PUSH BUTTON (12x12mm):
 *     - 4 pins, but only 2 connections (pins are paired internally)
 *     - Momentary: only closed while you hold it
 *     - Typical pinout:
 *     
 *           ┌─────┐
 *         1─┤     ├─2      Pins 1-2 are connected
 *           │ ○○○ │        Pins 3-4 are connected
 *         3─┤     ├─4      Button connects 1-2 to 3-4
 *           └─────┘
 *     
 *     - Use any diagonal pair: (1,4) or (2,3)
 * 
 * MECHANICAL KEYBOARD SWITCH (Outemu, Cherry MX, etc.):
 *     - 5 pins: 2 for switch, 2 for LED, 1 for stability
 *     - We only care about the 2 switch pins
 *     - Feels much nicer than tactile buttons!
 *     
 *           ┌─────────┐
 *           │    ○    │  ← Stem you press
 *           │  ┌───┐  │
 *           └──┴───┴──┘
 *              │ │ │││
 *              └─┘ └┴┘
 *           Switch  LED + stability
 *           pins    pins (ignore)
 * 
 * =============================================================================
 * DEBOUNCING (Same concept as encoder!)
 * =============================================================================
 * 
 * When you press a button, the metal contacts "bounce":
 * 
 *     What you expect:          What actually happens:
 *     
 *          ┌─────                    ┌─┬─┬───
 *          │                         │ │ │
 *     ─────┘                    ─────┴─┴─┘
 *     
 *     One clean                 Multiple rapid
 *     press                     on/off/on/off
 * 
 * Without debouncing, one press might register as 5-10 presses!
 * We filter this by ignoring changes that happen within ~50ms.
 * 
 * =============================================================================
 * EDGE DETECTION
 * =============================================================================
 * 
 * Sometimes you want to know:
 *     - "Is the button pressed RIGHT NOW?" → isPressed()
 *     - "Was the button JUST pressed?" → wasPressed() (once per press)
 *     - "Was the button JUST released?" → wasReleased() (once per release)
 * 
 * Edge detection catches the MOMENT of change, not the ongoing state.
 * 
 *     Button timeline:
 *     
 *     Released ─────┐              ┌───────── Released
 *                   │              │
 *     Pressed       └──────────────┘
 *                   ↑              ↑
 *                   │              │
 *         wasPressed() = true     wasReleased() = true
 *         (just this moment)      (just this moment)
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "button.h"
 *     
 *     void app_main(void) {
 *         // Create button on GPIO 5
 *         Button myButton(GPIO_NUM_5);
 *         
 *         // Initialize (configures GPIO with pull-up)
 *         myButton.init();
 *         
 *         while(1) {
 *             // Check for button press (once per press)
 *             if (myButton.wasPressed()) {
 *                 printf("Button pressed!\n");
 *             }
 *             
 *             // Or check current state (true while held)
 *             if (myButton.isPressed()) {
 *                 printf("Button is being held...\n");
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
 * @class Button
 * @brief Simple button driver with debouncing and edge detection.
 *
 * @details
 * Handles physical buttons with:
 * - Software debouncing (configurable, default 50ms)
 * - Edge detection (press and release events)
 * - Active-low logic (pressed = GPIO reads LOW)
 */
class Button {

public:

    /**
     * @brief Construct a new Button instance.
     *
     * @param pin GPIO pin the button is connected to.
     * @param debounceMs Debounce time in milliseconds (default: 50ms).
     *
     * @note
     * Does not configure GPIO. Call init() to set up hardware.
     */
    Button(gpio_num_t pin, uint32_t debounceMs = 50);


    /**
     * @brief Destroy the Button instance.
     */
    ~Button();


    /**
     * @brief Initialize GPIO for the button.
     *
     * @details
     * - Configures pin as input
     * - Enables internal pull-up resistor
     * - Reads initial button state
     */
    void init();


    /**
     * @brief Update button state (call this regularly in your loop).
     *
     * @details
     * Reads the GPIO, applies debouncing, and updates internal state.
     * Must be called frequently (every 10-50ms) for responsive detection.
     *
     * @note
     * Unlike the encoder (which uses interrupts), buttons are polled.
     * This is fine because human button presses are slow.
     */
    void update();


    /**
     * @brief Check if button is currently pressed.
     *
     * @return true if button is pressed right now.
     *
     * @note Returns true continuously while button is held.
     */
    bool isPressed() const;


    /**
     * @brief Check if button was just pressed (edge detection).
     *
     * @return true once when button transitions from released to pressed.
     *
     * @note Automatically clears after reading. Call update() regularly.
     */
    bool wasPressed();


    /**
     * @brief Check if button was just released (edge detection).
     *
     * @return true once when button transitions from pressed to released.
     *
     * @note Automatically clears after reading. Call update() regularly.
     */
    bool wasReleased();


    /**
     * @brief Get how long the button has been held (in milliseconds).
     *
     * @return Time in ms since button was pressed, or 0 if not pressed.
     *
     * @note Useful for detecting long-press vs short-press.
     */
    uint32_t getPressedDuration() const;


private:

    gpio_num_t pin;                 // GPIO pin number
    uint32_t debounceTimeUs;        // Debounce time in microseconds

    bool currentState;              // Current debounced state (true = pressed)
    bool lastState;                 // Previous state (for edge detection)
    bool lastRawState;              // Last raw reading (for debounce)
    
    uint64_t lastChangeTime;        // When state last changed (for debounce)
    uint64_t pressStartTime;        // When button was pressed (for duration)

    bool pressedFlag;               // Flag: button was just pressed
    bool releasedFlag;              // Flag: button was just released
};
