#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "encoder.h"

static const char *TAG = "ENCODER_TEST";

extern "C" void app_main(void) {
    RotaryEncoder encoder(
        (gpio_num_t)ENCODER_CLK, 
        (gpio_num_t)ENCODER_DT, 
        (gpio_num_t)ENCODER_SW
    );
    encoder.init();
    
    ESP_LOGI(TAG, "Encoder test started!");
    ESP_LOGI(TAG, "Turn the encoder and press the button");
    
    int lastPos = 0;
    while(1) {
        encoder.update();
        
        int pos = encoder.getPosition();
        if(pos != lastPos) {
            ESP_LOGI(TAG, "Position: %d", pos);
            lastPos = pos;
        }
        
        if(encoder.wasButtonPressed()) {
            ESP_LOGI(TAG, "Button pressed! Resetting position to 0");
            encoder.resetPosition();
            lastPos = 0;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}