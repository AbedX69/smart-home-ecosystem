/**
 * @file main.cpp
 * @brief HUB (ESP32-S3 WROOM) — wireless POC.
 *
 * Same two SmartLightRemote panels as the wired bench test, strips removed
 * from this board. Panel 0's state is pushed over ESP-NOW to the C6 strip
 * node as a 5-byte LightStatePayload. Panel 1 is UI-only until node #2
 * exists.
 *
 * FLASH ORDER:
 *   1. Flash the C6 node first — it prints "This device MAC: XX:.." on boot.
 *   2. Paste that MAC into STRIP_MAC below.   <-- the ONE thing to fill
 *   3. Flash this (env: s3_wroom).
 *
 * No channel config: neither board joins a router, both sit on default ch1.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/gpio.h>

#include "gc9a01.h"
#include "smart_light_remote.h"
#include "esp_now_manager.h"

static const char* TAG = "hub";


/* ─── Pin map (unchanged from wired bench, strip pins gone) ──────────────── */

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


/* ─── Strip node address ─────────────────────────────────────────────────── */
/* TODO: flash the C6 first, copy the MAC it prints on boot. */
static const uint8_t STRIP_MAC[6] = {0xB4, 0x3A, 0x45, 0x8A, 0x81, 0x74};

/* ─── Wire format — keep byte-identical with the node ────────────────────── */

typedef struct __attribute__((packed)) {
    uint8_t  on;           /* 0 / 1           */
    uint8_t  brightness;   /* 0..100 (RGB)    */
    uint16_t hue;          /* 0..359          */
    uint8_t  white;        /* 0..100 (W chan) */
} LightStatePayload;       /* 5 bytes */


/* ─── Sync: panel state -> radio (was syncToDevice in the wired test) ────── */

static void sendState(const SmartLightRemote& panel) {
    LightStatePayload pl;
    pl.on         = panel.isOn() ? 1 : 0;
    pl.brightness = panel.brightness();
    pl.hue        = panel.hue();
    pl.white      = panel.whiteBright();

    EspNowManager::instance().send(
        STRIP_MAC, reinterpret_cast<const uint8_t*>(&pl), sizeof(pl));
}


/* ─── app_main ───────────────────────────────────────────────────────────── */

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Hub starting (wireless POC)...");

    SmartLightRemote::buildAngleLUT();

    /* Displays — shared SPI2 bus, separate CS. */
    GC9A01 tft0(SPI_MOSI, SPI_SCK, GC1_CS, GC1_DC, GC1_RST, GC_BLK,      SPI2_HOST);
    GC9A01 tft1(SPI_MOSI, SPI_SCK, GC2_CS, GC2_DC, GC2_RST, GPIO_NUM_NC, SPI2_HOST);
    if (!tft0.init()) ESP_LOGE(TAG, "TFT 0 init failed");
    if (!tft1.init()) ESP_LOGE(TAG, "TFT 1 init failed");

    /* Remotes — each owns its encoder + touch + display ref + state. */
    SmartLightRemote panel0(tft0, 0, ENC1_CLK, ENC1_DT, ENC1_SW, TOUCH1_PIN);
    SmartLightRemote panel1(tft1, 1, ENC2_CLK, ENC2_DT, ENC2_SW, TOUCH2_PIN);
    panel0.init();
    panel1.init();

    /* Radio up + peer the node. */
    EspNowManager& enm = EspNowManager::instance();
    enm.setSendCallback([](const uint8_t*, bool ok) {
        if (!ok) ESP_LOGW(TAG, "TX not ACKed: node off / wrong MAC / channel");
    });
    if (enm.begin() != ESP_OK) ESP_LOGE(TAG, "ESP-NOW init failed");
    enm.addPeer(STRIP_MAC);

    /* Initial paint + push. */
    panel0.render();
    panel1.render();
    sendState(panel0);

    ESP_LOGI(TAG, "Entering main loop...");

    uint32_t keepalive = 0;
    while (true) {
        if (panel0.update()) sendState(panel0);
        panel1.update();                        /* UI-only for now */

        if (++keepalive >= 100) {               /* ~1 s: covers node reboot */
            keepalive = 0;
            sendState(panel0);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}