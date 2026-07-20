/**
 * @file main.cpp
 * @brief Vibration motor test application (ESP-IDF + FreeRTOS).
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "vibration.h"

static const char *TAG = "VIB_TEST";

#ifndef VIB_PIN
#define VIB_PIN 4
#endif

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== Vibration Motor Test ===");
    ESP_LOGI(TAG, "Motor pin: GPIO %d", VIB_PIN);

    Vibration motor((gpio_num_t)VIB_PIN);
    motor.init();

    ESP_LOGI(TAG, "Starting tests in 1 second...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        // --- Test 1: Basic vibration ---
        ESP_LOGI(TAG, "[1] Basic vibrate: 500ms, 100%% intensity");
        motor.vibrate(500, 100);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 2: Intensity levels ---
        ESP_LOGI(TAG, "[2] Intensity: 50%% -> 75%% -> 100%%");

        ESP_LOGI(TAG, "    Weak (50%%)");
        motor.vibrate(400, 50);
        vTaskDelay(pdMS_TO_TICKS(800));

        ESP_LOGI(TAG, "    Medium (75%%)");
        motor.vibrate(400, 75);
        vTaskDelay(pdMS_TO_TICKS(800));

        ESP_LOGI(TAG, "    Strong (100%%)");
        motor.vibrate(400, 100);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 3: Preset tap ---
        ESP_LOGI(TAG, "[3] Preset: tap");
        motor.tap();
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 4: Preset double tap ---
        ESP_LOGI(TAG, "[4] Preset: double tap");
        motor.doubleTap();
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 5: Preset triple tap ---
        ESP_LOGI(TAG, "[5] Preset: triple tap");
        motor.tripleTap();
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 6: Preset heartbeat ---
        ESP_LOGI(TAG, "[6] Preset: heartbeat");
        motor.heartbeat();
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 7: Preset alarm ---
        ESP_LOGI(TAG, "[7] Preset: alarm");
        motor.alarm();
        vTaskDelay(pdMS_TO_TICKS(4000));

        // --- Test 8: Preset pulse ---
        ESP_LOGI(TAG, "[8] Preset: pulse (gentle ramp)");
        motor.pulse();
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 9: Custom pattern ---
        ESP_LOGI(TAG, "[9] Custom pattern: SOS (... --- ...)");
        VibrationStep sos[] = {
            // S: three short (dots)
            { 120, 100 }, { 150, 0 },
            { 120, 100 }, { 150, 0 },
            { 120, 100 }, { 300, 0 },   // Longer gap between letters
            // O: three long (dashes)
            { 350, 100 }, { 150, 0 },
            { 350, 100 }, { 150, 0 },
            { 350, 100 }, { 300, 0 },   // Longer gap between letters
            // S: three short (dots)
            { 120, 100 }, { 150, 0 },
            { 120, 100 }, { 150, 0 },
            { 120, 100 },
        };
        motor.playPattern(sos, sizeof(sos) / sizeof(sos[0]));
        vTaskDelay(pdMS_TO_TICKS(5000));

        // --- Test 10: Stop test ---
        ESP_LOGI(TAG, "[10] Stop test: start 3s vibration, cut after 1s");
        motor.vibrate(3000, 80);
        vTaskDelay(pdMS_TO_TICKS(1000));
        motor.stop();
        ESP_LOGI(TAG, "    Stopped. Should be still now.");
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 11: Indefinite vibrate + stop ---
        ESP_LOGI(TAG, "[11] Indefinite vibrate (duration=0), stop after 2s");
        motor.vibrate(0, 80);
        vTaskDelay(pdMS_TO_TICKS(2000));
        motor.stop();
        ESP_LOGI(TAG, "    Stopped.");
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "=== All tests done! Restarting in 3s... ===");
        ESP_LOGI(TAG, "");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
