/**
 * @file main.cpp
 * @brief Relay / SSR test application (ESP-IDF + FreeRTOS).
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "relay.h"

static const char *TAG = "RELAY_TEST";

#ifndef RELAY1_PIN
#define RELAY1_PIN 4
#endif

#ifndef RELAY2_PIN
#define RELAY2_PIN 5
#endif

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== Relay / SSR Test ===");
    ESP_LOGI(TAG, "Relay 1 (active LOW):  GPIO %d", RELAY1_PIN);
    ESP_LOGI(TAG, "Relay 2 (active HIGH): GPIO %d", RELAY2_PIN);

    /*
     * Relay 1: Mechanical relay module (active LOW, most common)
     * Relay 2: SSR module (active HIGH)
     *
     * Change the second parameter to match YOUR module:
     *   true  = active LOW  (GPIO LOW  = relay ON)
     *   false = active HIGH (GPIO HIGH = relay ON)
     */
    Relay relay1((gpio_num_t)RELAY1_PIN, true);     // Active LOW
    Relay relay2((gpio_num_t)RELAY2_PIN, false);     // Active HIGH

    relay1.init();
    relay2.init();

    ESP_LOGI(TAG, "Both relays initialized (OFF). Starting tests in 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        // --- Test 1: Basic on/off ---
        ESP_LOGI(TAG, "[1] Relay 1: ON for 2 seconds");
        relay1.on();
        vTaskDelay(pdMS_TO_TICKS(2000));
        relay1.off();
        ESP_LOGI(TAG, "    Relay 1: OFF");
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 2: Second relay ---
        ESP_LOGI(TAG, "[2] Relay 2: ON for 2 seconds");
        relay2.on();
        vTaskDelay(pdMS_TO_TICKS(2000));
        relay2.off();
        ESP_LOGI(TAG, "    Relay 2: OFF");
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 3: Both on together ---
        ESP_LOGI(TAG, "[3] Both relays ON");
        relay1.on();
        relay2.on();
        vTaskDelay(pdMS_TO_TICKS(2000));
        relay1.off();
        relay2.off();
        ESP_LOGI(TAG, "    Both OFF");
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 4: Toggle ---
        ESP_LOGI(TAG, "[4] Toggle relay 1 three times (1 second each)");
        for (int i = 0; i < 3; i++) {
            relay1.toggle();
            ESP_LOGI(TAG, "    Relay 1: %s", relay1.isOn() ? "ON" : "OFF");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        relay1.off();   // Make sure it ends OFF
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 5: set() with boolean ---
        ESP_LOGI(TAG, "[5] set() test: true, false, true, false");
        relay1.set(true);
        ESP_LOGI(TAG, "    set(true)  → isOn() = %s", relay1.isOn() ? "true" : "false");
        vTaskDelay(pdMS_TO_TICKS(1000));

        relay1.set(false);
        ESP_LOGI(TAG, "    set(false) → isOn() = %s", relay1.isOn() ? "true" : "false");
        vTaskDelay(pdMS_TO_TICKS(1000));

        relay1.set(true);
        ESP_LOGI(TAG, "    set(true)  → isOn() = %s", relay1.isOn() ? "true" : "false");
        vTaskDelay(pdMS_TO_TICKS(1000));

        relay1.set(false);
        ESP_LOGI(TAG, "    set(false) → isOn() = %s", relay1.isOn() ? "true" : "false");
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 6: Alternating ---
        ESP_LOGI(TAG, "[6] Alternating: relay1 ON / relay2 OFF, then swap");
        relay1.on();
        relay2.off();
        ESP_LOGI(TAG, "    R1=ON  R2=OFF");
        vTaskDelay(pdMS_TO_TICKS(1500));

        relay1.off();
        relay2.on();
        ESP_LOGI(TAG, "    R1=OFF R2=ON");
        vTaskDelay(pdMS_TO_TICKS(1500));

        relay2.off();
        ESP_LOGI(TAG, "    Both OFF");
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "=== All tests done! Restarting in 3s... ===");
        ESP_LOGI(TAG, "");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
