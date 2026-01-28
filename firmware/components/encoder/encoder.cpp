/**
 * @file encoder.cpp
 * @brief Rotary encoder library implementation (ESP-IDF).
 *
 * @details
 * Uses a quadrature state machine and time-based debounce in an ISR.
 * A stable "endpoint transition" method is used to count one step per detent
 * across multiple ESP32 variants and boards.
 *
 * -----------------------------------------------------------------------------
 * Notes / Gotchas (take notes)
 * -----------------------------------------------------------------------------
 * 1) Different boards may traverse different valid state paths for the same
 *    physical detent. Therefore we count on BOTH possible "endpoint" transitions
 *    per direction.
 *
 * 2) Mechanical encoders bounce. Time-based debounce filters short pulses.
 *    If you see jitter/double steps, increase ROTATION_DEBOUNCE_US.
 *
 * 3) Button is active-low with pull-up. If it triggers randomly while rotating,
 *    that's usually wiring noise or insufficient pull-up strength.
 *    Consider external pull-ups (4.7k–10k) and/or higher debounce.
 *
 * 4) ISR must be IRAM-safe and fast. No regular logging inside ISR.
 *
 * -----------------------------------------------------------------------------
 * Quadrature states and transitions
 * -----------------------------------------------------------------------------
 * States: (CLK,DT) => 00, 01, 10, 11
 *
 * We combine old and new 2-bit states into a 4-bit transition code:
 *   sum = (lastEncoded << 2) | encoded
 *
 * Example: old=10 new=11
 *   lastEncoded<<2 = 1000
 *   encoded        = 0011
 *   sum            = 1011 = 0x0B
 *
 * Cross-board stable counting rules (1 step per detent):
 *   Clockwise (CW)  : sum == 0x0B (10→11) OR sum == 0x04 (01→00)
 *   Counter-Clockwise (CCW): sum == 0x0E (11→10) OR sum == 0x01 (00→01)
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: HOW THE ENCODER STATE MACHINE WORKS
 * =============================================================================
 * 
 * This is the "brain" of the encoder - understanding how we detect direction.
 * 
 * =============================================================================
 * THE 4 STATES
 * =============================================================================
 * 
 * At any moment, CLK and DT pins have one of 4 combinations:
 * 
 *     CLK=0, DT=0  →  State 00  (decimal 0)
 *     CLK=0, DT=1  →  State 01  (decimal 1)
 *     CLK=1, DT=0  →  State 10  (decimal 2)
 *     CLK=1, DT=1  →  State 11  (decimal 3)
 * 
 * These states form a "square" pattern:
 * 
 *         00 ←─────────────── 01
 *         ↑↓                  ↑↓
 *         ↓↑                  ↓↑
 *         10 ───────────────→ 11
 * 
 * The encoder can only move to ADJACENT states (one bit changes at a time).
 * It can't jump diagonally (00 → 11 is impossible in one step).
 * 
 * =============================================================================
 * DIRECTION FROM TRANSITIONS
 * =============================================================================
 * 
 * CLOCKWISE moves around the square one way:
 *     00 → 10 → 11 → 01 → 00 → ...
 * 
 * COUNTER-CLOCKWISE moves the other way:
 *     00 → 01 → 11 → 10 → 00 → ...
 * 
 * By watching which transition just happened, we know the direction!
 * 
 * =============================================================================
 * THE TRANSITION CODE
 * =============================================================================
 * 
 * We combine the OLD state and NEW state into a single 4-bit number:
 * 
 *     sum = (lastEncoded << 2) | encoded
 *     
 *           ┌── OLD state (bits 3-2)
 *           │
 *     sum = XX XX
 *              │
 *              └── NEW state (bits 1-0)
 * 
 * EXAMPLE: Transition from state 10 to state 11
 * 
 *     lastEncoded = 10 (binary) = 2 (decimal)
 *     encoded     = 11 (binary) = 3 (decimal)
 *     
 *     Step 1: lastEncoded << 2
 *             10 shifted left 2 positions = 1000 (binary) = 8 (decimal)
 *     
 *     Step 2: 1000 | 0011 (OR operation)
 *             1000
 *           | 0011
 *           ------
 *             1011 = 11 (decimal) = 0x0B (hexadecimal)
 *     
 *     sum = 0x0B means "we went from state 10 to state 11"
 * 
 * =============================================================================
 * TRANSITION TABLE
 * =============================================================================
 * 
 * Here are all the meaningful transitions and their codes:
 * 
 *     | From | To  | Binary | Hex  | Direction   |
 *     |------|-----|--------|------|-------------|
 *     |  00  | 10  |  0010  | 0x02 | Clockwise   |
 *     |  10  | 11  |  1011  | 0x0B | Clockwise   |  ← We count on this
 *     |  11  | 01  |  1101  | 0x0D | Clockwise   |
 *     |  01  | 00  |  0100  | 0x04 | Clockwise   |  ← Or this
 *     |------|-----|--------|------|-------------|
 *     |  00  | 01  |  0001  | 0x01 | Counter-CW  |  ← We count on this
 *     |  01  | 11  |  0111  | 0x07 | Counter-CW  |
 *     |  11  | 10  |  1110  | 0x0E | Counter-CW  |  ← Or this
 *     |  10  | 00  |  1000  | 0x08 | Counter-CW  |
 * 
 * =============================================================================
 * WHY TWO VALUES PER DIRECTION?
 * =============================================================================
 * 
 * Different ESP32 boards take different paths through the state machine!
 * 
 * Some boards (like C6 Seeed):
 *     CW click:  00 → 10 → 11 (ends at 0x0B)
 *     CCW click: 11 → 10 → 00 (ends at 0x0E)
 * 
 * Other boards (like S3 WROOM):
 *     CW click:  11 → 01 → 00 (ends at 0x04)
 *     CCW click: 00 → 01 → 11 (ends at 0x01)
 * 
 * By checking for BOTH possible endpoints, we work on ALL boards:
 * 
 *     if (sum == 0x0B || sum == 0x04) → Clockwise
 *     if (sum == 0x0E || sum == 0x01) → Counter-clockwise
 * 
 * Each click only hits ONE of these values, so we don't double-count!
 * 
 * =============================================================================
 */

