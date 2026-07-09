/**
 * @file main.cpp
 * @brief STRIP NODE (Seeed XIAO ESP32-C6) — receive-only ESP-NOW -> SK6812.
 *
 * Receives a 5-byte LightStatePayload from the hub and applies it to the
 * strip. No peers, no TX, no channel config — RX works from any sender.
 *
 * WIRING:
 *   Strip DATA -> D10 (GPIO18)   — next to the 5V/GND corner
 *   Strip GND  -> GND            — common with supply AND the XIAO
 *   Strip 5V   -> bench supply 5V (XIAO 5V pin also fine at 10 LEDs)
 *
 * FLASH ORDER:
 *   Flash THIS first. Boot log prints "This device MAC: XX:..".
 *   Paste that MAC into the hub's STRIP_MAC, then flash the hub.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <cstring>

#include "smart_light_device.h"
#include "esp_now_manager.h"

static const char* TAG = "strip_node";


/* ─── Config ─────────────────────────────────────────────────────────────── */

#define STRIP_PIN   GPIO_NUM_18     /* XIAO C6: D10 */
#define NUM_LEDS    10              /* POC — full strip is 144 */


/* ─── Wire format — keep byte-identical with the hub ─────────────────────── */

typedef struct __attribute__((packed)) {
    uint8_t  on;           /* 0 / 1           */
    uint8_t  brightness;   /* 0..100 (RGB)    */
    uint16_t hue;          /* 0..359          */
    uint8_t  white;        /* 0..100 (W chan) */
} LightStatePayload;       /* 5 bytes */


/* ─── Device ─────────────────────────────────────────────────────────────── */

static SmartLightDevice s_strip(STRIP_PIN, NUM_LEDS);


/* ─── ESP-NOW receive — runs in the manager's rx task, safe to drive HW ──── */

static void onState(const uint8_t* sender_mac, const uint8_t* data, int len) {
    if (len != (int)sizeof(LightStatePayload)) return;    /* not ours */

    LightStatePayload pl;
    memcpy(&pl, data, sizeof(pl));                        /* alignment-safe */

    /* Sanity — reject garbage that happens to be 5 bytes. */
    if (pl.hue > 359 || pl.brightness > 100 || pl.white > 100) return;

    s_strip.setOn(pl.on != 0);
    s_strip.setBrightness(pl.brightness);
    s_strip.setHue(pl.hue);
    s_strip.setWhite(pl.white);
    s_strip.update();

    ESP_LOGI(TAG, "state: on=%u  bri=%u  hue=%u  w=%u",
             pl.on, pl.brightness, pl.hue, pl.white);
}


/* ─── app_main ───────────────────────────────────────────────────────────── */

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Strip node starting (POC, %d LEDs on GPIO %d)...",
             NUM_LEDS, (int)STRIP_PIN);

   if (!s_strip.init()) {
        ESP_LOGE(TAG, "Strip init failed");
        return;
    }

    /* Boot test pattern: solid red 3 s — render path check with radio OFF */
    s_strip.setOn(true);
    s_strip.setBrightness(30);
    s_strip.setHue(0);
    s_strip.setWhite(0);
    s_strip.update();
    vTaskDelay(pdMS_TO_TICKS(3000));


    EspNowManager& enm = EspNowManager::instance();
    enm.setReceiveCallback(onState);

    if (enm.begin() != ESP_OK) {          /* prints this device's MAC */
        ESP_LOGE(TAG, "ESP-NOW init failed");
        return;
    }

    /* Receive-only: everything happens in the callback. */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}