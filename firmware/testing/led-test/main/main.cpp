#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"


static const char *TAG = "LED_TEST";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting LED test on pin %d", LED_PIN);
    
    // Configure GPIO
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << LED_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    
    ESP_LOGI(TAG, "GPIO configured. Starting blink...");
    
    bool state = false;
    while(1) {
        gpio_set_level((gpio_num_t)LED_PIN, state ? 1 : 0);
        ESP_LOGI(TAG, "LED %s", state ? "ON" : "OFF");
        state = !state;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}