#include "encoder.h"

#include <esp_err.h>
#include <esp_log.h>

/*
 * -----------------------------------------------------------------------------
 * LOGGING TAG
 * -----------------------------------------------------------------------------
 * 
 * This string appears in log messages to identify this module.
 * 
 * Example output:
 *     I (1234) RotaryEncoder: Encoder initialized successfully
 *       │      └─────────────── This is the TAG
 *       └── Time in milliseconds since boot
 * 
 * 'static' means this variable is only visible in this file.
 * 'const' means the value cannot be changed.
 */
static const char* TAG = "RotaryEncoder";


/**
 * @brief Construct a RotaryEncoder instance.
 * @param clk GPIO for encoder channel A (CLK).
 * @param dt  GPIO for encoder channel B (DT).
 * @param sw  GPIO for push button (SW).
 *
 * @note
 * Does not configure GPIO hardware. Call init().
 */

/*
 * =============================================================================
 * CONSTRUCTOR IMPLEMENTATION
 * =============================================================================
 * 
 * WHAT IS AN INITIALIZER LIST?
 * 
 * The part after the colon (:) is called an "initializer list".
 * It's a special syntax for initializing member variables.
 * 
 *     RotaryEncoder(...) : pinCLK(clk), pinDT(dt), ...
 *                        ↑
 *                        Initializer list starts here
 * 
 * WHY USE IT INSTEAD OF ASSIGNMENT IN THE BODY?
 * 
 *     // Method 1: Initializer list (PREFERRED)
 *     RotaryEncoder(...) : pinCLK(clk) { }
 *     
 *     // Method 2: Assignment in body (SLOWER)
 *     RotaryEncoder(...) {
 *         pinCLK = clk;
 *     }
 * 
 * Method 1 directly constructs variables with the right values.
 * Method 2 first constructs with defaults, then assigns new values.
 * For simple types it doesn't matter much, but it's good practice.
 * 
 * WHAT EACH INITIALIZATION DOES:
 * 
 *     pinCLK(clk)             - Store the CLK pin number
 *     pinDT(dt)               - Store the DT pin number
 *     pinSW(sw)               - Store the button pin number
 *     position(0)             - Start counting from zero
 *     lastEncoded(0)          - Initial state (will be updated in init())
 *     lastButtonState(false)  - Button starts as "not pressed"
 *     lastButtonChangeTime(0) - No previous button event
 *     lastRotationTime(0)     - No previous rotation
 */
