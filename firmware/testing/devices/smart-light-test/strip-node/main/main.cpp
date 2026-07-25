/**
 * @file main.cpp
 * @brief STRIP NODE (Seeed XIAO ESP32-C6) — MessageProtocol v2 + AutoPair.
 *
 * No hardcoded MACs anywhere. On first boot the node broadcasts
 * PAIR_REQUEST until the hub accepts; the pairing persists in NVS, so
 * every later boot goes straight to paired operation.
 *
 * Pairing feedback on the strip itself:
 *   dim blue   = searching for a hub
 *   green 0.8s = paired
 *   red x3     = rejected
 *
 * WIRING: unchanged — DATA -> D10 (GPIO18), common GND, 5V from bench.
 * FLASH ORDER: doesn't matter anymore. Flash both, watch them pair.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include "device_identity.h"
#include "smart_light_device.h"
#include "esp_now_manager.h"
#include "message_protocol.h"
#include "auto_pair.h"
#include "config_store.h"
#include <esp_ota_ops.h>
#include <esp_app_desc.h>

static const char* TAG = "strip_node";

#define STRIP_PIN   GPIO_NUM_18     /* XIAO C6: D10 */
#define NUM_LEDS    10              /* POC — full strip is 144 */

static SmartLightDevice s_strip(STRIP_PIN, NUM_LEDS);


/* ─── Pairing feedback rendered on the strip ─────────────────────────────── */

static void showPairLED(PairLED pattern) {
    switch (pattern) {
        case PairLED::FAST_BLINK:                       /* searching */
            s_strip.setOn(true);
            s_strip.setBrightness(15);
            s_strip.setHue(220);                        /* blue */
            s_strip.setWhite(0);
            s_strip.update();
            break;

        case PairLED::SOLID_ON:                         /* paired */
            s_strip.setOn(true);
            s_strip.setBrightness(40);
            s_strip.setHue(120);                        /* green */
            s_strip.setWhite(0);
            s_strip.update();
            break;

        case PairLED::TRIPLE_BLINK:                     /* rejected */
            for (int i = 0; i < 3; i++) {
                s_strip.setOn(true);
                s_strip.setBrightness(40);
                s_strip.setHue(0);                      /* red */
                s_strip.setWhite(0);
                s_strip.update();
                vTaskDelay(pdMS_TO_TICKS(150));
                s_strip.setOn(false);
                s_strip.update();
                vTaskDelay(pdMS_TO_TICKS(150));
            }
            break;

        case PairLED::OFF:
        default:
            s_strip.setOn(false);
            s_strip.update();
            break;
    }
}


/* ─── Command handler ────────────────────────────────────────────────────── */

static AckStatus onCommand(CmdId cmd, const uint8_t* payload, uint8_t len,
                           const uint8_t src_mac[6]) {

    /* Pairing traffic goes to AutoPair */
    if (AutoPair::handlesCmd(cmd)) {
        AutoPair::instance().processPairMessage(cmd, payload, len, src_mac);
        return AckStatus::OK;
    }

    switch (cmd) {

        case CmdId::SET_LIGHT_STATE: {
            LightStatePayload st;
            if (!msgDecodeLightState(payload, len, st)) return AckStatus::FAIL;
            s_strip.setOn(st.on);
            s_strip.setBrightness(st.brightness);
            s_strip.setHue(st.hue);
            s_strip.setWhite(st.white);
            s_strip.update();
            return AckStatus::OK;
        }

        case CmdId::ON:
            s_strip.setOn(true);
            s_strip.update();
            return AckStatus::OK;

        case CmdId::OFF:
            s_strip.setOn(false);
            s_strip.update();
            return AckStatus::OK;

        case CmdId::TOGGLE:
            s_strip.setOn(!s_strip.isOn());
            s_strip.update();
            return AckStatus::OK;

        case CmdId::IDENTIFY: {
            /* White flash x3, then restore previous state */
            bool     on  = s_strip.isOn();
            uint8_t  bri = s_strip.brightness();
            uint16_t hue = s_strip.hue();
            uint8_t  w   = s_strip.whiteBright();
            for (int i = 0; i < 3; i++) {
                s_strip.setOn(true);
                s_strip.setBrightness(0);
                s_strip.setWhite(80);
                s_strip.update();
                vTaskDelay(pdMS_TO_TICKS(200));
                s_strip.setOn(false);
                s_strip.update();
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            s_strip.setOn(on);
            s_strip.setBrightness(bri);
            s_strip.setHue(hue);
            s_strip.setWhite(w);
            s_strip.update();
            return AckStatus::OK;
        }

        case CmdId::PING:
            return AckStatus::OK;

        default:
            return AckStatus::UNKNOWN_CMD;
    }
}



static void logFirmwareIdentity(void) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_app_desc_t*  desc    = esp_app_get_description();
    ESP_LOGI(TAG, "══════════════════════════════════════════");
    ESP_LOGI(TAG, "  FW v%s   built %s %s", desc->version, desc->date, desc->time);
    ESP_LOGI(TAG, "  Slot: %s @ 0x%06lX  (%lu KB)",
             running->label,
             (unsigned long)running->address,
             (unsigned long)running->size / 1024);
    ESP_LOGI(TAG, "══════════════════════════════════════════");
}

/* ─── app_main ───────────────────────────────────────────────────────────── */

extern "C" void app_main(void) {
    logFirmwareIdentity();
    DeviceIdentity::instance().begin();
    ESP_LOGI(TAG, "Strip node starting (v2 protocol, %d LEDs on GPIO %d)...",
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
    s_strip.setOn(false);
    s_strip.update();

    /* NVS — AutoPair persistence lives here */
    ConfigStore::instance().begin();

    /* Radio */
    EspNowManager& enm = EspNowManager::instance();
    enm.setReceiveCallback([](const uint8_t* sender, const uint8_t* data, int len) {
        MessageProtocol::instance().processMessage(data, (uint8_t)len);
    });
    if (enm.begin() != ESP_OK) {              /* prints this device's MAC */
        ESP_LOGE(TAG, "ESP-NOW init failed");
        return;
    }

    /* Protocol */
    MessageProtocol& msg = MessageProtocol::instance();
    msg.begin();
    msg.registerTransport(TRANSPORT_ESPNOW,
        [](const uint8_t* data, uint8_t len) {                    /* broadcast */
            static const uint8_t B[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
            return EspNowManager::instance().send(B, data, len);
        },
        [](const uint8_t dst[6], const uint8_t* data, uint8_t len) { /* unicast */
            return EspNowManager::instance().send(dst, data, len);
        });
    msg.setCommandHandler(onCommand);

    /* Pairing */
    AutoPair& pair = AutoPair::instance();
    pair.setPeerAddCallback([](const uint8_t mac[6]) {
        EspNowManager::instance().addPeer(mac);
    });
    pair.setLEDCallback(showPairLED);
    pair.setPairResultCallback([](bool ok, const uint8_t ctrl[6]) {
        ESP_LOGI(TAG, "Pairing %s (hub %02X:%02X:%02X:%02X:%02X:%02X)",
                 ok ? "ACCEPTED" : "rejected",
                 ctrl[0], ctrl[1], ctrl[2], ctrl[3], ctrl[4], ctrl[5]);
    });
    pair.begin(DeviceRole::LIGHT, "Strip 1");

    while (true) {
        pair.update();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}