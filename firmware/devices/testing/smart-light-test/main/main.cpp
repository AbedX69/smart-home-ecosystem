/**
 * @file main.cpp
 * @brief Bench test: 2 self-contained SmartLightRemote panels + 2 SK6812 strips.
 *
 * One ESP32-S3 runs both the remote UI (encoder + touch + GC9A01) and the
 * device hardware (LED strips). No wireless — each loop syncs remote -> device.
 *
 * Per channel:
 *   Touch tap     -> toggle on/off
 *   Encoder press -> cycle mode: BRIGHTNESS -> COLOR -> WHITE
 *   Encoder turn  -> adjust the active mode value (ignored while off)
 *
 * HARDWARE (ESP32-S3):
 *   Touch 1:      GPIO 4          Touch 2:    GPIO 5
 *   Encoder 1:    CLK=6,  DT=7,  SW=15
 *   Encoder 2:    CLK=16, DT=17, SW=18
 *   SPI (shared): MOSI=11, SCK=12
 *   Display 1:    CS=38, DC=39, RST=40, BLK=3
 *   Display 2:    CS=21, DC=47, RST=48     (shares the bus + backlight)
 *   Strip 1:      GPIO 41   (SK6812 RGBW, 144 LEDs)
 *   Strip 2:      GPIO 42   (SK6812 RGBW, 144 LEDs)
 *
 * Pins are all defined below — change this block per board (C6/D use lower GPIOs;
 * 38-48 don't exist there). The classes take pins as args, nothing is hardcoded.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/gpio.h>

#include "gc9a01.h"
#include "smart_light_remote.h"
#include "smart_light_device.h"

static const char* TAG = "test_smart_light";


/* ─── Pin map (edit per board) ───────────────────────────────────────────── */

#define TOUCH1_PIN   GPIO_NUM_4
#define TOUCH2_PIN   GPIO_NUM_5

#define ENC1_CLK     GPIO_NUM_6
#define ENC1_DT      GPIO_NUM_7
#define ENC1_SW      GPIO_NUM_15

#define ENC2_CLK     GPIO_NUM_16
#define ENC2_DT      GPIO_NUM_17
#define ENC2_SW      GPIO_NUM_18

#define SPI_MOSI     GPIO_NUM_11
#define SPI_SCK      GPIO_NUM_12

#define GC1_CS       GPIO_NUM_38
#define GC1_DC       GPIO_NUM_39
#define GC1_RST      GPIO_NUM_40

#define GC2_CS       GPIO_NUM_21
#define GC2_DC       GPIO_NUM_47
#define GC2_RST      GPIO_NUM_48

#define GC_BLK       GPIO_NUM_3

#define STRIP1_PIN   GPIO_NUM_41
#define STRIP2_PIN   GPIO_NUM_42
#define NUM_LEDS     14


/* ─── Sync: copy remote panel state -> device strip, then push to HW ─────── */

static void syncToDevice(SmartLightRemote& panel, SmartLightDevice& device) {
    device.setOn(panel.isOn());
    device.setBrightness(panel.brightness());
    device.setHue(panel.hue());
    device.setWhite(panel.whiteBright());
    device.update();
}


/* ─── app_main ───────────────────────────────────────────────────────────── */

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Smart-light bench test starting (wired)...");

    /* Angle LUT for the panel renderer. */
    SmartLightRemote::buildAngleLUT();

    /* Displays — shared SPI2 bus, separate CS. Caller owns the SPI host + init. */
    GC9A01 tft0(SPI_MOSI, SPI_SCK, GC1_CS, GC1_DC, GC1_RST, GC_BLK,      SPI2_HOST);
    GC9A01 tft1(SPI_MOSI, SPI_SCK, GC2_CS, GC2_DC, GC2_RST, GPIO_NUM_NC, SPI2_HOST);
    if (!tft0.init()) ESP_LOGE(TAG, "TFT 0 init failed");
    if (!tft1.init()) ESP_LOGE(TAG, "TFT 1 init failed");

    /* Remotes — each owns its encoder + touch + display reference + state. */
    SmartLightRemote panel0(tft0, 0, ENC1_CLK, ENC1_DT, ENC1_SW, TOUCH1_PIN);
    SmartLightRemote panel1(tft1, 1, ENC2_CLK, ENC2_DT, ENC2_SW, TOUCH2_PIN);
    panel0.init();
    panel1.init();

    /* Devices — the LED strips (device-side hardware). */
    SmartLightDevice strip0(STRIP1_PIN, NUM_LEDS);
    SmartLightDevice strip1(STRIP2_PIN, NUM_LEDS);
    if (!strip0.init()) ESP_LOGE(TAG, "Strip 0 init failed");
    if (!strip1.init()) ESP_LOGE(TAG, "Strip 1 init failed");
    ESP_LOGI(TAG, "SK6812 RGBW strips initialized (2x %d LEDs)", NUM_LEDS);

    /* Initial paint + sync. */
    panel0.render();
    panel1.render();
    syncToDevice(panel0, strip0);
    syncToDevice(panel1, strip1);

    ESP_LOGI(TAG, "Entering main loop...");

    while (true) {
        if (panel0.update()) syncToDevice(panel0, strip0);
        if (panel1.update()) syncToDevice(panel1, strip1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}