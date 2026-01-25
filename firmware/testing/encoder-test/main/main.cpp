#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "encoder.h"

static const char *TAG = "ENCODER_TEST";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== Rotary Encoder Test (Interrupt-Driven) ===");
    
    // Create encoder instance
    RotaryEncoder encoder(
        (gpio_num_t)ENCODER_CLK,  // Channel A
        (gpio_num_t)ENCODER_DT,   // Channel B
        (gpio_num_t)ENCODER_SW,    // Button
        true
        
    );
    
    // Initialize with interrupts
    encoder.init();
    
    ESP_LOGI(TAG, "Ready! Turn the encoder and press the button");
    ESP_LOGI(TAG, "Note: Position updates happen automatically via interrupts!");
    
    int32_t lastPos = 0;
    
    while(1) {
        // Get position (updated automatically by interrupts)
        int32_t pos = encoder.getPosition();
        
        // Only log when position changes
        if(pos != lastPos) {
            int32_t delta = pos - lastPos;
            ESP_LOGI(TAG, "Position: %ld (delta: %+ld)", pos, delta);
            lastPos = pos;
        }
        
        // Check for button press (with debouncing)
        if(encoder.wasButtonPressed()) {
            ESP_LOGI(TAG, ">>> Button PRESSED! Resetting position to 0 <<<");
            encoder.resetPosition();
            lastPos = 0;
        }
        
        // Much longer delay is OK now - interrupts handle rotation!
        // We only need to poll the button
        vTaskDelay(pdMS_TO_TICKS(50));  // 50ms is fine
    }
}