RotaryEncoder::RotaryEncoder(gpio_num_t clk, gpio_num_t dt, gpio_num_t sw)
    : pinCLK(clk),
      pinDT(dt),
      pinSW(sw),
      position(0),
      lastEncoded(0),
      lastButtonState(false),
      lastButtonChangeTime(0),
      lastRotationTime(0) 
{
    /*
     * Constructor body is empty because all initialization
     * is done in the initializer list above.
     * 
     * The actual hardware setup happens in init(), not here.
     * This is a common pattern called "two-phase initialization":
     *     Phase 1: Constructor (just store configuration)
     *     Phase 2: init() (actually set up hardware)
     * 
     * This allows creating the object before the hardware is ready,
     * and gives explicit control over when initialization happens.
     */
}


/**
 * @brief Destructor. Removes ISR handlers from GPIO pins.
 *
 * @warning
 * If the ISR handlers remain attached after object destruction, interrupts
 * could call into invalid memory and crash.
 */

/*
 * =============================================================================
 * DESTRUCTOR IMPLEMENTATION
 * =============================================================================
 * 
 * WHY DO WE NEED TO REMOVE HANDLERS?
 * 
 * When we called init(), we told ESP32:
 *     "When pinCLK changes, call isrHandler with 'this' pointer"
 * 
 * If this encoder object is destroyed (deleted, goes out of scope), but we
 * don't remove that instruction, the hardware will STILL call isrHandler
 * with the old 'this' pointer... but that memory is now invalid!
 * 
 * This causes a CRASH (accessing freed memory).
 * 
 * The destructor removes the handlers so interrupts stop calling our function.
 */
RotaryEncoder::~RotaryEncoder() {
    /*
     * gpio_isr_handler_remove(pin) tells ESP32:
     * "Stop calling any interrupt handler for this pin."
     * 
     * We remove handlers for both CLK and DT.
     */
    gpio_isr_handler_remove(pinCLK);
    gpio_isr_handler_remove(pinDT);
}


/**
 * @brief Initialize GPIO configuration and attach interrupts.
 *
 * @details
 * - CLK/DT: input + pull-up + interrupt on any edge
 * - SW: input + pull-up, polled (no interrupt)
 * - Reads initial quadrature state
 * - Installs ISR service if needed
 * - Attaches ISR handler to CLK and DT
 *
 * @note
 * gpio_install_isr_service() is global; ESP_ERR_INVALID_STATE means it was already installed.
 */

/*
 * =============================================================================
 * INITIALIZATION IMPLEMENTATION
 * =============================================================================
 */
