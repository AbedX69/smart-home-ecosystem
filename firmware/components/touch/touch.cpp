/**
 * @file touch.cpp
 * @brief Capacitive touch sensor implementation (ESP-IDF).
 *
 * @details
 * Simple polling-based driver for TTP223 and similar touch modules.
 * No debouncing needed - the touch IC handles filtering internally.
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: IMPLEMENTATION NOTES
 * =============================================================================
 * 
 * This is SIMPLER than the button driver because:
 * 
 * 1. NO DEBOUNCING NEEDED
 *    The TTP223 chip has built-in filtering and outputs clean signals.
 * 
 * 2. NO PULL-UP RESISTOR
 *    The module has an active output (it drives HIGH or LOW itself).
 *    It's not an open-drain that needs external pull-up.
 * 
 * 3. CONFIGURABLE POLARITY
 *    Some modules output HIGH when touched, others output LOW.
 *    The 'activeHigh' parameter handles both cases.
 * 
 * =============================================================================
 */

#include "touch.h"
#include <esp_log.h>


static const char* TAG = "TouchSensor";


/**
 * @brief Constructor.
 */

/*
 * =============================================================================
 * CONSTRUCTOR
 * =============================================================================
 * 
 * Stores configuration. Hardware setup happens in init().
 * 
 * activeHigh:
 *     true  = Module outputs HIGH when touched (most common)
 *     false = Module outputs LOW when touched
 */
TouchSensor::TouchSensor(gpio_num_t pin, bool activeHigh)
    : pin(pin),
      activeHigh(activeHigh),
      currentState(false),
      lastState(false),
      touchStartTime(0),
      touchedFlag(false),
      releasedFlag(false)
{
    // Nothing else - init() sets up hardware
}


/**
 * @brief Destructor.
 */
TouchSensor::~TouchSensor() {
    // Nothing to clean up
}


/**
 * @brief Initialize GPIO.
 */

/*
 * =============================================================================
 * INITIALIZATION
 * =============================================================================
 * 
 * Configure GPIO as input.
 * 
 * IMPORTANT: No pull-up or pull-down!
 * The touch module has an active output that drives the line.
 * Adding a pull resistor could interfere with it.
 */
void TouchSensor::init() {
    ESP_LOGI(TAG, "Initializing touch sensor on GPIO %d (active %s)", 
             pin, activeHigh ? "HIGH" : "LOW");

    gpio_config_t io_conf = {};
    
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.mode = GPIO_MODE_INPUT;
    
    /*
     * NO pull-up or pull-down!
     * The module drives the output actively.
     */
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    
    io_conf.intr_type = GPIO_INTR_DISABLE;
    
    gpio_config(&io_conf);

    /*
     * Read initial state.
     * Convert GPIO level to touched/not-touched based on activeHigh setting.
     */
    bool level = (gpio_get_level(pin) == 1);
    currentState = (activeHigh) ? level : !level;
    lastState = currentState;

    if (currentState) {
        touchStartTime = esp_timer_get_time();
    }

    ESP_LOGI(TAG, "Touch sensor initialized (initial state: %s)", 
             currentState ? "TOUCHED" : "not touched");
}


/**
 * @brief Update touch state.
 */

/*
 * =============================================================================
 * UPDATE - READ STATE AND DETECT EDGES
 * =============================================================================
 * 
 * Much simpler than button.update() because we don't need debouncing!
 * 
 * 1. Read GPIO level
 * 2. Convert to touched/not-touched (based on activeHigh)
 * 3. If state changed, set the appropriate flag
 */
void TouchSensor::update() {
    /*
     * Read current GPIO level.
     * level = true if GPIO reads HIGH.
     */
    bool level = (gpio_get_level(pin) == 1);

    /*
     * Convert to touched state.
     * 
     * If activeHigh == true:
     *     HIGH level → touched
     *     LOW level  → not touched
     * 
     * If activeHigh == false:
     *     HIGH level → not touched
     *     LOW level  → touched
     */
    bool newState = (activeHigh) ? level : !level;

    /*
     * Check if state changed.
     */
    if (newState != currentState) {
        // Save previous state
        lastState = currentState;
        
        // Update current state
        currentState = newState;

        // Set edge flags
        if (currentState && !lastState) {
            // Just touched!
            touchedFlag = true;
            touchStartTime = esp_timer_get_time();
        }
        else if (!currentState && lastState) {
            // Just released!
            releasedFlag = true;
        }
    }
}


/**
 * @brief Check if currently touched.
 */
bool TouchSensor::isTouched() const {
    return currentState;
}


/**
 * @brief Check if just touched (edge).
 */
bool TouchSensor::wasTouched() {
    if (touchedFlag) {
        touchedFlag = false;
        return true;
    }
    return false;
}


/**
 * @brief Check if just released (edge).
 */
bool TouchSensor::wasReleased() {
    if (releasedFlag) {
        releasedFlag = false;
        return true;
    }
    return false;
}


/**
 * @brief Get touch duration.
 */
uint32_t TouchSensor::getTouchedDuration() const {
    if (!currentState) {
        return 0;
    }
    
    uint64_t now = esp_timer_get_time();
    uint64_t durationUs = now - touchStartTime;
    return (uint32_t)(durationUs / 1000);   // Convert to milliseconds
}
