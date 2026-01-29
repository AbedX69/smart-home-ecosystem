/**
 * @file button.cpp
 * @brief Button driver implementation (ESP-IDF).
 *
 * @details
 * Implements polling-based button reading with software debouncing.
 * Uses a simple state machine for edge detection.
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: HOW THIS IMPLEMENTATION WORKS
 * =============================================================================
 * 
 * Unlike the rotary encoder (which uses interrupts), buttons use POLLING:
 * 
 *     POLLING = Check the button state repeatedly in a loop
 *     
 *     while(1) {
 *         button.update();      // Check button state
 *         if (button.wasPressed()) {
 *             doSomething();
 *         }
 *         delay(10);            // Wait a bit, then check again
 *     }
 * 
 * =============================================================================
 * WHY POLLING FOR BUTTONS (NOT INTERRUPTS)?
 * =============================================================================
 * 
 * ENCODER (uses interrupts):
 *     - Can spin VERY fast (hundreds of transitions per second)
 *     - Miss one transition = wrong count
 *     - MUST use interrupts to catch everything
 * 
 * BUTTON (uses polling):
 *     - Human presses are SLOW (max ~10 presses per second)
 *     - Even 50ms polling catches every press easily
 *     - Simpler code, no ISR complications
 *     - Debouncing is easier with polling
 * 
 * =============================================================================
 * THE DEBOUNCE STATE MACHINE
 * =============================================================================
 * 
 * We track several states:
 * 
 *     lastRawState    = What the GPIO read LAST time
 *     currentState    = The DEBOUNCED state (what we report)
 *     lastState       = Previous debounced state (for edge detection)
 *     lastChangeTime  = When did the raw reading change?
 * 
 * DEBOUNCE LOGIC:
 * 
 *     1. Read GPIO (raw reading)
 *     2. If raw reading changed from lastRawState:
 *        - Record the time (lastChangeTime)
 *        - Update lastRawState
 *     3. If raw reading has been STABLE for debounceTime:
 *        - Update currentState to match raw reading
 *        - Set edge flags if state actually changed
 * 
 * TIMELINE EXAMPLE:
 * 
 *     Time:    0ms    10ms   20ms   30ms   40ms   50ms   60ms
 *     Raw:     HIGH   LOW    HIGH   LOW    LOW    LOW    LOW
 *                     ↑      ↑      ↑
 *                   bounce bounce  stable now
 *     
 *     Debounced: HIGH  HIGH   HIGH   HIGH   HIGH   LOW    LOW
 *                                                  ↑
 *                                          Finally changes after 50ms stable
 * 
 * =============================================================================
 */

#include "button.h"
#include <esp_log.h>

/*
 * Logging tag for ESP_LOGI, ESP_LOGE, etc.
 */
static const char* TAG = "Button";


/**
 * @brief Construct a Button instance.
 *
 * @param pin GPIO pin number.
 * @param debounceMs Debounce time in milliseconds.
 */

/*
 * =============================================================================
 * CONSTRUCTOR
 * =============================================================================
 * 
 * Just stores configuration. Hardware setup happens in init().
 * 
 * The debounce time is converted from milliseconds to microseconds
 * because esp_timer_get_time() returns microseconds.
 * 
 *     1 millisecond = 1,000 microseconds
 *     50ms = 50,000 microseconds
 */
Button::Button(gpio_num_t pin, uint32_t debounceMs)
    : pin(pin),
      debounceTimeUs(debounceMs * 1000),    // Convert ms to µs
      currentState(false),
      lastState(false),
      lastRawState(false),
      lastChangeTime(0),
      pressStartTime(0),
      pressedFlag(false),
      releasedFlag(false)
{
    // Nothing else to do - init() sets up hardware
}


/**
 * @brief Destructor.
 */

