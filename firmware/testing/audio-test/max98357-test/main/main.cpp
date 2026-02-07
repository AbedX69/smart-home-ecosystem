/**
 * @file main.cpp
 * @brief MAX98357 I2S audio amplifier test (ESP-IDF).
 *
 * @details
 * Demonstrates the MAX98357 component:
 * - I2S initialization
 * - Tone generation
 * - Simple melody playback
 * - Volume control
 */

#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "max98357.h"


static const char* TAG = "MAX98357_TEST";


#ifndef MAX98357_DIN
#define MAX98357_DIN 25
#endif
#ifndef MAX98357_BCLK
#define MAX98357_BCLK 26
#endif
#ifndef MAX98357_LRC
#define MAX98357_LRC 27
#endif


/*
 * =============================================================================
 * MUSICAL NOTES (frequencies in Hz)
 * =============================================================================
 */
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define NOTE_REST 0


extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== MAX98357 Audio Test ===");
    ESP_LOGI(TAG, "DIN=%d, BCLK=%d, LRC=%d", MAX98357_DIN, MAX98357_BCLK, MAX98357_LRC);
    
    /*
     * =========================================================================
     * CREATE AND INITIALIZE AMPLIFIER
     * =========================================================================
     */
    MAX98357 amp(
        (gpio_num_t)MAX98357_DIN,
        (gpio_num_t)MAX98357_BCLK,
        (gpio_num_t)MAX98357_LRC
    );
    
    if (!amp.init()) {
        ESP_LOGE(TAG, "Amplifier init failed!");
        return;
    }
    
    ESP_LOGI(TAG, "Amplifier initialized. Running tests...");
    vTaskDelay(pdMS_TO_TICKS(500));
      while (1) {
    /*
     * =========================================================================
     * TEST 1: Simple beep
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 1: Beep");
    
    amp.beep(200);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    amp.beep(200);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    amp.beep(400);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    /*
     * =========================================================================
     * TEST 2: Frequency sweep
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 2: Frequency sweep (200Hz - 2000Hz)");
    
    for (int freq = 200; freq <= 2000; freq += 100) {
        ESP_LOGI(TAG, "  %d Hz", freq);
        amp.playTone(freq, 100, 0.3f);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    /*
     * =========================================================================
     * TEST 3: Volume levels
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 3: Volume levels");
    
    float volumes[] = {0.1f, 0.25f, 0.5f, 0.75f, 1.0f};
    const char* volNames[] = {"10%", "25%", "50%", "75%", "100%"};
    
    for (int i = 0; i < 5; i++) {
        ESP_LOGI(TAG, "  Volume: %s", volNames[i]);
        amp.playTone(440, 300, volumes[i]);
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    /*
     * =========================================================================
     * TEST 4: Musical scale
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 4: Musical scale (C major)");
    
    int scale[] = {NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4, NOTE_G4, NOTE_A4, NOTE_B4, NOTE_C5};
    const char* noteNames[] = {"C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5"};
    
    for (int i = 0; i < 8; i++) {
        ESP_LOGI(TAG, "  %s (%d Hz)", noteNames[i], scale[i]);
        amp.playTone(scale[i], 250, 0.4f);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    /*
     * =========================================================================
     * TEST 5: Simple melody - "Twinkle Twinkle Little Star"
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 5: Melody - Twinkle Twinkle Little Star");
    
    struct Note {
        int freq;
        int duration;
    };
    
    Note melody[] = {
        {NOTE_C4, 400}, {NOTE_C4, 400}, {NOTE_G4, 400}, {NOTE_G4, 400},
        {NOTE_A4, 400}, {NOTE_A4, 400}, {NOTE_G4, 800},
        {NOTE_F4, 400}, {NOTE_F4, 400}, {NOTE_E4, 400}, {NOTE_E4, 400},
        {NOTE_D4, 400}, {NOTE_D4, 400}, {NOTE_C4, 800},
        {NOTE_G4, 400}, {NOTE_G4, 400}, {NOTE_F4, 400}, {NOTE_F4, 400},
        {NOTE_E4, 400}, {NOTE_E4, 400}, {NOTE_D4, 800},
        {NOTE_G4, 400}, {NOTE_G4, 400}, {NOTE_F4, 400}, {NOTE_F4, 400},
        {NOTE_E4, 400}, {NOTE_E4, 400}, {NOTE_D4, 800},
        {NOTE_C4, 400}, {NOTE_C4, 400}, {NOTE_G4, 400}, {NOTE_G4, 400},
        {NOTE_A4, 400}, {NOTE_A4, 400}, {NOTE_G4, 800},
        {NOTE_F4, 400}, {NOTE_F4, 400}, {NOTE_E4, 400}, {NOTE_E4, 400},
        {NOTE_D4, 400}, {NOTE_D4, 400}, {NOTE_C4, 800},
    };
    
    int melodyLength = sizeof(melody) / sizeof(Note);
    
    for (int i = 0; i < melodyLength; i++) {
        if (melody[i].freq > 0) {
            amp.playTone(melody[i].freq, melody[i].duration - 50, 0.4f);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    /*
     * =========================================================================
     * TEST 6: Alarm sound
     * =========================================================================
     */
    ESP_LOGI(TAG, "Test 6: Alarm sound");
    
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 5; j++) {
            amp.playTone(800, 100, 0.5f);
            vTaskDelay(pdMS_TO_TICKS(100));
            amp.playTone(600, 100, 0.5f);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    
    /*
     * =========================================================================
     * COMPLETE
     * =========================================================================
     */
    ESP_LOGI(TAG, "All tests complete!");
    
    // Final confirmation beeps
    vTaskDelay(pdMS_TO_TICKS(500));
    amp.playTone(NOTE_C5, 150, 0.3f);
    vTaskDelay(pdMS_TO_TICKS(100));
    amp.playTone(NOTE_E5, 150, 0.3f);
    vTaskDelay(pdMS_TO_TICKS(100));
    amp.playTone(NOTE_G5, 300, 0.3f);
    
  
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
