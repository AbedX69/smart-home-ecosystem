/**
 * @file relay.cpp
 * @brief Relay and SSR driver implementation (ESP-IDF).
 *
 * @details
 * Implements simple GPIO-based relay control with active-LOW/HIGH support.
 * This is the simplest driver in the component library - just GPIO high/low.
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: HOW THIS IMPLEMENTATION WORKS
 * =============================================================================
 * 
 * This is by far the simplest driver. No PWM, no background tasks,
 * no timers. Just GPIO high and low:
 * 
 *     on()  → set GPIO to the "active" level
 *     off() → set GPIO to the "inactive" level
 * 
 * The only tricky part is the active-LOW / active-HIGH inversion:
 * 
 *     ACTIVE HIGH (intuitive):
 *         on()  → GPIO HIGH → relay energized
 *         off() → GPIO LOW  → relay de-energized
 *     
 *     ACTIVE LOW (inverted):
 *         on()  → GPIO LOW  → relay energized
 *         off() → GPIO HIGH → relay de-energized
 * 
 * The applyState() function handles this inversion so the rest
 * of the code can just think in terms of "on" and "off".
 * 
 * =============================================================================
 * BOOT SAFETY
 * =============================================================================
 * 
 * During ESP32 boot, GPIO pins go through several states:
 * 
 *     Power on → pins floating → bootloader → your code runs
 *                 ↑
 *                 This is dangerous! Floating pins can be
 *                 read as HIGH or LOW randomly.
 * 
 * For an active-LOW relay, a floating pin might be seen as LOW,
 * which would turn the relay ON during boot. Not great if it
 * controls a heater or garage door!
 * 
 * Our init() sequence minimizes this:
 *     1. Set the output level to OFF state FIRST (gpio_set_level)
 *     2. THEN configure as output (gpio_config)
 * 
 * This way, the moment the pin becomes an output, it's already
 * in the correct OFF state. The dangerous floating window is
 * as short as possible.
 * 
 * =============================================================================
 * COMPARISON TO OTHER DRIVERS
 * =============================================================================
 * 
 *     Component     Peripheral    Complexity    Background tasks?
 *     ─────────     ──────────    ──────────    ─────────────────
 *     Relay         GPIO          Very simple   No
 *     Button        GPIO          Simple        No (polling)
 *     Buzzer        LEDC (PWM)    Medium        Yes
 *     Vibration     LEDC (PWM)    Medium        Yes
 *     Encoder       GPIO + ISR    Complex       No (interrupts)
 *     Display       SPI/I2C       Complex       No
 * 
 * =============================================================================
 */

#include "relay.h"
#include <esp_log.h>


/*
 * Logging tag for ESP_LOGI, ESP_LOGE, etc.
 */
static const char *TAG = "Relay";


/* ============================= Constructor / Destructor ============================= */

/**
 * @brief Construct a Relay instance.
 *
 * @param pin GPIO pin number.
 * @param activeLow True for active-LOW modules.
 */

/*
 * =============================================================================
 * CONSTRUCTOR
 * =============================================================================
 * 
 * Same pattern as all other components.
 * Stores config, starts in OFF state. Hardware setup in init().
 */
Relay::Relay(gpio_num_t pin, bool activeLow)
    : pin(pin),
      activeLow(activeLow),
      currentState(false),          // Start OFF
      initialized(false)
{
}


/**
 * @brief Destructor.
 */

/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 * 
 * Turns relay OFF for safety. You don't want a relay stuck ON
 * if the Relay object goes out of scope.
 */
Relay::~Relay() {
    if (initialized) {
        off();
    }
}


/* ============================= Initialization ============================= */

/**
 * @brief Initialize GPIO.
 */

/*
 * =============================================================================
 * INITIALIZATION
 * =============================================================================
 * 
 * Boot-safe initialization sequence:
 * 
 *     Step 1: Set GPIO level to OFF state BEFORE making it an output.
 *             This prevents a brief "glitch" where the relay might
 *             turn on during the transition from floating to output.
 *     
 *     Step 2: Configure GPIO as push-pull output.
 *             No pull-up/pull-down needed (relay module has its own).
 *             No interrupts (we only write to this pin, never read).
 * 
 * ACTIVE LOW:  OFF state = GPIO HIGH (3.3V)
 * ACTIVE HIGH: OFF state = GPIO LOW  (0V)
 */