/*
 * =============================================================================
 * DESTRUCTOR
 * =============================================================================
 * 
 * Unlike the encoder, we don't have any interrupt handlers to remove.
 * The GPIO will just be left as-is (still configured as input).
 * 
 * If you wanted to be thorough, you could reset the GPIO here,
 * but it's usually not necessary.
 */
Button::~Button() {
    // Nothing to clean up for a simple polled button
}


/**
 * @brief Initialize GPIO.
 */

/*
 * =============================================================================
 * INITIALIZATION
 * =============================================================================
 * 
 * Configures the GPIO pin as:
 *     - Input (we READ from the button, not write to it)
 *     - Pull-up enabled (keeps pin HIGH when button is open)
 *     - No interrupt (we poll instead)
 */
void Button::init() {
    ESP_LOGI(TAG, "Initializing button on GPIO %d", pin);

    /*
     * Configure GPIO using the gpio_config_t structure.
     * Same approach as the encoder, but simpler (no interrupts).
     */
    gpio_config_t io_conf = {};
    
    /*
     * pin_bit_mask: Which pin(s) to configure.
     * (1ULL << pin) creates a bitmask with just our pin set.
     */
    io_conf.pin_bit_mask = (1ULL << pin);
    
    /*
     * mode: Input (we read the button state).
     */
    io_conf.mode = GPIO_MODE_INPUT;
    
    /*
     * pull_up_en: Enable internal pull-up resistor.
     * This keeps the pin HIGH when the button is not pressed.
     */
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    
    /*
     * pull_down_en: Disable pull-down (we're using pull-up).
     */
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    
    /*
     * intr_type: No interrupts (we poll the button).
     */
    io_conf.intr_type = GPIO_INTR_DISABLE;
    
    /*
     * Apply the configuration.
     */
    gpio_config(&io_conf);

    /*
     * Read initial button state.
     * Button is active LOW: GPIO reads 0 when pressed.
     * So: pressed = (level == 0)
     */
    bool initialState = (gpio_get_level(pin) == 0);
    currentState = initialState;
    lastState = initialState;
    lastRawState = initialState;

    /*
     * Initialize timing.
     */
    uint64_t now = esp_timer_get_time();
    lastChangeTime = now;
    if (initialState) {
        pressStartTime = now;
    }

    ESP_LOGI(TAG, "Button initialized (initial state: %s)", 
             initialState ? "PRESSED" : "released");
}


/**
 * @brief Update button state (call regularly).
 */

/*
 * =============================================================================
 * UPDATE - THE MAIN DEBOUNCE AND EDGE DETECTION LOGIC
 * =============================================================================
 * 
 * This function should be called frequently in your main loop.
 * It reads the GPIO, applies debouncing, and detects edges.
 * 
 * STEP BY STEP:
 * 
 * 1. Read raw GPIO state
 * 2. If raw state changed, record the time (start debounce timer)
 * 3. If raw state has been stable for debounceTime, update debounced state
 * 4. If debounced state changed, set edge flags (pressed/released)
 */
void Button::update() {
    /*
     * Get current time in microseconds.
     */
    uint64_t now = esp_timer_get_time();

    /*
     * Read raw button state.
     * Active LOW: GPIO reads 0 when pressed.
     */
    bool rawState = (gpio_get_level(pin) == 0);

    /*
     * -------------------------------------------------------------------------
     * STEP 1: Check if raw reading changed
     * -------------------------------------------------------------------------
     * 
     * If the raw reading is different from last time, the button might be
     * changing state (or it might be bouncing). Start the debounce timer.
     */
    if (rawState != lastRawState) {
        lastChangeTime = now;       // Reset debounce timer
        lastRawState = rawState;    // Remember this reading
    }

    /*
     * -------------------------------------------------------------------------
     * STEP 2: Check if debounce time has passed
     * -------------------------------------------------------------------------
     * 
     * Only update the debounced state if the raw reading has been STABLE
     * (unchanged) for at least debounceTimeUs microseconds.
     */
    if ((now - lastChangeTime) >= debounceTimeUs) {
        /*
         * Raw state has been stable long enough.
         * Check if the debounced state actually changed.
         */
        if (rawState != currentState) {
            /*
             * -----------------------------------------------------------------
             * STEP 3: State actually changed - update and set flags
             * -----------------------------------------------------------------
             */
            
            // Save previous state for edge detection
            lastState = currentState;
            
            // Update to new state
            currentState = rawState;

            // Set edge flags
            if (currentState && !lastState) {
                /*
                 * Transition: released → pressed
                 * Button was just pressed!
                 */
                pressedFlag = true;
                pressStartTime = now;
            }
            else if (!currentState && lastState) {
                /*
                 * Transition: pressed → released
                 * Button was just released!
                 */
                releasedFlag = true;
            }
        }
    }
}


