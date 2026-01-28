/**
 * @file encoder.h
 * @brief Interrupt-driven rotary encoder driver for ESP32 (ESP-IDF).
 *
 * @details
 * This component reads a mechanical quadrature rotary encoder (CLK/DT) and an
 * optional push button (SW). Rotation is handled with GPIO interrupts, while
 * the button is typically polled using edge detection + debounce.
 *
 * @note
 * Electrical assumptions:
 * - Internal pull-ups are enabled for CLK/DT/SW
 * - Inputs are treated as active-low when the switch closes to GND
 *
 * @warning
 * ISR rules apply. Keep ISR fast; no logging, no malloc/new, no blocking calls.
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
 * BEGINNER'S GUIDE: ROTARY ENCODER LIBRARY FOR ESP32
 * =============================================================================
 * 
 * If you're new to embedded programming, this section explains everything
 * from the ground up. Experienced developers can skip to the class definition.
 * 
 * =============================================================================
 * WHAT IS A ROTARY ENCODER?
 * =============================================================================
 * 
 * A rotary encoder is a knob you can turn infinitely in either direction.
 * Unlike a potentiometer (which has start/end positions and outputs analog
 * voltage), an encoder outputs digital pulses.
 * 
 * Physical appearance:
 * 
 *         ┌─────────┐
 *         │    ○    │  ← Shaft you turn (can also be pushed like a button)
 *         │  ┌───┐  │
 *         │  │   │  │
 *         └──┴───┴──┘
 *            │ │ │
 *           CLK DT SW
 *            │ │ │
 *           (A)(B)(Button)
 * 
 * The encoder has 3 pins:
 *     - CLK (or A): Channel A output
 *     - DT (or B):  Channel B output  
 *     - SW:         Push button (when you press the shaft down)
 * 
 * =============================================================================
 * HOW QUADRATURE ENCODING WORKS
 * =============================================================================
 * 
 * The two output pins (CLK and DT) produce signals that are 90° out of phase.
 * This is called "quadrature encoding" and allows detecting:
 *     1. DIRECTION - which signal changes first tells you CW vs CCW
 *     2. POSITION  - count the transitions to track position
 * 
 *     Clockwise Rotation:
 *     
 *     CLK: ──┐   ┌───┐   ┌───
 *            │   │   │   │
 *            └───┘   └───┘
 *     
 *     DT:  ────┐   ┌───┐   ┌─
 *              │   │   │   │
 *              └───┘   └───┘
 *     
 *     Notice: CLK changes BEFORE DT when turning clockwise.
 *     
 *     Counter-Clockwise Rotation:
 *     
 *     CLK: ────┐   ┌───┐   ┌─
 *              │   │   │   │
 *              └───┘   └───┘
 *     
 *     DT:  ──┐   ┌───┐   ┌───
 *            │   │   │   │
 *            └───┘   └───┘
 *     
 *     Notice: DT changes BEFORE CLK when turning counter-clockwise.
 * 
 * The two pins create 4 possible states at any moment:
 * 
 *     | CLK | DT | State (binary) | Decimal |
 *     |-----|-----|----------------|---------|
 *     |  0  |  0  |      00        |    0    |
 *     |  0  |  1  |      01        |    1    |
 *     |  1  |  0  |      10        |    2    |
 *     |  1  |  1  |      11        |    3    |
 * 
 * By tracking TRANSITIONS between states, we detect direction.
 * 
 * =============================================================================
 * WHY INTERRUPTS INSTEAD OF POLLING?
 * =============================================================================
 * 
 * There are two ways to detect encoder changes:
 * 
 * POLLING (checking repeatedly in a loop):
 * 
 *     while(1) {
 *         int clk = read_pin(CLK);  // Check the pin
 *         int dt = read_pin(DT);    // Check the pin
 *         // ... process ...
 *         delay(10);                // Wait, then check again
 *     }
 * 
 *     PROBLEM: If you turn the encoder during the delay(), you miss it!
 *     Fast spinning can cause missed steps or wrong direction.
 * 
 * INTERRUPTS (hardware notifies you automatically):
 * 
 *     void on_pin_change() {        // Called automatically by hardware!
 *         // Process the change immediately
 *     }
 *     
 *     while(1) {
 *         // Do other stuff, encoder is handled automatically
 *         delay(100);  // Can be long, won't miss anything!
 *     }
 * 
 *     ADVANTAGE: Never miss a transition, even during fast rotation.
 *     The main loop is free to do other work.
 * 
 * =============================================================================
 * ACTIVE LOW - WHAT DOES IT MEAN?
 * =============================================================================
 * 
 * The button is "active LOW":
 *     - When NOT pressed: Pin reads HIGH (1) because of pull-up resistor
 *     - When pressed: Pin reads LOW (0) because button connects to ground
 * 
 *     NOT PRESSED:              PRESSED:
 *     
 *         3.3V                      3.3V
 *          │                         │
 *          ├── Pull-up resistor      ├── Pull-up resistor
 *          │                         │
 *     Pin ─┤                    Pin ─┤
 *          │                         │
 *          ○ ← Button OPEN          ─┴─ ← Button CLOSED
 *          │                         │
 *         GND                       GND
 *     
 *     Pin reads: HIGH (1)       Pin reads: LOW (0)
 * 
 * So in code: pressed = (pin_value == 0)
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "encoder.h"
 *     
 *     void app_main(void) {
 *         // Create encoder on pins CLK=18, DT=19, Button=5
 *         RotaryEncoder encoder(GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_5);
 *         
 *         // Initialize (sets up GPIOs and interrupts)
 *         encoder.init();
 *         
 *         while(1) {
 *             // Get current position (updated automatically by interrupts)
 *             int32_t pos = encoder.getPosition();
 *             printf("Position: %ld\n", pos);
 *             
 *             // Check for button press
 *             if (encoder.wasButtonPressed()) {
 *                 printf("Button pressed!\n");
 *                 encoder.resetPosition();
 *             }
 *             
 *             vTaskDelay(pdMS_TO_TICKS(50));
 *         }
 *     }
 * 
 * =============================================================================
 */

