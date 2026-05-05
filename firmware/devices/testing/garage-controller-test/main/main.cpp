/**
 * @file main.cpp
 * @brief Bench test for GarageDoorDevice — 2 buttons + 2 relays on one ESP32.
 *
 * Wires:
 *   - 1x GarageDoorDevice (from devices/modules/garage_controller)
 *
 * INPUT MAPPING (matches device's internal state machine)
 *   UP press    → start opening (or stop if moving)
 *   DOWN press  → start closing (or stop if moving)
 *   60s travel timeout → IDLE_OPEN / IDLE_CLOSED
 *
 * HARDWARE
 *   UP relay:    GPIO 4
 *   DOWN relay:  GPIO 5
 *   UP button:   GPIO 18
 *   DOWN button: GPIO 19
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "garage_door_device.h"

static const char* TAG = "test_garage";


/* ─── Pin map ────────────────────────────────────────────────────────────── */

#define UP_RELAY_PIN     GPIO_NUM_4
#define DOWN_RELAY_PIN   GPIO_NUM_5
#define UP_BTN_PIN       GPIO_NUM_18
#define DOWN_BTN_PIN     GPIO_NUM_19


extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Garage-door bench test starting...");

    GarageDoorDevice door(UP_RELAY_PIN, DOWN_RELAY_PIN,
                          UP_BTN_PIN,   DOWN_BTN_PIN,
                          /*relay_active_low=*/true);
    if (!door.init()) {
        ESP_LOGE(TAG, "Init failed");
        return;
    }

    ESP_LOGI(TAG, "Entering main loop...");

    GarageState last_logged = GarageState::STOPPED_MID;

    while (true) {
        door.update();

        /* Log state transitions so you can watch them on serial. */
        if (door.state() != last_logged) {
            ESP_LOGI(TAG, "State -> %s", door.stateStr());
            last_logged = door.state();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