void RotaryEncoder::init() {
    /*
     * -------------------------------------------------------------------------
     * LOG STARTUP MESSAGE
     * -------------------------------------------------------------------------
     * 
     * ESP_LOGI = "ESP Log Info" - prints an informational message
     * TAG = "RotaryEncoder" (defined at top of file)
     * %d = format specifier for integers
     */
    ESP_LOGI(TAG, "Initializing rotary encoder on CLK=%d, DT=%d, SW=%d",
             pinCLK, pinDT, pinSW);


    /*
     * -------------------------------------------------------------------------
     * CONFIGURE CLK AND DT PINS (ROTATION)
     * -------------------------------------------------------------------------
     * 
     * gpio_config_t is a structure that holds all settings for GPIO pins.
     * We fill it out, then pass it to gpio_config() to apply.
     * 
     * The {} initializes all fields to zero/false (safe defaults).
     */
    gpio_config_t rot_conf{};
    
    /*
     * pin_bit_mask: Which pins to configure (as a bitmask)
     * 
     * Each bit represents a GPIO pin:
     *     Bit 0 = GPIO0, Bit 1 = GPIO1, ... Bit 18 = GPIO18, etc.
     * 
     * (1ULL << pinCLK) creates a number with a single 1 at position pinCLK:
     *     If pinCLK = 18:  1ULL << 18 = 0b000...00100...0 (bit 18 is set)
     * 
     * ULL = "unsigned long long" (64-bit) - needed for pins > 31
     * 
     * The | operator combines them:
     *     (1ULL << 18) | (1ULL << 19) = bits 18 AND 19 are set
     */
    rot_conf.pin_bit_mask = (1ULL << pinCLK) | (1ULL << pinDT);
    
    /*
     * mode: Input or output?
     * GPIO_MODE_INPUT = We READ from these pins (encoder sends TO us)
     */
    rot_conf.mode = GPIO_MODE_INPUT;
    
    /*
     * pull_up_en: Enable internal pull-up resistor
     * 
     * Encoder outputs are "open drain" - they can pull LOW but not drive HIGH.
     * Without pull-up, the pin would "float" at an undefined voltage.
     * Pull-up keeps pin HIGH when encoder switch is open.
     */
    rot_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    
    /*
     * pull_down_en: Disable pull-down (we're using pull-up)
     * Having both would create a voltage divider (bad).
     */
    rot_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    
    /*
     * intr_type: When should interrupts fire?
     * 
     * GPIO_INTR_ANYEDGE = Fire on BOTH:
     *     - Rising edge (LOW → HIGH)
     *     - Falling edge (HIGH → LOW)
     * 
     * We need both because the encoder generates both types.
     */
    rot_conf.intr_type = GPIO_INTR_ANYEDGE;
    
    /*
     * Apply the configuration to the pins.
     */
    gpio_config(&rot_conf);


    /*
     * -------------------------------------------------------------------------
     * CONFIGURE BUTTON PIN (SW)
     * -------------------------------------------------------------------------
     * 
     * Same settings, but NO interrupt.
     * We poll the button in the main loop (it changes slowly - human speed).
     */
    gpio_config_t btn_conf{};
    btn_conf.pin_bit_mask = (1ULL << pinSW);
    btn_conf.mode = GPIO_MODE_INPUT;
    btn_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    btn_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    btn_conf.intr_type = GPIO_INTR_DISABLE;  // No interrupt for button
    gpio_config(&btn_conf);


    /*
     * -------------------------------------------------------------------------
     * READ INITIAL STATE
     * -------------------------------------------------------------------------
     * 
     * Before watching for CHANGES, we need to know the CURRENT state.
     * This prevents a false transition on the first real change.
     */
    
    /*
     * gpio_get_level() returns 0 or 1 for the pin's current voltage.
     */
    uint8_t clk = gpio_get_level(pinCLK);
    uint8_t dt  = gpio_get_level(pinDT);
    
    /*
     * Combine into a 2-bit state value:
     * 
     *     (clk << 1) shifts clk left by 1 bit
     *     | dt       combines with dt using OR
     *     
     *     Example: clk=1, dt=0
     *         (1 << 1) | 0 = 10 | 00 = 10 (binary) = 2 (decimal)
     */
    lastEncoded = (clk << 1) | dt;

    /*
     * Read initial button state.
     * Button is active LOW: pressed = pin reads 0
     */
    lastButtonState = (gpio_get_level(pinSW) == 0);

    /*
     * Initialize timing for debounce.
     * esp_timer_get_time() returns microseconds since boot.
     */
    uint64_t now = esp_timer_get_time();
    lastButtonChangeTime = now;
    lastRotationTime = now;


    /*
     * -------------------------------------------------------------------------
     * INSTALL ISR SERVICE
     * -------------------------------------------------------------------------
     * 
     * ESP-IDF has a shared "ISR service" that manages all GPIO interrupts.
     * It's like a post office that receives all mail and routes it.
     * 
     * It must be installed ONCE before adding any handlers.
     * 
     * The 0 parameter means "use default settings".
     */
    esp_err_t err = gpio_install_isr_service(0);
    
    /*
     * Check for errors, but IGNORE "already installed" error.
     * 
     * ESP_ERR_INVALID_STATE means the service was already installed
     * (maybe by another part of your code). That's fine - we just need
     * it to exist, doesn't matter who installed it.
     */
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(err));
        return;  // Can't continue without ISR service
    }


    /*
     * -------------------------------------------------------------------------
     * ATTACH INTERRUPT HANDLERS
     * -------------------------------------------------------------------------
     * 
     * gpio_isr_handler_add(pin, handler, argument)
     * 
     * This tells ESP32:
     * "When 'pin' changes, call 'handler' and pass 'argument' to it."
     * 
     * We pass 'this' as the argument so the handler knows which
     * encoder object to work with.
     * 
     * 'this' is a pointer to the current encoder object.
     * Inside isrHandler, we cast it back to RotaryEncoder*.
     */
    gpio_isr_handler_add(pinCLK, isrHandler, this);
    gpio_isr_handler_add(pinDT,  isrHandler, this);

    ESP_LOGI(TAG, "Encoder initialized successfully");
}


