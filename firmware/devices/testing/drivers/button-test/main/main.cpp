/**
 * @file main.cpp
 * @brief Button test application (ESP-IDF + FreeRTOS).
 *
 * @details
 * Demonstrates the Button component:
 * - Creates Button instances for tactile switches
 * - Polls button state in main loop
 * - Detects press, release, and long-press events
 *
 * @par Pin configuration (PlatformIO)
 * Define button pins in platformio.ini:
 * @code
 * build_flags =
 *   -DBUTTON1_PIN=5
 *   -DBUTTON2_PIN=4
 * @endcode
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE
 * =============================================================================
 * 
 * This test program shows how to use the Button class.
 * 
 * The main difference from the encoder test:
 *     - Encoder uses INTERRUPTS (automatic, in background)
 *     - Buttons use POLLING (we check them manually in the loop)
 * 
 * That's why we call button.update() every loop iteration!
 * 
 * =============================================================================
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "button.h"


static const char* TAG = "BUTTON_TEST";


/*
 * Default pin if not defined in platformio.ini
 */
#ifndef BUTTON1_PIN
#define BUTTON1_PIN 5
#endif

#ifndef BUTTON2_PIN
#define BUTTON2_PIN 4
#endif


extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== Button Test ===");
    
    /*
     * -------------------------------------------------------------------------
     * CREATE BUTTON OBJECTS
     * -------------------------------------------------------------------------
     * 
     * Button(pin, debounceMs)
     *     pin: GPIO number
     *     debounceMs: Debounce time (default 50ms)
     */
    Button button1((gpio_num_t)BUTTON1_PIN);
    Button button2((gpio_num_t)BUTTON2_PIN, 30);  // Custom 30ms debounce
    
    /*
     * -------------------------------------------------------------------------
     * INITIALIZE BUTTONS
     * -------------------------------------------------------------------------
     */
    button1.init();
    button2.init();
    
    ESP_LOGI(TAG, "Buttons initialized on GPIO %d and GPIO %d", 
             BUTTON1_PIN, BUTTON2_PIN);
    ESP_LOGI(TAG, "Press the buttons to test!");
    ESP_LOGI(TAG, "Hold Button 1 for 2+ seconds to see long-press detection.");
    
    /*
     * Track if we've already reported a long press
     * (so we don't spam the log)
     */
    bool longPressReported = false;
    
    /*
     * -------------------------------------------------------------------------
     * MAIN LOOP
     * -------------------------------------------------------------------------
     */
    while (1) {
        /*
         * IMPORTANT: Call update() for each button every loop!
         * This reads the GPIO and updates internal state.
         */
        button1.update();
        button2.update();
        
        /*
         * ---------------------------------------------------------------------
         * BUTTON 1: Test all features
         * ---------------------------------------------------------------------
         */
        
        // Detect press (once per press)
        if (button1.wasPressed()) {
            ESP_LOGI(TAG, "Button 1: PRESSED!");
            longPressReported = false;  // Reset long-press flag
        }
        
        // Detect release (once per release)
        if (button1.wasReleased()) {
            ESP_LOGI(TAG, "Button 1: RELEASED!");
        }
        
        // Long-press detection (while held)
        if (button1.isPressed()) {
            uint32_t duration = button1.getPressedDuration();
            
            // Report long-press once after 2 seconds
            if (duration > 2000 && !longPressReported) {
                ESP_LOGI(TAG, "Button 1: LONG PRESS detected! (held for %lu ms)", duration);
                longPressReported = true;
            }
        }
        
        /*
         * ---------------------------------------------------------------------
         * BUTTON 2: Simple press detection
         * ---------------------------------------------------------------------
         */
        
        if (button2.wasPressed()) {
            ESP_LOGI(TAG, "Button 2: PRESSED!");
        }
        
        if (button2.wasReleased()) {
            ESP_LOGI(TAG, "Button 2: RELEASED!");
        }
        
        /*
         * ---------------------------------------------------------------------
         * DELAY
         * ---------------------------------------------------------------------
         * 
         * 10ms delay = 100 updates per second
         * Fast enough for responsive buttons, slow enough to save CPU.
         */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
