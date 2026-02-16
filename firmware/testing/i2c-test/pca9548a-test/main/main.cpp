/**
 * @file main.cpp
 * @brief PCA9548A I2C multiplexer test (ESP-IDF).
 *
 * @details
 * Demonstrates the PCA9548A component:
 * - Initialization
 * - Channel selection
 * - Device scanning on each channel
 *
 * Connect I2C devices to the multiplexer channels to see them detected.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "pca9548a.h"


static const char* TAG = "PCA9548A_TEST";


#ifndef PCA9548A_SDA
#define PCA9548A_SDA 21
#endif
#ifndef PCA9548A_SCL
#define PCA9548A_SCL 22
#endif


extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== PCA9548A I2C Multiplexer Test ===");
    ESP_LOGI(TAG, "SDA=%d, SCL=%d", PCA9548A_SDA, PCA9548A_SCL);
    
    /*
     * =========================================================================
     * CREATE AND INITIALIZE MULTIPLEXER
     * =========================================================================
     */
    PCA9548A mux(
        (gpio_num_t)PCA9548A_SDA,
        (gpio_num_t)PCA9548A_SCL
    );
    
    if (!mux.init()) {
        ESP_LOGE(TAG, "Multiplexer init failed!");
        ESP_LOGE(TAG, "Check wiring: SDA=%d, SCL=%d, Address=0x70", PCA9548A_SDA, PCA9548A_SCL);
        return;
    }
    
    ESP_LOGI(TAG, "Multiplexer initialized successfully");
    
    /*
     * =========================================================================
     * TEST 1: Channel selection
     * =========================================================================
     */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Test 1: Channel selection");
    
    for (int ch = 0; ch < 8; ch++) {
        if (mux.selectChannel(ch)) {
            uint8_t enabled = mux.getEnabledChannels();
            ESP_LOGI(TAG, "  Selected channel %d (register: 0x%02X)", ch, enabled);
        } else {
            ESP_LOGE(TAG, "  Failed to select channel %d", ch);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    /*
     * =========================================================================
     * TEST 2: Multi-channel enable
     * =========================================================================
     */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Test 2: Multi-channel enable");
    
    // Enable channels 0 and 2 (binary: 00000101 = 0x05)
    if (mux.enableChannels(0x05)) {
        uint8_t enabled = mux.getEnabledChannels();
        ESP_LOGI(TAG, "  Enabled CH0 + CH2 (register: 0x%02X)", enabled);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Enable all channels
    if (mux.enableChannels(0xFF)) {
        uint8_t enabled = mux.getEnabledChannels();
        ESP_LOGI(TAG, "  Enabled ALL channels (register: 0x%02X)", enabled);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Disable all
    if (mux.disableAll()) {
        uint8_t enabled = mux.getEnabledChannels();
        ESP_LOGI(TAG, "  Disabled ALL channels (register: 0x%02X)", enabled);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    /*
     * =========================================================================
     * TEST 3: Scan each channel for I2C devices
     * =========================================================================
     */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Test 3: Scanning all channels for I2C devices");
    ESP_LOGI(TAG, "(Connect devices to multiplexer channels to see them)");
    ESP_LOGI(TAG, "");
    

    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    uint8_t addresses[16];
    int totalDevices = 0;
    
    for (int ch = 0; ch < 8; ch++) {
        ESP_LOGI(TAG, "--- Channel %d ---", ch);
        uint8_t found = mux.scanChannel(ch, addresses, 16);
        
        if (found == 0) {
            ESP_LOGI(TAG, "  No devices found");
        } else {
            for (int i = 0; i < found && i < 16; i++) {
                const char* deviceName = "Unknown";
                
                // Common I2C addresses
                switch (addresses[i]) {
                    case 0x3C:
                    case 0x3D:
                        deviceName = "SSD1306 OLED";
                        break;
                    case 0x27:
                    case 0x3F:
                        deviceName = "PCF8574 (LCD I2C)";
                        break;
                    case 0x50:
                    case 0x51:
                    case 0x52:
                    case 0x53:
                        deviceName = "EEPROM (24Cxx)";
                        break;
                    case 0x68:
                        deviceName = "DS3231 RTC / MPU6050";
                        break;
                    case 0x76:
                    case 0x77:
                        deviceName = "BME280 / BMP280";
                        break;
                    case 0x48:
                    case 0x49:
                    case 0x4A:
                    case 0x4B:
                        deviceName = "ADS1115 ADC";
                        break;
                    case 0x20:
                    case 0x21:
                    case 0x22:
                    case 0x23:
                        deviceName = "PCF8574 GPIO Expander";
                        break;
                }
                
                ESP_LOGI(TAG, "  0x%02X - %s", addresses[i], deviceName);
            }
            totalDevices += found;
        }
        ESP_LOGI(TAG, "");
    }
    
    /*
     * =========================================================================
     * SUMMARY
     * =========================================================================
     */
    ESP_LOGI(TAG, "=== Scan Complete ===");
    ESP_LOGI(TAG, "Total devices found: %d", totalDevices);
    
    if (totalDevices == 0) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "No devices detected. To test properly:");
        ESP_LOGI(TAG, "1. Connect an I2C device (e.g., SSD1306) to channel 0");
        ESP_LOGI(TAG, "   - SD0 -> Device SDA");
        ESP_LOGI(TAG, "   - SC0 -> Device SCL");
        ESP_LOGI(TAG, "   - Also connect VCC and GND to the device");
        ESP_LOGI(TAG, "2. Re-run this test");
    }
    
    /*
     * =========================================================================
     * CONTINUOUS SCAN (for testing hot-plug)
     * =========================================================================
     */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Starting continuous scan (every 5 seconds)...");
    ESP_LOGI(TAG, "Connect/disconnect devices to see changes.");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "--- Quick scan ---");
        
        for (int ch = 0; ch < 8; ch++) {
            uint8_t found = mux.scanChannel(ch, addresses, 16);
            if (found > 0) {
                ESP_LOGI(TAG, "CH%d: %d device(s)", ch, found);
                for (int i = 0; i < found && i < 16; i++) {
                    ESP_LOGI(TAG, "  -> 0x%02X", addresses[i]);
                }
            }
        }
    }
}
