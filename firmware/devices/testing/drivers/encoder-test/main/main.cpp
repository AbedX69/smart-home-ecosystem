/**
 * @file main.cpp
 * @brief Rotary Encoder test application (ESP-IDF + FreeRTOS).
 *
 * @details
 * This application demonstrates the RotaryEncoder component:
 * - Creates a RotaryEncoder instance using pin macros defined via PlatformIO build_flags
 * - Calls init() to configure GPIO + attach interrupts
 * - Prints position changes (interrupt-driven)
 * - Detects button press events and resets position
 *
 * @par Pin configuration (PlatformIO)
 * Pins are defined in platformio.ini using build_flags:
 * @code
 * build_flags =
 *   -DENCODER_CLK=18
 *   -DENCODER_DT=19
 *   -DENCODER_SW=5
 * @endcode
 *
 * These become preprocessor macros available at compile time:
 * @code
 * #define ENCODER_CLK 18
 * #define ENCODER_DT  19
 * #define ENCODER_SW  5
 * @endcode
 *
 * @note
 * Rotation is interrupt-driven, so the main loop can sleep longer (e.g. 50ms)
 * without missing encoder transitions.
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE: MAIN APPLICATION
 * =============================================================================
 * 
 * This is the entry point of the program. It demonstrates how to use the
 * RotaryEncoder library.
 * 
 * =============================================================================
 * HOW ESP-IDF PROGRAMS START
 * =============================================================================
 * 
 * Unlike desktop C programs that start with main(), ESP-IDF uses app_main().
 * 
 * When you power on the ESP32:
 *     1. Bootloader runs (built into the chip)
 *     2. Your program is loaded from flash
 *     3. FreeRTOS starts (the operating system)
 *     4. app_main() is called
 * 
 * From app_main(), your code takes over!
 * 
 * =============================================================================
 * PIN CONFIGURATION VIA platformio.ini
 * =============================================================================
 * 
 * Instead of hard-coding pin numbers, we use build_flags in platformio.ini:
 * 
 *     [env:esp32d]
 *     build_flags = 
 *         -DENCODER_CLK=18
 *         -DENCODER_DT=19
 *         -DENCODER_SW=5
 * 
 * The -D flag tells the compiler to define a preprocessor macro.
 * It's equivalent to writing this in your code:
 * 
 *     #define ENCODER_CLK 18
 *     #define ENCODER_DT  19
 *     #define ENCODER_SW  5
 * 
 * ADVANTAGE: Different boards can use different pins without changing code!
 * 
 *     [env:esp32d]
 *     build_flags = -DENCODER_CLK=18 -DENCODER_DT=19 -DENCODER_SW=5
 *     
 *     [env:c6_seeed]
 *     build_flags = -DENCODER_CLK=0 -DENCODER_DT=1 -DENCODER_SW=2
 * 
 * Same code, different pin configurations per board!
 * 
 * =============================================================================
 * PROGRAM FLOW DIAGRAM
 * =============================================================================
 * 
 *     ┌─────────────────────────────────────────────────────────────┐
 *     │                         app_main()                          │
 *     │                                                             │
 *     │  1. Create RotaryEncoder object with pin numbers            │
 *     │                      │                                      │
 *     │                      ▼                                      │
 *     │  2. Call encoder.init()                                     │
 *     │     - Configure GPIO pins                                   │
 *     │     - Set up interrupts                                     │
 *     │     - Read initial state                                    │
 *     │                      │                                      │
 *     │                      ▼                                      │
 *     │  3. Enter infinite loop                                     │
 *     │     ┌─────────────────────────────────────────────┐        │
 *     │     │                                             │        │
 *     │     │  a. Read position (updated by ISR)          │        │
 *     │     │                  │                          │        │
 *     │     │                  ▼                          │        │
 *     │     │  b. If changed, print new position          │        │
 *     │     │                  │                          │        │
 *     │     │                  ▼                          │        │
 *     │     │  c. Check if button was pressed             │        │
 *     │     │                  │                          │        │
 *     │     │                  ▼                          │        │
 *     │     │  d. If pressed, reset position to 0         │        │
 *     │     │                  │                          │        │
 *     │     │                  ▼                          │        │
 *     │     │  e. Sleep 50ms                              │        │
 *     │     │                  │                          │        │
 *     │     │                  ▼                          │        │
 *     │     │  f. Go back to (a)                          │        │
 *     │     │                                             │        │
 *     │     └─────────────────────────────────────────────┘        │
 *     │                                                             │
 *     │  Meanwhile, ISR runs automatically on any encoder movement  │
 *     │                                                             │
 *     └─────────────────────────────────────────────────────────────┘
 * 
 * =============================================================================
 * EXPECTED SERIAL OUTPUT
 * =============================================================================
 * 
 *     I (297) ENCODER_TEST: === Rotary Encoder Test (Interrupt-Driven) ===
 *     I (302) RotaryEncoder: Initializing rotary encoder on CLK=18, DT=19, SW=5
 *     I (308) RotaryEncoder: Encoder initialized successfully
 *     I (314) ENCODER_TEST: Ready! Turn the encoder and press the button
 *     I (320) ENCODER_TEST: Note: Position updates happen automatically via interrupts!
 *     
 *     (You turn the encoder clockwise)
 *     I (1234) ENCODER_TEST: Position: 1 (delta: +1)
 *     I (1456) ENCODER_TEST: Position: 2 (delta: +1)
 *     I (1678) ENCODER_TEST: Position: 3 (delta: +1)
 *     
 *     (You turn counter-clockwise)
 *     I (2345) ENCODER_TEST: Position: 2 (delta: -1)
 *     I (2567) ENCODER_TEST: Position: 1 (delta: -1)
 *     
 *     (You press the button)
 *     I (3456) ENCODER_TEST: >>> Button PRESSED! Resetting position to 0 <<<
 * 
 * =============================================================================
 */


