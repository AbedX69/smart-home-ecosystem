/**
 * @file main.cpp
 * @brief Bench test for SmartLightRemote: 2 panels, touch + encoder per side.
 *
 * Wires:
 *   - 2x SmartLightRemote   (from devices/modules/smart_light)
 *   - 2x TouchSensor        (toggle on touch)
 *   - 2x RotaryEncoder      (rotate = adjust, button = cycle mode)
 *   - 2x GC9A01 displays    (shared SPI bus)
 *
 * INPUT MAPPING
 *   Touch tap     → toggle panel on/off
 *   Encoder press → cycle mode: BRIGHTNESS → COLOR → WHITE
 *   Encoder rotate (panel on):
 *       BRIGHTNESS → 0..100
 *       COLOR      → hue, 5° per detent
 *       WHITE      → 0..100
 *   Encoder rotate (panel off): ignored.
 *
 * HARDWARE (ESP32-S3):
 *   Touch 1:    GPIO 4         Touch 2:    GPIO 5
 *   Encoder 1:  CLK=6, DT=7, SW=15
 *   Encoder 2:  CLK=16, DT=17, SW=18
 *   SPI:        MOSI=11, SCK=12
 *   Display 1:  CS=38, DC=39, RST=40, BLK=3
 *   Display 2:  CS=21, DC=47, RST=48
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "gc9a01.h"
#include "touch.h"
#include "encoder.h"
#include "smart_light_remote.h"

static const char* TAG = "test_smart_light";


/* ─── Pin map ────────────────────────────────────────────────────────────── */

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


/* ─── Per-channel input handler ──────────────────────────────────────────── */
/*
 * Pulls pending events from one (touch, encoder) pair and applies them to
 * the matching SmartLightRemote. Returns true if state changed (caller
 * should re-render).
 */
static bool handleInputs(int idx,
                         TouchSensor& touch,
                         RotaryEncoder& enc,
                         int32_t& lastPos,
                         SmartLightRemote& panel)
{
    bool dirty = false;

    if (touch.wasTouched()) {
        panel.toggle();
        dirty = true;
        ESP_LOGI(TAG, "Panel %d toggled: %s", idx + 1, panel.isOn() ? "ON" : "OFF");
    }

    if (enc.wasButtonPressed()) {
        panel.cycleMode();
        dirty = true;
        const char* names[] = { "BRIGHTNESS", "COLOR", "WHITE" };
        ESP_LOGI(TAG, "Panel %d mode: %s", idx + 1, names[(int)panel.mode()]);
    }

    int32_t pos   = enc.getPosition();
    int32_t delta = pos - lastPos;
    if (delta != 0) {
        lastPos = pos;
        if (panel.isOn()) {
            switch (panel.mode()) {
                case SmartLightMode::BRIGHTNESS: panel.adjustBrightness(delta);  break;
                case SmartLightMode::COLOR:      panel.adjustHue(delta * 5);     break;
                case SmartLightMode::WHITE:      panel.adjustWhite(delta);       break;
            }
            dirty = true;
        }
    }

    return dirty;
}


/* ─── app_main ───────────────────────────────────────────────────────────── */

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Smart-light bench test starting...");

    /* 1. Build the angle LUT used by the panel renderer (once). */
    SmartLightRemote::buildAngleLUT();

    /* 2. Displays (shared SPI). */
    GC9A01 tft0(SPI_MOSI, SPI_SCK, GC1_CS, GC1_DC, GC1_RST, GC_BLK,        SPI2_HOST);
    GC9A01 tft1(SPI_MOSI, SPI_SCK, GC2_CS, GC2_DC, GC2_RST, GPIO_NUM_NC,   SPI2_HOST);
    if (!tft0.init()) ESP_LOGE(TAG, "TFT 0 init failed");
    if (!tft1.init()) ESP_LOGE(TAG, "TFT 1 init failed");

    /* 3. Inputs. */
    TouchSensor   touch0(TOUCH1_PIN, true);
    TouchSensor   touch1(TOUCH2_PIN, true);
    RotaryEncoder enc0(ENC1_CLK, ENC1_DT, ENC1_SW);
    RotaryEncoder enc1(ENC2_CLK, ENC2_DT, ENC2_SW);
    touch0.init();
    touch1.init();
    enc0.init();
    enc1.init();

    /* 4. Two panels, one per display. */
    SmartLightRemote panel0(tft0, 0);
    SmartLightRemote panel1(tft1, 1);

    int32_t lastEnc0 = 0;
    int32_t lastEnc1 = 0;

    /* 5. Initial paint. */
    panel0.invalidate();
    panel1.invalidate();
    panel0.render();
    panel1.render();

    ESP_LOGI(TAG, "Entering main loop...");

    while (true) {
        touch0.update();
        touch1.update();

        bool dirty0 = handleInputs(0, touch0, enc0, lastEnc0, panel0);
        bool dirty1 = handleInputs(1, touch1, enc1, lastEnc1, panel1);

        if (dirty0) panel0.render();
        if (dirty1) panel1.render();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