/**
 * @brief Check if button is currently pressed.
 *
 * @return true if pressed.
 */

/*
 * =============================================================================
 * isPressed() - CURRENT STATE
 * =============================================================================
 * 
 * Returns the DEBOUNCED state of the button.
 * Returns true continuously while the button is held down.
 * 
 * Use this for:
 *     - "Is the button being held right now?"
 *     - Continuous actions while holding (like scrolling)
 */
bool Button::isPressed() const {
    return currentState;
}


/**
 * @brief Check if button was just pressed.
 *
 * @return true once per press.
 */

/*
 * =============================================================================
 * wasPressed() - EDGE DETECTION (PRESS)
 * =============================================================================
 * 
 * Returns true ONCE when the button is pressed.
 * The flag is cleared after reading, so it only returns true once per press.
 * 
 * Use this for:
 *     - "Did the user just press the button?"
 *     - Single actions on button press (toggle, select, etc.)
 */
bool Button::wasPressed() {
    if (pressedFlag) {
        pressedFlag = false;    // Clear the flag
        return true;
    }
    return false;
}


/**
 * @brief Check if button was just released.
 *
 * @return true once per release.
 */

/*
 * =============================================================================
 * wasReleased() - EDGE DETECTION (RELEASE)
 * =============================================================================
 * 
 * Returns true ONCE when the button is released.
 * The flag is cleared after reading, so it only returns true once per release.
 * 
 * Use this for:
 *     - "Did the user just let go of the button?"
 *     - Actions that should happen when releasing (like stopping movement)
 */
bool Button::wasReleased() {
    if (releasedFlag) {
        releasedFlag = false;   // Clear the flag
        return true;
    }
    return false;
}


/**
 * @brief Get how long button has been held.
 *
 * @return Duration in milliseconds, or 0 if not pressed.
 */

/*
 * =============================================================================
 * getPressedDuration() - HOW LONG HAS IT BEEN HELD?
 * =============================================================================
 * 
 * Returns how many milliseconds the button has been held down.
 * Returns 0 if the button is not currently pressed.
 * 
 * Use this for:
 *     - Long-press detection ("hold for 2 seconds to reset")
 *     - Different actions for tap vs hold
 * 
 * EXAMPLE:
 *     if (button.isPressed()) {
 *         uint32_t duration = button.getPressedDuration();
 *         if (duration > 2000) {
 *             // Held for more than 2 seconds
 *             doLongPressAction();
 *         }
 *     }
 *     if (button.wasReleased()) {
 *         uint32_t duration = button.getPressedDuration();  // Will be 0 now!
 *         // Better: track duration BEFORE release
 *     }
 */
uint32_t Button::getPressedDuration() const {
    if (!currentState) {
        return 0;   // Not pressed, no duration
    }
    
    /*
     * Calculate duration in milliseconds.
     * esp_timer_get_time() returns microseconds.
     * Divide by 1000 to get milliseconds.
     */
    uint64_t now = esp_timer_get_time();
    uint64_t durationUs = now - pressStartTime;
    return (uint32_t)(durationUs / 1000);
}