/**
 * @brief ISR for quadrature transitions.
 *
 * @param arg Pointer passed during ISR registration (we pass `this`).
 *
 * @details
 * 1) Read CLK/DT quickly
 * 2) Time-based debounce to filter bounce
 * 3) Build transition code: sum = (old<<2) | new
 * 4) Update position using stable endpoint transitions:
 *    - CW : 0x0B or 0x04
 *    - CCW: 0x0E or 0x01
 *
 * @warning
 * ISR context: keep it fast and IRAM-safe. No normal logging.
 */

/*
 * =============================================================================
 * ISR HANDLER IMPLEMENTATION
 * =============================================================================
 * 
 * This function is called AUTOMATICALLY by hardware when CLK or DT changes.
 * 
 * IRAM_ATTR:
 *     Tells compiler to put this function in IRAM (Internal RAM), not Flash.
 *     
 *     WHY?
 *     - Flash memory can be BUSY (during WiFi, writes, etc.)
 *     - ISRs must respond INSTANTLY, can't wait for Flash
 *     - IRAM is always immediately accessible
 *     
 *     Without this, the program CRASHES if an interrupt happens
 *     while Flash is busy (very common when WiFi is enabled).
 * 
 * RULES FOR ISRs:
 *     1. Keep it SHORT - don't do heavy processing
 *     2. No regular logging - use ESP_EARLY_LOGI if absolutely needed
 *     3. No memory allocation (no new, malloc, etc.)
 *     4. No floating point math (on some processors)
 *     5. Use 'volatile' for shared variables
 */