/*
 * -----------------------------------------------------------------------------
 * INCLUDES
 * -----------------------------------------------------------------------------
 */

/*
 * <stdio.h>
 *     Standard C input/output library.
 *     Provides printf(), which we don't use directly (we use ESP_LOGI instead),
 *     but it's commonly included as a habit.
 */
#include <stdio.h>

/*
 * "freertos/FreeRTOS.h"
 *     FreeRTOS is the Real-Time Operating System that runs on ESP32.
 *     It provides:
 *         - Task scheduling (run multiple "threads")
 *         - Delays (vTaskDelay)
 *         - Synchronization primitives (mutexes, semaphores, queues)
 *     
 *     This header must be included before other FreeRTOS headers.
 */
#include "freertos/FreeRTOS.h"

/*
 * "freertos/task.h"
 *     Provides task-related functions:
 *         - vTaskDelay(): Sleep for a number of ticks
 *         - xTaskCreate(): Create new tasks
 *         - etc.
 */
#include "freertos/task.h"

/*
 * "esp_log.h"
 *     ESP-IDF logging system. Provides:
 *         - ESP_LOGE(): Error messages (red)
 *         - ESP_LOGW(): Warning messages (yellow)
 *         - ESP_LOGI(): Info messages (green)
 *         - ESP_LOGD(): Debug messages (no color, disabled by default)
 *         - ESP_LOGV(): Verbose messages (no color, disabled by default)
 *     
 *     All take a TAG as first argument to identify the source.
 */
#include "esp_log.h"

/*
 * "encoder.h"
 *     Our rotary encoder library!
 */
#include "encoder.h"


/**
 * @brief Logging tag used by ESP-IDF logging macros in this file.
 */

/*
 * -----------------------------------------------------------------------------
 * LOGGING TAG
 * -----------------------------------------------------------------------------
 * 
 * This string identifies log messages from this file.
 * 
 * In the output:
 *     I (1234) ENCODER_TEST: Position: 5
 *              ^^^^^^^^^^^^
 *              This is the TAG
 * 
 * 'static' = Only visible in this file (can't be accessed from other files)
 * 'const' = Value can't be changed
 * 'char*' = Pointer to a string
 */
static const char *TAG = "ENCODER_TEST";


/**
 * @brief ESP-IDF entry point.
 *
 * @details
 * ESP-IDF expects a C symbol named app_main().
 * Using `extern "C"` prevents C++ name mangling so the runtime can find it.
 *
 * Program flow:
 * 1) Create RotaryEncoder using ENCODER_* macros
 * 2) encoder.init() installs GPIO config + ISR handlers
 * 3) Loop forever:
 *    - read position and print only on change
 *    - detect button press event and reset
 *    - delay 50ms (interrupts still catch all rotation)
 *
 * @note
 * This function never returns because embedded firmware typically runs forever.
 */