void Relay::init() {
    ESP_LOGI(TAG, "Initializing relay on GPIO %d (active %s)",
             pin, activeLow ? "LOW" : "HIGH");

    /*
     * Step 1: Pre-set the GPIO level to OFF state BEFORE configuring.
     * 
     * For active LOW:  OFF = HIGH level (relay needs LOW to trigger)
     * For active HIGH: OFF = LOW level  (relay needs HIGH to trigger)
     */
    gpio_set_level(pin, activeLow ? 1 : 0);

    /*
     * Step 2: Configure GPIO as output.
     */
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.mode         = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type    = GPIO_INTR_DISABLE;

    gpio_config(&io_conf);

    currentState = false;   // Relay starts OFF
    initialized = true;

    ESP_LOGI(TAG, "Relay initialized on GPIO %d (OFF)", pin);
}


/* ============================= Internal Helpers ============================= */

/**
 * @brief Apply current state to GPIO hardware.
 */

/*
 * =============================================================================
 * APPLY STATE - HANDLES THE ACTIVE LOW/HIGH INVERSION
 * =============================================================================
 * 
 * This is the ONLY place where we think about active-LOW vs active-HIGH.
 * Everything else just sets currentState to true/false and calls this.
 * 
 *     currentState = true  (ON):
 *         Active LOW  → GPIO = 0 (LOW)
 *         Active HIGH → GPIO = 1 (HIGH)
 *     
 *     currentState = false (OFF):
 *         Active LOW  → GPIO = 1 (HIGH)
 *         Active HIGH → GPIO = 0 (LOW)
 * 
 * The XOR trick:
 *     activeLow = true:  level = !currentState  (inverted)
 *     activeLow = false: level =  currentState  (normal)
 * 
 * Truth table:
 *     activeLow  currentState  →  GPIO level
 *     false      false            0 (LOW)    ← OFF, active HIGH
 *     false      true             1 (HIGH)   ← ON,  active HIGH
 *     true       false            1 (HIGH)   ← OFF, active LOW
 *     true       true             0 (LOW)    ← ON,  active LOW
 */
void Relay::applyState() {
    /*
     * XOR logic: if activeLow, invert the state.
     * activeLow XOR currentState:
     *   true  XOR false = true  (HIGH) = OFF for active-low
     *   true  XOR true  = false (LOW)  = ON  for active-low
     *   false XOR false = false (LOW)  = OFF for active-high
     *   false XOR true  = true  (HIGH) = ON  for active-high
     */
    int level = activeLow ? !currentState : currentState;
    gpio_set_level(pin, level);
}


/* ============================= Public API ============================= */

/**
 * @brief Turn relay ON.
 */

/*
 * =============================================================================
 * on() - ENERGIZE THE RELAY
 * =============================================================================
 * 
 * Sets the internal state to ON and applies it to hardware.
 * For mechanical relays, you'll hear a "click".
 * For SSRs, it's silent.
 */
void Relay::on() {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized - call init() first");
        return;
    }

    currentState = true;
    applyState();
    ESP_LOGI(TAG, "GPIO %d: ON", pin);
}


/**
 * @brief Turn relay OFF.
 */

/*
 * =============================================================================
 * off() - DE-ENERGIZE THE RELAY
 * =============================================================================
 */
void Relay::off() {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized - call init() first");
        return;
    }

    currentState = false;
    applyState();
    ESP_LOGI(TAG, "GPIO %d: OFF", pin);
}


/**
 * @brief Toggle state.
 */

/*
 * =============================================================================
 * toggle() - FLIP THE STATE
 * =============================================================================
 * 
 * If ON → turn OFF.  If OFF → turn ON.
 * Useful for light switches, button-controlled outlets, etc.
 */
void Relay::toggle() {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized - call init() first");
        return;
    }

    currentState = !currentState;
    applyState();
    ESP_LOGI(TAG, "GPIO %d: %s (toggled)", pin, currentState ? "ON" : "OFF");
}


/**
 * @brief Set specific state.
 */

/*
 * =============================================================================
 * set() - SET TO A SPECIFIC STATE
 * =============================================================================
 * 
 * Useful when you have a boolean from somewhere else:
 *     relay.set(temperature > 30);  // Turn on cooling if hot
 */
void Relay::set(bool state) {
    if (state) {
        on();
    } else {
        off();
    }
}


/**
 * @brief Check if relay is ON.
 *
 * @return True if ON.
 */

/*
 * =============================================================================
 * isOn() - CHECK CURRENT STATE
 * =============================================================================
 * 
 * Returns the logical state (ON/OFF), NOT the raw GPIO level.
 * So isOn() returns true when the relay is energized, regardless
 * of whether it's an active-LOW or active-HIGH module.
 */
bool Relay::isOn() const {
    return currentState;
}