void IRAM_ATTR RotaryEncoder::isrHandler(void* arg) {
    
    /*
     * -------------------------------------------------------------------------
     * RECOVER THE ENCODER POINTER
     * -------------------------------------------------------------------------
     * 
     * 'arg' contains the pointer we passed when registering (it was 'this').
     * It's a void* (generic pointer), so we cast it to RotaryEncoder*.
     * 
     * static_cast<Type>(value) is the C++ way to convert types.
     * It's safer than C-style casts because the compiler checks validity.
     */
    RotaryEncoder* encoder = static_cast<RotaryEncoder*>(arg);


    /*
     * -------------------------------------------------------------------------
     * READ PIN STATES
     * -------------------------------------------------------------------------
     * 
     * Read both pins as close together as possible for a consistent snapshot.
     * 
     * encoder->pinCLK is the same as (*encoder).pinCLK
     * The -> operator is shorthand for "dereference and access member".
     */
    uint8_t clk = gpio_get_level(encoder->pinCLK);
    uint8_t dt  = gpio_get_level(encoder->pinDT);
    
    /*
     * Combine into 2-bit state: (CLK << 1) | DT
     * Results in: 00, 01, 10, or 11
     */
    uint8_t encoded = (clk << 1) | dt;


    /*
     * -------------------------------------------------------------------------
     * DEBOUNCE
     * -------------------------------------------------------------------------
     * 
     * If less than ROTATION_DEBOUNCE_US microseconds since the last
     * valid transition, this is probably bounce - ignore it.
     * 
     * esp_timer_get_time() returns microseconds since boot.
     * It's one of the few safe functions to call from an ISR.
     */
    uint64_t now = esp_timer_get_time();
    if (now - encoder->lastRotationTime < ROTATION_DEBOUNCE_US) {
        return;  // Too soon, probably bounce - ignore
    }
    encoder->lastRotationTime = now;


    /*
     * -------------------------------------------------------------------------
     * BUILD TRANSITION CODE
     * -------------------------------------------------------------------------
     * 
     * Combine old state (lastEncoded) and new state (encoded) into 4 bits:
     * 
     *     sum = (lastEncoded << 2) | encoded
     *     
     *     OLD in bits 3-2, NEW in bits 1-0
     *     
     *     Example: old=10 (2), new=11 (3)
     *         10 << 2 = 1000 (8)
     *         1000 | 0011 = 1011 = 0x0B
     */
    uint8_t sum = (encoder->lastEncoded << 2) | encoded;


    /*
     * -------------------------------------------------------------------------
     * DIRECTION DETECTION AND COUNTING
     * -------------------------------------------------------------------------
     * 
     * Check if this transition indicates clockwise or counter-clockwise.
     * 
     * We check TWO values per direction because different boards
     * take different paths through the state machine:
     * 
     *     CLOCKWISE:
     *         0x0B = transition 10→11 (some boards)
     *         0x04 = transition 01→00 (other boards)
     *     
     *     COUNTER-CLOCKWISE:
     *         0x0E = transition 11→10 (some boards)
     *         0x01 = transition 00→01 (other boards)
     * 
     * Each click hits only ONE of these, so no double-counting.
     * 
     * NOTE: We use "position = position + 1" instead of "position++"
     * to avoid a compiler warning about volatile increment being deprecated.
     */
    
    // Clockwise endpoint transitions
    if (sum == 0x0B || sum == 0x04) {
        encoder->position = encoder->position + 1;
    }
    // Counter-clockwise endpoint transitions
    else if (sum == 0x0E || sum == 0x01) {
        encoder->position = encoder->position - 1;
    }
    
    /*
     * Other sum values are either:
     *     - Invalid (shouldn't happen)
     *     - Same state (no movement)
     *     - Intermediate (not at a detent)
     * 
     * We ignore them - no position change.
     */


    /*
     * -------------------------------------------------------------------------
     * SAVE STATE FOR NEXT TIME
     * -------------------------------------------------------------------------
     * 
     * The current state becomes the "old" state for the next transition.
     */
    encoder->lastEncoded = encoded;
}


/**
 * @brief Get current position.
 * @return Current position count.
 */

/*
 * =============================================================================
 * getPosition() IMPLEMENTATION
 * =============================================================================
 * 
 * Simply returns the position value.
 * 
 * Because 'position' is volatile, this ALWAYS reads from memory,
 * not from a cached/optimized value. This ensures we see the
 * latest value set by the ISR.
 * 
 * 'const' at the end means this function promises not to modify
 * any member variables. It's a safety feature.
 */
