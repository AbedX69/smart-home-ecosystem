/**
 * @file relay.h
 * @brief Relay and SSR (Solid State Relay) driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component drives mechanical relay modules and solid state relays.
 * Supports both active-LOW and active-HIGH trigger modules.
 * Simple GPIO on/off control - no PWM needed.
 *
 * @note
 * Electrical assumptions:
 * - Relay module has its own driver circuit (optocoupler/transistor)
 * - IN/signal pin accepts 3.3V logic from ESP32 GPIO
 * - VCC powered externally (5V for mechanical relays, varies for SSR)
 *
 * @par Supported hardware
 * - Mechanical relay modules (1/2/4/8 channel, blue box with coil)
 * - Solid State Relays (SSR, black box, silent, no moving parts)
 * - Any module with a digital trigger pin (HIGH or LOW to activate)
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
 * BEGINNER'S GUIDE: RELAYS AND SSRs
 * =============================================================================
 * 
 * A relay is an electrically controlled switch. It lets a small signal
 * (3.3V from ESP32) control a big load (mains 120V/240V, motors, etc.)
 * 
 * Think of it as a remote-controlled light switch:
 *     ESP32 says "ON"  → relay closes  → lamp turns on
 *     ESP32 says "OFF" → relay opens   → lamp turns off
 * 
 * =============================================================================
 * TWO TYPES OF RELAYS
 * =============================================================================
 * 
 * MECHANICAL RELAY (blue box, clicks):
 *     - Has a physical electromagnet inside
 *     - When energized, magnet pulls a metal contact closed
 *     - Makes a "click" sound when switching
 *     - Can handle AC and DC loads
 *     - Slower switching (~10ms), limited lifespan (~100k cycles)
 *     
 *         ┌─────────────────────┐
 *         │   ┌───┐   ┌─┐      │
 *         │   │coil│   │ ├──NO  │  ← Normally Open
 *         │   │    │   │●│      │     (connected when ON)
 *         │   └───┘   │ ├──COM │  ← Common
 *         │            │ ├──NC  │  ← Normally Closed
 *         │            └─┘      │     (connected when OFF)
 *         └──┬──┬──┬────────────┘
 *            IN VCC GND
 *         (from ESP32)
 * 
 * SOLID STATE RELAY (SSR, black box, silent):
 *     - No moving parts, uses semiconductors (TRIAC/MOSFET)
 *     - Completely silent switching
 *     - Much faster switching (<1ms)
 *     - Longer lifespan (millions of cycles)
 *     - Some only work with AC, some only DC (check yours!)
 *     
 *         ┌─────────────────┐
 *         │                 │
 *         │  SSR            │
 *         │                 │
 *         └──┬──┬──┬──┬────┘
 *            +  -  ~  ~
 *           IN GND  LOAD
 *        (control) (output)
 * 
 * =============================================================================
 * NO vs NC (OUTPUT SIDE)
 * =============================================================================
 * 
 * These are about the relay's OUTPUT contacts (the high-voltage side):
 * 
 *     NO = Normally Open:
 *         - Circuit is OPEN (disconnected) when relay is OFF
 *         - Circuit CLOSES (connected) when relay is ON
 *         - Use for: things you want OFF by default (most common)
 *     
 *     NC = Normally Closed:
 *         - Circuit is CLOSED (connected) when relay is OFF
 *         - Circuit OPENS (disconnected) when relay is ON
 *         - Use for: things you want ON by default (safety circuits)
 *     
 *     COM = Common:
 *         - Connect your load wire here (always)
 *     
 *     Wiring example (lamp with NO):
 *     
 *         Live ──► COM ──► NO ──► Lamp ──► Neutral
 *                   │
 *                   └── When relay turns ON, COM connects to NO
 *                        and lamp turns on
 * 
 * =============================================================================
 * ACTIVE LOW vs ACTIVE HIGH (TRIGGER SIDE)
 * =============================================================================
 * 
 * This is about the INPUT/trigger pin (the ESP32 side):
 * 
 *     ACTIVE LOW (most common for mechanical relay modules):
 *         - GPIO LOW  (0V)  = relay ON  (energized)
 *         - GPIO HIGH (3.3V) = relay OFF (de-energized)
 *         - Backwards from what you'd expect!
 *         - Why? The optocoupler sinks current to trigger
 *     
 *     ACTIVE HIGH (common for SSR modules):
 *         - GPIO HIGH (3.3V) = relay ON  (energized)
 *         - GPIO LOW  (0V)  = relay OFF (de-energized)
 *         - More intuitive: HIGH = ON
 *     
 *     This driver handles BOTH. Just tell it which type you have:
 *     
 *         Relay relay(GPIO_NUM_4, true);   // active LOW module
 *         Relay relay(GPIO_NUM_4, false);  // active HIGH module
 * 
 * =============================================================================
 * WHY DO MOST RELAY MODULES USE ACTIVE LOW?
 * =============================================================================
 * 
 * It's because of the optocoupler inside the module:
 * 
 *     ESP32 GPIO ──► Optocoupler LED ──► GND
 *                         │
 *                    (light triggers phototransistor)
 *                         │
 *                    Transistor ──► Relay coil
 * 
 * The optocoupler LED turns on when current flows FROM VCC THROUGH
 * the LED TO the GPIO pin. This happens when GPIO is LOW (sinking
 * current). So LOW = LED on = relay triggered.
 * 
 * =============================================================================
 * BOOT STATE SAFETY
 * =============================================================================
 * 
 * IMPORTANT: During ESP32 boot, GPIO pins can be in undefined states
 * (floating, briefly HIGH, etc.) This can cause relays to briefly
 * turn on during startup!
 * 
 * This driver handles this by:
 *     1. Setting the GPIO to the OFF state FIRST (before configuring as output)
 *     2. Then configuring it as an output
 * 
 * This minimizes the window where the relay might accidentally trigger.
 * 
 * =============================================================================
 * WIRING
 * =============================================================================
 * 
 *     ESP32             Relay Module
 *     ┌──────┐         ┌──────────────┐
 *     │      │         │              │
 *     │ GPIOx├────────►│ IN  (signal) │
 *     │      │         │              │
 *     │   5V ├────────►│ VCC (power)  │  ← Most relay modules need 5V
 *     │      │         │              │
 *     │  GND ├────────►│ GND (ground) │
 *     │      │         │              │
 *     └──────┘         └──────────────┘
 * 
 * NOTE: Some ESP32 dev boards have a 5V pin (from USB).
 * If not, power the relay module from an external 5V supply
 * (but share GND with the ESP32!).
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "relay.h"
 *     
 *     extern "C" void app_main(void) {
 *         // Active LOW mechanical relay on GPIO 4
 *         Relay light(GPIO_NUM_4, true);
 *         light.init();
 *         
 *         // Active HIGH SSR on GPIO 5
 *         Relay heater(GPIO_NUM_5, false);
 *         heater.init();
 *         
 *         light.on();              // Turn on the light
 *         vTaskDelay(pdMS_TO_TICKS(5000));
 *         light.off();             // Turn it off
 *         
 *         light.toggle();          // Flip the state
 *         
 *         bool isOn = light.isOn(); // Check current state
 *     }
 * 
 * =============================================================================
 */

