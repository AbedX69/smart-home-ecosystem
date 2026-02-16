/**
 * @file main.cpp
 * @brief MOSFET driver test application (ESP-IDF + FreeRTOS).
 *
 * @details
 * Tests the MosfetDriver component:
 * - Basic on/off
 * - Level control (0-100%)
 * - Soft start (inrush protection)
 * - Fading
 *
 * Connect a MOSFET module or discrete MOSFET with LED strip
 * to the configured GPIO pin.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mosfet_driver.h"

static const char *TAG = "MOSFET_TEST";

#ifndef MOSFET_PIN
#define MOSFET_PIN 4
#endif

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== MOSFET Driver Test ===");
    ESP_LOGI(TAG, "MOSFET pin: GPIO %d", MOSFET_PIN);

    MosfetDriver led((gpio_num_t)MOSFET_PIN);
    led.init();

    ESP_LOGI(TAG, "Starting tests in 2 seconds...");
    ESP_LOGI(TAG, "Connect LED strip: (+) to PSU, (-) to MOSFET VIN-");
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        // --- Test 1: Basic on/off ---
        ESP_LOGI(TAG, "[1] Basic on/off");
        ESP_LOGI(TAG, "    ON (100%%)");
        led.on();
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        ESP_LOGI(TAG, "    OFF (0%%)");
        led.off();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 2: Level control ---
        ESP_LOGI(TAG, "[2] Level control: 25%% -> 50%% -> 75%% -> 100%%");
        
        led.setLevel(25);
        ESP_LOGI(TAG, "    25%%");
        vTaskDelay(pdMS_TO_TICKS(1500));
        
        led.setLevel(50);
        ESP_LOGI(TAG, "    50%%");
        vTaskDelay(pdMS_TO_TICKS(1500));
        
        led.setLevel(75);
        ESP_LOGI(TAG, "    75%%");
        vTaskDelay(pdMS_TO_TICKS(1500));
        
        led.setLevel(100);
        ESP_LOGI(TAG, "    100%%");
        vTaskDelay(pdMS_TO_TICKS(1500));
        
        led.off();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 3: Soft start ---
        ESP_LOGI(TAG, "[3] Soft start: 0%% -> 100%% over 1 second");
        led.softStart(100, 1000);
        vTaskDelay(pdMS_TO_TICKS(2000));
        led.off();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 4: Soft start to 50% ---
        ESP_LOGI(TAG, "[4] Soft start: 0%% -> 50%% over 500ms");
        led.softStart(50, 500);
        vTaskDelay(pdMS_TO_TICKS(2000));
        led.off();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 5: Fade in ---
        ESP_LOGI(TAG, "[5] Fade in: 0%% -> 100%% over 2 seconds (non-blocking)");
        led.fadeIn(2000);
        vTaskDelay(pdMS_TO_TICKS(3000));  // Wait for fade + extra

        // --- Test 6: Fade out ---
        ESP_LOGI(TAG, "[6] Fade out: 100%% -> 0%% over 2 seconds");
        led.fadeOut(2000);
        vTaskDelay(pdMS_TO_TICKS(3000));

        // --- Test 7: Fade to specific levels ---
        ESP_LOGI(TAG, "[7] Fade to 75%%, then to 25%%");
        led.fadeTo(75, 1000);
        vTaskDelay(pdMS_TO_TICKS(1500));
        led.fadeTo(25, 1000);
        vTaskDelay(pdMS_TO_TICKS(1500));
        led.off();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 8: Toggle ---
        ESP_LOGI(TAG, "[8] Toggle 4 times (1 second each)");
        for (int i = 0; i < 4; i++) {
            led.toggle();
            ESP_LOGI(TAG, "    %s", led.isOn() ? "ON" : "OFF");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        led.off();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 9: Compare soft start vs instant on ---
        ESP_LOGI(TAG, "[9] Comparison: instant ON vs soft start");
        
        ESP_LOGI(TAG, "    Instant ON (watch for brightness spike)");
        led.on();
        vTaskDelay(pdMS_TO_TICKS(1500));
        led.off();
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "    Soft start (smooth ramp)");
        led.softStartFull(500);
        vTaskDelay(pdMS_TO_TICKS(1500));
        led.off();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 10: Raw duty cycle ---
        ESP_LOGI(TAG, "[10] Raw duty: 0 -> 256 -> 512 -> 1023 (10-bit max)");
        
        led.setDuty(0);
        ESP_LOGI(TAG, "    Duty: 0 (off)");
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        led.setDuty(256);
        ESP_LOGI(TAG, "    Duty: 256 (~25%%)");
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        led.setDuty(512);
        ESP_LOGI(TAG, "    Duty: 512 (~50%%)");
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        led.setDuty(1023);
        ESP_LOGI(TAG, "    Duty: 1023 (max)");
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        led.off();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 11: Gamma correction comparison ---
        ESP_LOGI(TAG, "[11] Gamma comparison at 50%%");
        
        ESP_LOGI(TAG, "    Linear 50%% (no gamma)");
        led.setLevel(50, false);
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        ESP_LOGI(TAG, "    Gamma 50%% (should look dimmer)");
        led.setLevel(50, true);
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        led.off();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 12: Gamma ramp ---
        ESP_LOGI(TAG, "[12] Gamma ramp: 0%% -> 100%% in steps");
        for (int i = 0; i <= 100; i += 10) {
            led.setLevel(i, true);
            ESP_LOGI(TAG, "    %d%%", i);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        led.off();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 13: Soft start with gamma ---
        ESP_LOGI(TAG, "[13] Soft start with gamma: 0%% -> 100%% over 1s");
        led.softStart(100, 1000, true);
        vTaskDelay(pdMS_TO_TICKS(2000));
        led.off();
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "=== All tests done! Restarting in 3s... ===");
        ESP_LOGI(TAG, "");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
