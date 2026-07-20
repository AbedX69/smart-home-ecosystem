/*
 * main.cpp — garage bench demo with two buzzers.
 *
 *   GPIO 4 = buzzer1 ("power")     ON whenever moving
 *   GPIO 5 = buzzer2 ("direction") ON only when going DOWN
 *   GPIO 18 = button (to GND)
 *
 *   UP   = one tone (1500 Hz)
 *   DOWN = two tones together (1500 + 500 Hz)
 *   STOP = silence
 *
 * buzzer1 uses LEDC channel 0 / timer 0 (the defaults).
 * buzzer2 gets channel 1 / timer 1 so both can play at once.
 *
 * Going to relays later: swap Buzzer -> Relay, tone(...) -> on(), stop() -> off().
 */
#define GARAGE_TRAVEL_MS 2000   /* short so the timeout is watchable on a bench */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "garage_door_device.h"
#include "buzzer.h"

static const char* TAG = "demo";

#define BTN_PIN  GPIO_NUM_18
#define F_POWER  1500    /* buzzer1 pitch */
#define F_DIR     500    /* buzzer2 pitch */
#define DIR_SETTLE_MS 300   /* dead-time: let direction settle before power */


extern "C" void app_main(void) {
    Buzzer buzzer1(GPIO_NUM_10);                                /* ch0, timer0 */
    Buzzer buzzer2(GPIO_NUM_11, LEDC_CHANNEL_1, LEDC_TIMER_1);  /* ch1, timer1 */
    buzzer1.init();
    buzzer2.init();

    GarageDoorDevice door(BTN_PIN);
    door.init();

    ESP_LOGI(TAG, "Press the button.");

    GarageState last = door.state();
    while (true) {
        door.update();

        if (door.state() != last) {
            switch (door.state()) {
                case GarageState::MOVING_UP:
                    buzzer2.stop();                          /* direction = UP (off) first */
                    vTaskDelay(pdMS_TO_TICKS(DIR_SETTLE_MS));
                    buzzer1.tone(F_POWER, 0, 100);           /* then power */
                    break;
                case GarageState::MOVING_DOWN:
                    buzzer2.tone(F_DIR, 0, 100);             /* direction = DOWN (on) first */
                    vTaskDelay(pdMS_TO_TICKS(DIR_SETTLE_MS));
                    buzzer1.tone(F_POWER, 0, 100);           /* then power */
                    break;
                default:                                      /* stopped / idle */
                    buzzer1.stop();                           /* power OFF first */
                    buzzer2.stop();                           /* then direction */
                    break;
            }
            ESP_LOGI(TAG, "%s", door.stateStr());
            last = door.state();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}