/*
 * =============================================================================
 * MAIN FUNCTION: app_main
 * =============================================================================
 * 
 * WHY 'extern "C"'?
 * 
 * This file is C++ (.cpp), but ESP-IDF's startup code is C.
 * C and C++ have different "name mangling":
 * 
 *     C:   app_main → "app_main"
 *     C++: app_main → "_Z8app_mainv" (or similar gibberish)
 * 
 * The ESP-IDF bootloader looks for a function named exactly "app_main".
 * Without 'extern "C"', it won't find our function!
 * 
 * 'extern "C"' tells the C++ compiler:
 * "Don't mangle this name. Use C-style naming so the C code can find it."
 * 
 * WHY 'void' RETURN TYPE?
 * 
 * Unlike desktop programs (where main() returns int to the OS),
 * embedded programs run forever. There's no OS to return to!
 * The return value would never be used, so we use void.
 */
extern "C" void app_main(void) {
    
    /*
     * -------------------------------------------------------------------------
     * STARTUP MESSAGE
     * -------------------------------------------------------------------------
     * 
     * ESP_LOGI(tag, format, ...) - Log an INFO message
     *     tag: String identifying the source (we defined TAG above)
     *     format: printf-style format string
     *     ...: Optional values to insert into format string
     */
    ESP_LOGI(TAG, "=== Rotary Encoder Test (Interrupt-Driven) ===");
    
    
    /**
     * @brief Create encoder instance.
     *
     * @details
     * ENCODER_CLK / ENCODER_DT / ENCODER_SW are integer macros from platformio.ini.
     * We cast them to gpio_num_t because the driver uses the ESP-IDF GPIO enum type.
     */
    
    /*
     * -------------------------------------------------------------------------
     * CREATE ENCODER OBJECT
     * -------------------------------------------------------------------------
     * 
     * ENCODER_CLK, ENCODER_DT, ENCODER_SW are macros defined in platformio.ini.
     * They're just numbers (like 18, 19, 5).
     * 
     * (gpio_num_t) is a TYPE CAST:
     *     - RotaryEncoder constructor expects gpio_num_t type
     *     - Our macros are plain integers (int)
     *     - Cast converts int → gpio_num_t
     * 
     * This creates the encoder object but does NOT set up hardware yet.
     * Hardware setup happens in init().
     */
    RotaryEncoder encoder(
        (gpio_num_t)ENCODER_CLK,    // Channel A pin (CLK)
        (gpio_num_t)ENCODER_DT,     // Channel B pin (DT)
        (gpio_num_t)ENCODER_SW      // Button pin (SW)
    );
    
    
    /**
     * @brief Initialize hardware (GPIO + interrupts).
     *
     * @warning Must be called before relying on position updates.
     */
    
    /*
     * -------------------------------------------------------------------------
     * INITIALIZE ENCODER HARDWARE
     * -------------------------------------------------------------------------
     * 
     * This is where the magic happens:
     *     1. Configure GPIO pins (input mode, pull-ups)
     *     2. Enable interrupts on CLK and DT (any edge)
     *     3. Register ISR handler
     *     4. Read initial encoder state
     * 
     * After this call, the ISR will automatically update 'position'
     * whenever you turn the encoder. No polling needed!
     */
    encoder.init();
    
    
    /*
     * -------------------------------------------------------------------------
     * READY MESSAGES
     * -------------------------------------------------------------------------
     */
    ESP_LOGI(TAG, "Ready! Turn the encoder and press the button");
    ESP_LOGI(TAG, "Note: Position updates happen automatically via interrupts!");
    
    
    /**
     * @brief Tracks the last printed position to avoid log spam.
     *
     * @note
     * The ISR may update position multiple times between loop iterations.
     * Therefore delta can be > 1 or < -1 if you spin fast.
     */
    
    /*
     * -------------------------------------------------------------------------
     * TRACKING VARIABLE
     * -------------------------------------------------------------------------
     * 
     * We keep track of the last position we printed.
     * This way, we only print when something CHANGES.
     * 
     * Without this, we'd print "Position: 0" every 50ms even when
     * nothing is happening (annoying!).
     * 
     * int32_t = 32-bit signed integer
     *     Range: about -2 billion to +2 billion
     *     Enough for any reasonable encoder usage
     */
    int32_t lastPos = 0;
    
    
    /*
     * -------------------------------------------------------------------------
     * MAIN LOOP
     * -------------------------------------------------------------------------
     * 
     * while(1) means "loop forever".
     * This is normal for embedded systems - the program runs until power off.
     * 
     * WHY CAN WE USE A 50ms DELAY?
     * 
     * With POLLING, a 50ms delay would miss fast encoder turns.
     * But we use INTERRUPTS - the ISR catches every turn instantly!
     * 
     * The main loop just:
     *     1. Reads the position (which ISR updates automatically)
     *     2. Prints if changed
     *     3. Checks button
     *     4. Sleeps
     * 
     * Even during sleep, the ISR is still active and catching turns.
     */
    while (1) {
        
        /*
         * ---------------------------------------------------------------------
         * READ CURRENT POSITION
         * ---------------------------------------------------------------------
         * 
         * getPosition() just reads a variable - very fast!
         * The actual rotation detection happened in the ISR.
         * 
         * Even if we slept for 50ms, every encoder turn was caught by the ISR
         * and the position was updated. We just read the final result.
         */
        int32_t pos = encoder.getPosition();
        
        
        /*
         * ---------------------------------------------------------------------
         * PRINT IF POSITION CHANGED
         * ---------------------------------------------------------------------
         * 
         * Only print when something changes. This keeps logs clean.
         */
        if (pos != lastPos) {
            /*
             * Calculate HOW MUCH it changed (delta).
             * 
             *     delta = new_position - old_position
             *     
             *     Positive delta = turned clockwise
             *     Negative delta = turned counter-clockwise
             * 
             * Usually delta is +1 or -1, but if you spin REALLY fast,
             * multiple turns might happen between loop iterations,
             * so delta could be +3 or -5 or whatever.
             */
            int32_t delta = pos - lastPos;
            
            /*
             * Print the new position and the change.
             * 
             * %ld = "long decimal" - format for long int (int32_t)
             * %+ld = same, but ALWAYS show sign (+ or -)
             * 
             * (long) cast is for compatibility with printf on different platforms.
             */
            ESP_LOGI(TAG, "Position: %ld (delta: %+ld)", (long)pos, (long)delta);
            
            /*
             * Remember this position for next time.
             */
            lastPos = pos;
        }
        
        
        /**
         * @brief Debounced button press event.
         *
         * @details
         * wasButtonPressed() returns true ONCE per press (edge detect + debounce).
         */
        
        /*
         * ---------------------------------------------------------------------
         * CHECK FOR BUTTON PRESS
         * ---------------------------------------------------------------------
         * 
         * wasButtonPressed() returns true ONCE when you press the button:
         *     - Includes debouncing (filters mechanical bounce)
         *     - Includes edge detection (only triggers on press, not hold)
         * 
         * If button was pressed, reset position to zero.
         */
        if (encoder.wasButtonPressed()) {
            ESP_LOGI(TAG, ">>> Button PRESSED! Resetting position to 0 <<<");
            encoder.resetPosition();    // Set position to 0
            lastPos = 0;                // Also update our tracking variable!
        }
        
        
        /**
         * @brief Sleep to reduce CPU usage.
         *
         * @details
         * Rotation is interrupt-driven, so we don't need fast polling here.
         * 50ms is a good balance for responsiveness and low CPU usage.
         */
        
        /*
         * ---------------------------------------------------------------------
         * SLEEP
         * ---------------------------------------------------------------------
         * 
         * vTaskDelay() pauses this task, allowing:
         *     - Other FreeRTOS tasks to run
         *     - CPU to enter low-power state
         *     - System to remain responsive
         * 
         * pdMS_TO_TICKS(50) converts 50 milliseconds to FreeRTOS "ticks".
         * A tick is the basic time unit in FreeRTOS (usually 1ms or 10ms).
         * 
         * WHY 50ms IS FINE:
         * 
         * With polling: 50ms delay = miss encoder turns
         * With interrupts: 50ms delay = no problem!
         * 
         * The ISR runs instantly when encoder moves, regardless of this delay.
         * We only need to wake up occasionally to:
         *     - Print position changes
         *     - Check button
         * 
         * 50ms = 20 updates per second = responsive enough for UI
         * But not wasting CPU cycles checking constantly.
         */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    
    /*
     * -------------------------------------------------------------------------
     * THIS CODE IS NEVER REACHED
     * -------------------------------------------------------------------------
     * 
     * The while(1) loop never exits, so the function never returns.
     * This is normal for embedded systems.
     * 
     * If you ever need to exit (for testing), you could use:
     *     break;          // Exit the while loop
     *     return;         // Exit the function
     *     esp_restart();  // Reboot the ESP32
     */
}