#pragma once

/*
 * -----------------------------------------------------------------------------
 * INCLUDES
 * -----------------------------------------------------------------------------
 * 
 * What each header provides:
 * 
 * <driver/gpio.h>
 *     ESP-IDF GPIO functions: gpio_config(), gpio_get_level(), 
 *     gpio_isr_handler_add(), etc.
 *     Also defines gpio_num_t enum for pin numbers.
 * 
 * <esp_timer.h>
 *     High-resolution timer: esp_timer_get_time() returns microseconds
 *     since boot. Used for debouncing.
 * 
 * <stdint.h>
 *     Fixed-width integer types: int32_t, uint8_t, uint64_t, etc.
 *     These guarantee exact sizes across different platforms.
 */
#include <driver/gpio.h>
#include <esp_timer.h>
#include <stdint.h>


/**
 * @class RotaryEncoder
 * @brief Rotary encoder driver with interrupt-driven rotation and debounced button.
 *
 * @details
 * Rotation is updated inside a GPIO ISR attached to both CLK and DT pins.
 * The ISR tracks direction by combining previous and current quadrature states.
 *
 * Button handling:
 * - isButtonPressed() returns the raw (active-low) state.
 * - wasButtonPressed() returns true once per press (edge detect + debounce).
 */

/*
 * =============================================================================
 * CLASS EXPLANATION FOR BEGINNERS
 * =============================================================================
 * 
 * A CLASS is like a blueprint for creating objects. It bundles together:
 *     - DATA (variables) that describe the object's state
 *     - FUNCTIONS (methods) that operate on that data
 * 
 * Think of it like a blueprint for a "TV remote":
 *     - Data: current channel, volume level, power state
 *     - Functions: changeChannel(), adjustVolume(), powerOn()
 * 
 * Our RotaryEncoder class:
 *     - Data: pin numbers, position count, button state, timing info
 *     - Functions: init(), getPosition(), wasButtonPressed(), etc.
 * 
 * PUBLIC vs PRIVATE:
 *     - PUBLIC: Anyone can use these (the "interface")
 *     - PRIVATE: Only the class itself can use these (internal details)
 * 
 * =============================================================================
 */