#pragma once

#include <driver/gpio.h>
#include <stdint.h>
#include <stdbool.h>


/**
 * @class Relay
 * @brief Relay / SSR driver using simple GPIO control.
 *
 * @details
 * Handles both active-LOW and active-HIGH relay modules.
 * Provides on/off/toggle control with state tracking.
 */
class Relay {

public:

    /**
     * @brief Construct a new Relay instance.
     *
     * @param pin GPIO pin connected to relay module's IN/trigger pin.
     * @param activeLow True if relay turns ON when GPIO is LOW (most mechanical modules).
     *                   False if relay turns ON when GPIO is HIGH (most SSR modules).
     *
     * @note
     * Does not configure hardware. Call init() first.
     */
    Relay(gpio_num_t pin, bool activeLow = true);


    /**
     * @brief Destroy the Relay instance.
     *
     * @details
     * Turns the relay OFF before destroying (safe shutdown).
     */
    ~Relay();


    /**
     * @brief Initialize GPIO for relay control.
     *
     * @details
     * - Sets GPIO to OFF state BEFORE configuring as output (boot safety)
     * - Configures pin as push-pull output
     * - No pull-up/pull-down (relay module has its own)
     *
     * @note Relay starts in OFF state after init().
     */
    void init();


    // =========================== Core Functions ===========================

    /**
     * @brief Turn the relay ON.
     *
     * @note For active-LOW modules: sets GPIO LOW.
     *       For active-HIGH modules: sets GPIO HIGH.
     */
    void on();


    /**
     * @brief Turn the relay OFF.
     *
     * @note For active-LOW modules: sets GPIO HIGH.
     *       For active-HIGH modules: sets GPIO LOW.
     */
    void off();


    /**
     * @brief Toggle the relay state (ON → OFF, OFF → ON).
     */
    void toggle();


    /**
     * @brief Set relay to a specific state.
     *
     * @param state True = ON, False = OFF.
     */
    void set(bool state);


    /**
     * @brief Check if relay is currently ON.
     *
     * @return True if relay is ON.
     */
    bool isOn() const;


private:

    gpio_num_t pin;         // GPIO pin number
    bool activeLow;         // True = active LOW trigger
    bool currentState;      // True = relay is ON
    bool initialized;       // True after init()

    /**
     * @brief Apply the current state to the GPIO hardware.
     *
     * @details Handles active-LOW/HIGH inversion internally.
     */
    void applyState();
};