int32_t RotaryEncoder::getPosition() const {
    return position;
}


/**
 * @brief Reset position to zero.
 */

/*
 * =============================================================================
 * resetPosition() IMPLEMENTATION
 * =============================================================================
 * 
 * Sets position back to zero.
 * Typically called when user presses the button to "reset".
 */
void RotaryEncoder::resetPosition() {
    position = 0;
}


/**
 * @brief Set position to a specific value.
 * @param pos New position value.
 */

/*
 * =============================================================================
 * setPosition() IMPLEMENTATION
 * =============================================================================
 * 
 * Sets position to any specific value.
 * Useful for:
 *     - Starting from a non-zero value
 *     - Restoring a saved position
 *     - Implementing wrap-around limits
 */
void RotaryEncoder::setPosition(int32_t pos) {
    position = pos;
}


/**
 * @brief Read raw button state (active-low).
 * @return true if pressed now, false otherwise.
 */

/*
 * =============================================================================
 * isButtonPressed() IMPLEMENTATION
 * =============================================================================
 * 
 * Returns the CURRENT state of the button (no debouncing).
 * 
 * The button is "active LOW":
 *     - Pull-up resistor keeps pin HIGH (1) when button is open
 *     - Pressing button connects pin to GND, reads LOW (0)
 *     
 * So: pressed = (level == 0)
 */
bool RotaryEncoder::isButtonPressed() const {
    return gpio_get_level(pinSW) == 0;
}


/**
 * @brief Debounced edge detect for button press.
 * @return true once per press (released->pressed), false otherwise.
 *
 * @details
 * - Debounce window: BUTTON_DEBOUNCE_US
 * - Edge detect: current pressed AND last not pressed
 */

/*
 * =============================================================================
 * wasButtonPressed() IMPLEMENTATION
 * =============================================================================
 * 
 * Returns TRUE exactly once when button is pressed.
 * Includes debouncing to filter mechanical bounce.
 * Includes edge detection to trigger only on the press, not while held.
 * 
 * EDGE DETECTION LOGIC:
 * 
 *     currentState    = Is button pressed RIGHT NOW?
 *     lastButtonState = Was button pressed LAST TIME we checked?
 *     
 *     "Just pressed" = currentState==true AND lastButtonState==false
 *     
 *     | current | last  | Meaning                    | Return |
 *     |---------|-------|----------------------------|--------|
 *     | false   | false | Still released             | false  |
 *     | false   | true  | Just released              | false  |
 *     | true    | false | JUST PRESSED ←             | TRUE   |
 *     | true    | true  | Still held down            | false  |
 */
bool RotaryEncoder::wasButtonPressed() {
    /*
     * Get current button state (true = pressed)
     */
    bool currentState = isButtonPressed();
    
    /*
     * Get current time for debounce comparison
     */
    uint64_t now = esp_timer_get_time();

    /*
     * DEBOUNCE:
     * If the button changed recently (within 50ms), ignore this check.
     * This filters out the rapid on/off bouncing of mechanical contacts.
     */
    if (now - lastButtonChangeTime < BUTTON_DEBOUNCE_US) {
        return false;  // Still in debounce window, ignore
    }

    /*
     * EDGE DETECTION:
     * "Just pressed" = pressed now AND wasn't pressed before
     * 
     * currentState && !lastButtonState
     *     - currentState: button is pressed now (true)
     *     - !lastButtonState: button was NOT pressed before (true)
     *     - Both true = button was JUST pressed
     */
    bool pressed = (currentState && !lastButtonState);

    /*
     * UPDATE STATE TRACKING:
     * If state changed (either pressed or released), remember the new state
     * and when it changed.
     */
    if (currentState != lastButtonState) {
        lastButtonState = currentState;
        lastButtonChangeTime = now;
    }

    /*
     * Return whether a NEW press just happened
     */
    return pressed;
}