class RotaryEncoder {

/*
 * =============================================================================
 * PUBLIC SECTION
 * =============================================================================
 * 
 * These are the functions and data that USERS of the class can access.
 * This is the "interface" - how you interact with the encoder.
 */
public:

    /**
     * @brief Construct a new RotaryEncoder instance.
     *
     * @param clk GPIO pin for encoder channel A (CLK).
     * @param dt  GPIO pin for encoder channel B (DT).
     * @param sw  GPIO pin for push button (SW).
     *
     * @note
     * This constructor does not configure GPIO hardware. Call init().
     */
    
    /*
     * -------------------------------------------------------------------------
     * CONSTRUCTOR - Beginner Explanation
     * -------------------------------------------------------------------------
     * 
     * A CONSTRUCTOR is a special function that runs when you CREATE an object.
     * It has the same name as the class and no return type.
     * 
     * Example:
     *     RotaryEncoder encoder(GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_5);
     *                   ^^^^^^^ This calls the constructor!
     * 
     * The constructor stores the pin numbers but does NOT set up the hardware.
     * You must call init() separately. This is a common pattern that allows:
     *     1. Creating the object early
     *     2. Initializing hardware when you're ready
     * 
     * PARAMETERS:
     *     gpio_num_t is an enum (list of valid values) from ESP-IDF.
     *     It's safer than using plain integers because the compiler
     *     can check if you pass a valid GPIO number.
     * -------------------------------------------------------------------------
     */
    RotaryEncoder(gpio_num_t clk, gpio_num_t dt, gpio_num_t sw);


    /**
     * @brief Destroy the RotaryEncoder instance.
     *
     * @details
     * Removes GPIO ISR handlers from CLK and DT pins to prevent interrupts
     * from calling into an object that no longer exists.
     */
    
    /*
     * -------------------------------------------------------------------------
     * DESTRUCTOR - Beginner Explanation
     * -------------------------------------------------------------------------
     * 
     * A DESTRUCTOR is the opposite of a constructor. It runs when an object
     * is DESTROYED (goes out of scope or is deleted).
     * 
     * The name is: ~ClassName()  (tilde + class name)
     * 
     * WHY DO WE NEED IT?
     * 
     * When we call init(), we tell the ESP32:
     *     "When CLK pin changes, call isrHandler()"
     * 
     * If the encoder object is destroyed but we don't remove that instruction,
     * the hardware will still try to call isrHandler()... but the object
     * doesn't exist anymore! This causes a CRASH.
     * 
     * The destructor cleans up by removing the interrupt handlers.
     * -------------------------------------------------------------------------
     */
    ~RotaryEncoder();


    /**
     * @brief Initialize GPIO and attach interrupts.
     *
     * @details
     * - Configures CLK and DT as inputs with pull-ups and interrupt on any edge.
     * - Configures SW as input with pull-up (no interrupt).
     * - Reads initial quadrature state.
     * - Installs ISR service (if not already installed).
     * - Attaches ISR handler to both CLK and DT pins.
     *
     * @warning Must be called before using rotation functions reliably.
     */
    
    /*
     * -------------------------------------------------------------------------
     * init() - Beginner Explanation
     * -------------------------------------------------------------------------
     * 
     * This function sets up all the hardware. It does these things:
     * 
     * 1. CONFIGURE GPIO PINS
     *    - Set CLK and DT as inputs (we read FROM them)
     *    - Enable internal pull-up resistors (keep pins HIGH when open)
     *    - Enable interrupts on "any edge" (both LOW→HIGH and HIGH→LOW)
     *    - Set SW as input with pull-up (no interrupt, we poll it)
     * 
     * 2. READ INITIAL STATE
     *    - Read current CLK and DT values
     *    - Store them so we can detect the FIRST change correctly
     * 
     * 3. INSTALL INTERRUPT SERVICE
     *    - ESP-IDF needs a "dispatcher" set up once
     *    - This is shared by all GPIO interrupts
     * 
     * 4. ATTACH INTERRUPT HANDLERS
     *    - Tell ESP32: "When CLK changes, call isrHandler"
     *    - Tell ESP32: "When DT changes, call isrHandler"
     * 
     * MUST BE CALLED before getPosition() will work correctly!
     * -------------------------------------------------------------------------
     */
    void init();


