/**
 * @file main.cpp
 * @brief Touch sensor test application (ESP-IDF + FreeRTOS).
 *
 * @details
 * Demonstrates the TouchSensor component:
 * - Creates TouchSensor instances for TTP223 modules
 * - Polls touch state in main loop
 * - Detects touch and release events
 *
 * @par Pin configuration (PlatformIO)
 * Define touch pins in platformio.ini:
 * @code
 * build_flags =
 *   -DTOUCH1_PIN=4
 *   -DTOUCH2_PIN=5
 * @endcode
 */

/*
 * =============================================================================
 * BEGINNER'S GUIDE
 * =============================================================================
 * 
 * This test program shows how to use the TouchSensor class.
 * 
 * Very similar to the button test, but:
 *     - No debouncing needed (TTP223 chip handles it)
 *     - May be active HIGH (most modules) or active LOW
 * 
 * =============================================================================
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "touch.h"


static const char* TAG = "TOUCH_TEST";


/*
 * Default pins if not defined in platformio.ini
 */
#ifndef TOUCH1_PIN
#define TOUCH1_PIN 4
#endif

#ifndef TOUCH2_PIN
#define TOUCH2_PIN 5
#endif


extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== Touch Sensor Test (TTP223 / HTTM) ===");
    
    /*
     * -------------------------------------------------------------------------
     * CREATE TOUCH SENSOR OBJECTS
     * -------------------------------------------------------------------------
     * 
     * TouchSensor(pin, activeHigh)
     *     pin: GPIO number
     *     activeHigh: true if HIGH when touched (default, most modules)
     *                 false if LOW when touched
     */
    TouchSensor touch1((gpio_num_t)TOUCH1_PIN);              // Active HIGH (default)
    TouchSensor touch2((gpio_num_t)TOUCH2_PIN);       // Active LOW (if your module is wired that way)
    
    /*
     * -------------------------------------------------------------------------
     * INITIALIZE TOUCH SENSORS
     * -------------------------------------------------------------------------
     */
    touch1.init();
    touch2.init();
    
    ESP_LOGI(TAG, "Touch sensors initialized:");
    ESP_LOGI(TAG, "  Touch 1: GPIO %d (active HIGH)", TOUCH1_PIN);
    ESP_LOGI(TAG, "  Touch 2: GPIO %d (active LOW)", TOUCH2_PIN);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Touch the sensors to test!");
    ESP_LOGI(TAG, "Hold Touch 1 for a few seconds to see duration tracking.");
    
    /*
     * -------------------------------------------------------------------------
     * MAIN LOOP
     * -------------------------------------------------------------------------
     */
    while (1) {
        /*
         * Update both sensors (read GPIO, detect edges)
         */
        touch1.update();
        touch2.update();
        
        /*
         * ---------------------------------------------------------------------
         * TOUCH 1: Full feature test
         * ---------------------------------------------------------------------
         */
        
        // Detect touch start
        if (touch1.wasTouched()) {
            ESP_LOGI(TAG, "Touch 1: TOUCHED!");
        }
        
        // Detect touch end
        if (touch1.wasReleased()) {
            ESP_LOGI(TAG, "Touch 1: RELEASED!");
        }
        
        // Show duration while touching (every 500ms)
        static uint32_t lastDurationLog = 0;
        if (touch1.isTouched()) {
            uint32_t duration = touch1.getTouchedDuration();
            if (duration - lastDurationLog >= 500) {
                ESP_LOGI(TAG, "Touch 1: Still touching... (%lu ms)", duration);
                lastDurationLog = duration;
            }
        } else {
            lastDurationLog = 0;
        }
        
        /*
         * ---------------------------------------------------------------------
         * TOUCH 2: Simple touch detection
         * ---------------------------------------------------------------------
         */
        
        if (touch2.wasTouched()) {
            ESP_LOGI(TAG, "Touch 2: TOUCHED!");
        }
        
        if (touch2.wasReleased()) {
            ESP_LOGI(TAG, "Touch 2: RELEASED!");
        }
        
        /*
         * ---------------------------------------------------------------------
         * DELAY
         * ---------------------------------------------------------------------
         * 
         * 10ms delay = 100 updates per second
         * Touch sensors respond quickly, so this is plenty fast.
         */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
