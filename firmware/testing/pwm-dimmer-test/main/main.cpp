/**
 * @file main.cpp
 * @brief PWM dimmer test (ESP-IDF).
 *
 * @details
 * Demonstrates the PWM dimmer component:
 * - Brightness levels
 * - Smooth fading
 * - Gamma correction comparison
 * - Various light effects
 *
 * Connect a MOSFET + LED strip to see the dimming in action.
 * Can also use just an LED with resistor for testing (much dimmer).
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"    
#include "pwm_dimmer.h"


static const char* TAG = "PWM_TEST";


#ifndef PWM_PIN
#define PWM_PIN 25
#endif


extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== PWM Dimmer Test ===");
    ESP_LOGI(TAG, "GPIO=%d", PWM_PIN);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Wiring (IRLB8721 MOSFET):");
    ESP_LOGI(TAG, "  Gate (left)   → GPIO %d", PWM_PIN);
    ESP_LOGI(TAG, "  Drain (mid)   → LED Strip (-)");
    ESP_LOGI(TAG, "  Source (right)→ GND (common)");
    ESP_LOGI(TAG, "");
    
    /*
     * =========================================================================
     * CREATE AND INITIALIZE DIMMER
     * =========================================================================
     */
    PWMDimmer light((gpio_num_t)PWM_PIN);
    
    if (!light.init()) {
        ESP_LOGE(TAG, "PWM init failed!");
        return;
    }
    
    ESP_LOGI(TAG, "PWM initialized. Running tests...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    while (1) {
    /*
     * =========================================================================
     * TEST 1: On/Off
     * =========================================================================
     */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Test 1: On/Off");
    
    ESP_LOGI(TAG, "  ON");
    light.on();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "  OFF");
    light.off();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "  ON");
    light.on();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "  OFF");
    light.off();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    /*
     * =========================================================================
     * TEST 2: Brightness levels (with gamma)
     * =========================================================================
     */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Test 2: Brightness levels (with gamma correction)");
    
    for (int brightness = 0; brightness <= 100; brightness += 10) {
        ESP_LOGI(TAG, "  %d%%", brightness);
        light.setBrightness(brightness, true);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    light.off();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    /*
     * =========================================================================
     * TEST 3: Brightness levels (without gamma - for comparison)
     * =========================================================================
     */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Test 3: Brightness levels (linear, no gamma)");
    
    for (int brightness = 0; brightness <= 100; brightness += 10) {
        ESP_LOGI(TAG, "  %d%%", brightness);
        light.setBrightness(brightness, false);  // No gamma
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    light.off();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    /*
     * =========================================================================
     * TEST 4: Smooth fade in/out
     * =========================================================================
     */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Test 4: Smooth fade in/out (2 seconds each)");
    
    ESP_LOGI(TAG, "  Fading in...");
    light.fadeIn(2000);
    vTaskDelay(pdMS_TO_TICKS(2500));
    
    ESP_LOGI(TAG, "  Fading out...");
    light.fadeOut(2000);
    vTaskDelay(pdMS_TO_TICKS(2500));
    
    /*
     * =========================================================================
     * TEST 5: Fade to specific levels
     * =========================================================================
     */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Test 5: Fade to specific levels");
    
    ESP_LOGI(TAG, "  Fading to 25%%...");
    light.fadeTo(25, 1000);
    vTaskDelay(pdMS_TO_TICKS(1500));
    
    ESP_LOGI(TAG, "  Fading to 75%%...");
    light.fadeTo(75, 1000);
    vTaskDelay(pdMS_TO_TICKS(1500));
    
    ESP_LOGI(TAG, "  Fading to 50%%...");
    light.fadeTo(50, 1000);
    vTaskDelay(pdMS_TO_TICKS(1500));
    
    ESP_LOGI(TAG, "  Fading to 0%%...");
    light.fadeTo(0, 1000);
    vTaskDelay(pdMS_TO_TICKS(1500));
    
    /*
     * =========================================================================
     * TEST 6: Breathing effect
     * =========================================================================
     */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Test 6: Breathing effect (5 cycles)");
    
    for (int cycle = 0; cycle < 5; cycle++) {
        light.fadeIn(1500);
        vTaskDelay(pdMS_TO_TICKS(1600));
        light.fadeOut(1500);
        vTaskDelay(pdMS_TO_TICKS(1600));
    }
    
    /*
     * =========================================================================
     * TEST 7: Strobe effect
     * =========================================================================
     */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Test 7: Strobe effect (10 flashes)");
    
    for (int i = 0; i < 10; i++) {
        light.on();
        vTaskDelay(pdMS_TO_TICKS(50));
        light.off();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    /*
     * =========================================================================
     * TEST 8: Candle flicker effect
     * =========================================================================
     */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Test 8: Candle flicker effect (5 seconds)");
    
    uint32_t startTime = xTaskGetTickCount();
    while ((xTaskGetTickCount() - startTime) < pdMS_TO_TICKS(5000)) {
        // Random brightness between 40-100%
        int brightness = 40 + (esp_random() % 60);
        light.setBrightness(brightness, true);
        
        // Random delay between 30-100ms
        int delay = 30 + (esp_random() % 70);
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
    
    light.off();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    /*
     * =========================================================================
     * COMPLETE
     * =========================================================================
     */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "All tests complete!");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Entering continuous breathing mode...");
    
    
        light.fadeIn(2000);
        vTaskDelay(pdMS_TO_TICKS(2100));
        light.fadeOut(2000);
        vTaskDelay(pdMS_TO_TICKS(2100));
    }
}