    /**
     * @brief Get the current encoder position.
     *
     * @return Current position count (CW increments, CCW decrements).
     *
     * @note
     * Updated by ISR. Reads are safe on ESP32-class chips (aligned 32-bit).
     */
    
    /*
     * -------------------------------------------------------------------------
     * getPosition() - Beginner Explanation
     * -------------------------------------------------------------------------
     * 
     * Returns the current position counter.
     * 
     * This value is updated AUTOMATICALLY by the interrupt handler whenever
     * you turn the encoder. You don't need to do anything - just read it!
     * 
     *     - Positive values = turned clockwise
     *     - Negative values = turned counter-clockwise
     *     - Zero = either starting point or reset
     * 
     * The 'const' keyword means: "This function promises not to change
     * any member variables." It's a safety feature.
     * 
     * EXAMPLE:
     *     int32_t pos = encoder.getPosition();
     *     printf("Position: %ld\n", pos);
     * -------------------------------------------------------------------------
     */
    int32_t getPosition() const;


    /**
     * @brief Reset the encoder position to zero.
     */
    
    /*
     * -------------------------------------------------------------------------
     * resetPosition() - Beginner Explanation
     * -------------------------------------------------------------------------
     * 
     * Sets the position counter back to zero.
     * 
     * Use this when:
     *     - User presses the button to "reset"
     *     - You want to define a new "home" position
     *     - Starting a new measurement
     * 
     * EXAMPLE:
     *     if (encoder.wasButtonPressed()) {
     *         encoder.resetPosition();  // Reset to zero
     *     }
     * -------------------------------------------------------------------------
     */
    void resetPosition();


    /**
     * @brief Set the encoder position to a specific value.
     *
     * @param pos New position value to store.
     */
    
    /*
     * -------------------------------------------------------------------------
     * setPosition() - Beginner Explanation
     * -------------------------------------------------------------------------
     * 
     * Sets the position to any value you want (not just zero).
     * 
     * Use this when:
     *     - You want to start counting from a specific number
     *     - Restoring a saved position from memory
     *     - Implementing position limits (wrap around at max)
     * 
     * EXAMPLE:
     *     encoder.setPosition(100);  // Start from 100
     *     // Now turning CW gives 101, 102, 103...
     *     // Turning CCW gives 99, 98, 97...
     * -------------------------------------------------------------------------
     */
    void setPosition(int32_t pos);


    /**
     * @brief Read raw button state (active-low).
     *
     * @return true if button is pressed right now, false otherwise.
     *
     * @note No debouncing. Returns true continuously while held down.
     */
    
    /*
     * -------------------------------------------------------------------------
     * isButtonPressed() - Beginner Explanation
     * -------------------------------------------------------------------------
     * 
     * Returns TRUE if the button is being pressed RIGHT NOW.
     * Returns FALSE if the button is not pressed.
     * 
     * This is the RAW state - no debouncing:
     *     - Returns true THE WHOLE TIME you hold the button
     *     - Might flicker true/false during the initial press (bounce)
     * 
     * USE THIS FOR:
     *     - Detecting if button is HELD DOWN
     *     - Implementing "hold to repeat" features
     * 
     * DON'T USE THIS FOR:
     *     - Detecting a single button press (use wasButtonPressed instead)
     * -------------------------------------------------------------------------
     */
    bool isButtonPressed() const;


    /**
     * @brief Debounced edge detection for a button press.
     *
     * @return true exactly once when the button transitions from released->pressed.
     *
     * @details
     * Uses time-based debouncing and edge detection:
     * - Detects "pressed now AND was not pressed last time"
     * - Updates internal tracking state only when state changes.
     */
    
