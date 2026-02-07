/**
 * @file main.cpp
 * @brief Buzzer test application (ESP-IDF + FreeRTOS).
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "buzzer.h"

static const char *TAG = "BUZZER_TEST";

#ifndef BUZZER_PIN
#define BUZZER_PIN 4
#endif

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== Buzzer Test ===");
    ESP_LOGI(TAG, "Buzzer pin: GPIO %d", BUZZER_PIN);

    Buzzer buzzer((gpio_num_t)BUZZER_PIN);
    buzzer.init();

    ESP_LOGI(TAG, "Starting tests in 1 second...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        // --- Test 1: Basic tone ---
        ESP_LOGI(TAG, "[1] Basic tone: 1kHz, 500ms, 50%% volume");
        buzzer.tone(1000, 500, 50);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 2: Volume levels ---
        ESP_LOGI(TAG, "[2] Volume: 20%% -> 50%% -> 100%%");

        ESP_LOGI(TAG, "    Quiet (20%%)");
        buzzer.tone(2000, 400, 20);
        vTaskDelay(pdMS_TO_TICKS(800));

        ESP_LOGI(TAG, "    Medium (50%%)");
        buzzer.tone(2000, 400, 50);
        vTaskDelay(pdMS_TO_TICKS(800));

        ESP_LOGI(TAG, "    Loud (100%%)");
        buzzer.tone(2000, 400, 100);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 3: Musical notes ---
        ESP_LOGI(TAG, "[3] Notes: C4 (262Hz), E4 (330Hz), G4 (392Hz)");
        buzzer.tone(262, 400, 50);
        vTaskDelay(pdMS_TO_TICKS(800));
        buzzer.tone(330, 400, 50);
        vTaskDelay(pdMS_TO_TICKS(800));
        buzzer.tone(392, 400, 50);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 4: Log sweep up ---
        ESP_LOGI(TAG, "[4] Sweep up: 300Hz -> 3000Hz");
        buzzer.sweepLog(300, 3000, 1500, 60, 10);
        vTaskDelay(pdMS_TO_TICKS(2500));

        // --- Test 5: Log sweep down ---
        ESP_LOGI(TAG, "[5] Sweep down: 3000Hz -> 300Hz");
        buzzer.sweepLog(3000, 300, 1500, 60, 10);
        vTaskDelay(pdMS_TO_TICKS(2500));

        // --- Test 6: Beep ---
        ESP_LOGI(TAG, "[6] Preset: beep");
        buzzer.beep();
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 7: Chirp ---
        ESP_LOGI(TAG, "[7] Preset: chirp (R2D2)");
        buzzer.chirp();
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 8: Alarm ---
        ESP_LOGI(TAG, "[8] Preset: alarm");
        buzzer.alarm();
        vTaskDelay(pdMS_TO_TICKS(2500));

        // --- Test 9: Success ---
        ESP_LOGI(TAG, "[9] Preset: success");
        buzzer.success();
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 10: Error ---
        ESP_LOGI(TAG, "[10] Preset: error");
        buzzer.error();
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 11: Click (x3) ---
        ESP_LOGI(TAG, "[11] Preset: click (x3, listen carefully)");
        buzzer.click();
        vTaskDelay(pdMS_TO_TICKS(500));
        buzzer.click();
        vTaskDelay(pdMS_TO_TICKS(500));
        buzzer.click();
        vTaskDelay(pdMS_TO_TICKS(2000));

        // --- Test 12: Custom melody ---
        ESP_LOGI(TAG, "[12] Melody: Shave and a Haircut");
        BuzzerNote melody[] = {
            { 523, 200, 55 },   // C5  "Shave"
            { 392, 150, 55 },   // G4  "and"
            { 392, 150, 55 },   // G4  "a"
            { 440, 200, 55 },   // A4  "hair"
            { 392, 300, 55 },   // G4  "cut"
            {   0, 300,  0 },   // rest
            { 494, 200, 55 },   // B4  "two"
            { 523, 300, 60 },   // C5  "bits!"
        };
        buzzer.playMelody(melody, sizeof(melody) / sizeof(melody[0]), 20);
        vTaskDelay(pdMS_TO_TICKS(3000));

        // --- Test 13: Stop test ---
        ESP_LOGI(TAG, "[13] Stop test: start 3s tone, cut after 1s");
        buzzer.tone(800, 3000, 50);
        vTaskDelay(pdMS_TO_TICKS(1000));
        buzzer.stop();
        ESP_LOGI(TAG, "    Stopped. Should be silent now.");
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "=== All tests done! Restarting in 3s... ===");
        ESP_LOGI(TAG, "");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