    /*
     * -------------------------------------------------------------------------
     * wasButtonPressed() - Beginner Explanation
     * -------------------------------------------------------------------------
     * 
     * Returns TRUE exactly ONCE when you press the button.
     * Returns FALSE all other times (holding, releasing, not pressed).
     * 
     * This includes DEBOUNCING:
     *     - Mechanical buttons "bounce" - they don't transition cleanly
     *     - Without debouncing, one press might register as 5-10 presses!
     *     - This function filters out the bounce
     * 
     * This includes EDGE DETECTION:
     *     - Only returns true at the MOMENT of pressing
     *     - Doesn't keep returning true while you hold the button
     * 
     *     Timeline:
     *     ─────────┐         ┌──────────
     *              │         │
     *              └─────────┘
     *              ↑ PRESS   ↑ RELEASE
     *              │
     *              └── wasButtonPressed() returns TRUE here, only once!
     * 
     * USE THIS FOR:
     *     - Triggering an action when button is pressed
     *     - Menus, resets, confirmations
     * 
     * EXAMPLE:
     *     if (encoder.wasButtonPressed()) {
     *         // This code runs ONCE per press
     *         doSomething();
     *     }
     * -------------------------------------------------------------------------
     */
    bool wasButtonPressed();


/*
 * =============================================================================
 * PRIVATE SECTION
 * =============================================================================
 * 
 * These are internal details that users DON'T need to access.
 * They're hidden to prevent accidental misuse and to keep the interface clean.
 */
private:

    /*
     * -------------------------------------------------------------------------
     * PIN CONFIGURATION
     * -------------------------------------------------------------------------
     * 
     * gpio_num_t is an enum (enumeration) from ESP-IDF.
     * It's a type that can only hold valid GPIO pin numbers.
     * Using it instead of 'int' helps catch errors at compile time.
     */
    gpio_num_t pinCLK;      // Encoder Channel A (CLK pin number)
    gpio_num_t pinDT;       // Encoder Channel B (DT pin number)
    gpio_num_t pinSW;       // Push button (SW pin number)


    /*
     * -------------------------------------------------------------------------
     * POSITION TRACKING - THE 'volatile' KEYWORD
     * -------------------------------------------------------------------------
     * 
     * WHAT IS 'volatile'?
     * 
     * It tells the compiler: "This variable can change at ANY moment,
     * even when you don't see any code changing it."
     * 
     * WHY DO WE NEED IT?
     * 
     * The 'position' variable is changed by the ISR (interrupt handler).
     * From the main code's perspective, there's no visible code changing it.
     * 
     * Without 'volatile', the compiler might "optimize" like this:
     * 
     *     // Your code:
     *     while (position == 0) {
     *         // wait for encoder to move
     *     }
     *     
     *     // Compiler thinks: "position never changes in this loop!"
     *     // Compiler optimizes to:
     *     while (true) {  // Infinite loop! position is never re-read!
     *     }
     * 
     * With 'volatile', the compiler MUST read from memory every time:
     * 
     *     while (position == 0) {
     *         // Compiler: "position is volatile, I must check memory each time"
     *         // ISR changes position in memory
     *         // Next check sees the new value!
     *     }
     * 
     * RULE: Any variable shared between ISR and main code needs 'volatile'.
     */
    volatile int32_t position;      // Current rotation count
                                    // Positive = CW turns, Negative = CCW turns
                                    // Range: about -2 billion to +2 billion
    
    volatile uint8_t lastEncoded;   // Previous state of CLK and DT pins
                                    // Stored as 2-bit value: (CLK << 1) | DT
                                    // Possible values: 0b00, 0b01, 0b10, 0b11


    /*
     * -------------------------------------------------------------------------
     * BUTTON STATE TRACKING
     * -------------------------------------------------------------------------
     * 
     * These are used by wasButtonPressed() for edge detection and debouncing.
     * They're NOT volatile because only the main loop accesses them
     * (button is polled, not interrupt-driven).
     */
    bool lastButtonState;           // Was button pressed on last check?
    uint64_t lastButtonChangeTime;  // When did button state last change? (microseconds)


    /*
     * -------------------------------------------------------------------------
     * ROTATION DEBOUNCING
     * -------------------------------------------------------------------------
     * 
     * WHAT IS DEBOUNCING?
     * 
     * Mechanical switches don't transition cleanly. They "bounce":
     * 
     *     What you expect:          What actually happens:
     *     
     *          ┌─────                    ┌─┬─┬───
     *          │                         │ │ │
     *     ─────┘                    ─────┴─┴─┘
     *     
     *     One clean                 Multiple rapid
     *     transition               on/off/on/off
     * 
     * Without debouncing, one turn might register as 5-10 turns!
     * 
     * SOLUTION: Ignore transitions that happen too quickly after the last one.
     * If the last valid transition was less than 1ms ago, ignore this one.
     */
    uint64_t lastRotationTime;      // When did last valid rotation happen? (microseconds)


    /**
     * @brief Rotation debounce time (microseconds).
     * @note Raise this if you see jitter/double steps on noisy wiring.
     */
    
    /*
     * -------------------------------------------------------------------------
     * DEBOUNCE CONSTANTS - 'static constexpr'
     * -------------------------------------------------------------------------
     * 
     * 'static' = Shared by ALL instances of this class (only one copy exists)
     * 'constexpr' = Value is computed at COMPILE TIME, not runtime
     *               (This is slightly more efficient than 'const')
     * 
     * Times are in MICROSECONDS:
     *     1 second        = 1,000,000 microseconds
     *     1 millisecond   = 1,000 microseconds
     *     1000 us         = 1 ms (rotation debounce)
     *     50000 us        = 50 ms (button debounce)
     */
    static constexpr uint32_t ROTATION_DEBOUNCE_US = 1000;   // 1ms for rotation


    /**
     * @brief Button debounce time (microseconds).
     * @note Human presses are slow; 50ms is a common safe default.
     */
    static constexpr uint32_t BUTTON_DEBOUNCE_US = 50000;    // 50ms for button


    /**
     * @brief GPIO ISR handler for quadrature transitions.
     *
     * @param arg Pointer passed during gpio_isr_handler_add (we pass `this`).
     *
     * @warning Runs in ISR context. Keep it short and ISR-safe.
     */
    
    /*
     * -------------------------------------------------------------------------
     * ISR HANDLER - WHY STATIC?
     * -------------------------------------------------------------------------
     * 
     * WHAT IS AN ISR?
     * 
     * ISR = Interrupt Service Routine
     * It's a function that runs AUTOMATICALLY when hardware detects an event.
     * It "interrupts" whatever the CPU was doing.
     * 
     * WHY MUST IT BE 'static'?
     * 
     * ESP-IDF requires ISR handlers to be either:
     *     1. Free functions (not inside any class)
     *     2. Static member functions
     * 
     * Regular member functions have a hidden 'this' parameter.
     * The hardware interrupt system doesn't know about C++ objects,
     * so it can't provide 'this'.
     * 
     * THE PROBLEM:
     * 
     * Static functions can't access instance variables (like 'position')
     * because they don't have a 'this' pointer.
     * 
     *     static void isrHandler() {
     *         position++;  // ERROR! Which encoder's position?
     *     }
     * 
     * THE SOLUTION:
     * 
     * We pass 'this' as an argument when registering the handler:
     * 
     *     gpio_isr_handler_add(pinCLK, isrHandler, (void*)this);
     *                                              ^^^^^^^^^^^
     *                                              Passing object address!
     * 
     * Inside the handler, we convert it back:
     * 
     *     static void isrHandler(void* arg) {
     *         RotaryEncoder* encoder = static_cast<RotaryEncoder*>(arg);
     *         (*encoder).position = (*encoder).position + 1;  // Now it works!
     *     }
     * 
     * WHAT IS IRAM_ATTR?
     * 
     * It tells the compiler to put this function in IRAM (Internal RAM)
     * instead of Flash memory.
     * 
     * WHY?
     *     - Flash can be BUSY (during WiFi operations, writing data, etc.)
     *     - ISRs must respond INSTANTLY, can't wait for Flash
     *     - IRAM is always accessible immediately
     * 
     * Without IRAM_ATTR, the program crashes if an interrupt happens
     * while Flash is busy (very common with WiFi enabled).
     * -------------------------------------------------------------------------
     */
      // ISR handler - must be static for C-style ISR
    static void isrHandler(void* arg);
    // Instance pointer for ISR (since static can't access members directly)
    static RotaryEncoder* instance;
};
