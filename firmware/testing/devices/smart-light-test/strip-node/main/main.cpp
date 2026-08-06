/**
 * @file main.cpp
 * @brief STRIP NODE (Seeed XIAO ESP32-C6) — MessageProtocol v3 + AutoPair v3.
 *
 * No hardcoded MACs and no hardcoded addresses anywhere. On first boot the
 * node broadcasts PAIR_REQUEST until a hub accepts; the hub's PAIR_ACCEPT
 * carries house/room/node, which the node persists. Every later boot goes
 * straight to paired, commissioned operation.
 *
 * Pairing feedback on the strip itself:
 *   dim blue   = searching for a hub
 *   green 0.8s = paired
 *   red x3     = rejected
 *
 * WIRING: unchanged — DATA -> D10 (GPIO18), common GND, 5V from bench.
 * FLASH ORDER: doesn't matter. Flash both, watch them pair.
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
#include "ota_manager.h"
#include "ota_bulk_rx.h"
#include <esp_ota_ops.h>
#include <esp_app_desc.h>

static const char* TAG = "strip_node";

#define STRIP_PIN   GPIO_NUM_18     /* XIAO C6: D10 */
#define NUM_LEDS    10              /* POC — full strip is 144 */

static SmartLightDevice s_strip(STRIP_PIN, NUM_LEDS);


/* ─── Pairing feedback rendered on the strip ──────────────────────────────── */

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


/* ─── Command handler ─────────────────────────────────────────────────────── */

static AckStatus onCommand(CmdId cmd, const uint8_t* payload, uint8_t len,
                           DeviceUid src_uid) {

    /* A command that got this far came off the radio and passed house and
     * room/node filtering - proof the receive path works end to end. That
     * is the gate for validate(); booting alone is not. See "WHAT validate()
     * SHOULD MEAN" in ota_manager.h. */
    if (OTAManager::instance().isPendingValidation()) {
        OTAManager::instance().validate();
        ESP_LOGI(TAG, "Receive path proven - firmware validated, rollback cancelled");
    }

    /* Pairing and commissioning traffic goes to AutoPair */
    if (AutoPair::handlesCmd(cmd)) {
        AutoPair::instance().processPairMessage(cmd, payload, len, src_uid);
        return AckStatus::OK;
    }

    /* OTA control plane. The ACK we return on OTA_OFFER is the accept
     * or reject - there is no separate accept message. */
    if (OtaBulkRx::handlesCmd(cmd)) {
        return OtaBulkRx::instance().processControl(cmd, payload, len, src_uid);
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
    ESP_LOGI(TAG, "  %s  v%s  sha %02x%02x%02x%02x",
             desc->project_name, desc->version,
             desc->app_elf_sha256[0], desc->app_elf_sha256[1],
             desc->app_elf_sha256[2], desc->app_elf_sha256[3]);
    ESP_LOGI(TAG, "  Slot: %s @ 0x%06lX  (%lu KB)",
             running->label,
             (unsigned long)running->address,
             (unsigned long)running->size / 1024);
    ESP_LOGI(TAG, "══════════════════════════════════════════");
}

static void logDeviceIdentity(void) {
    DeviceIdentity& id = DeviceIdentity::instance();
    AutoPair&       pr = AutoPair::instance();
    ESP_LOGI(TAG, "══════════════════════════════════════════");
    ESP_LOGI(TAG, "  UID %s", id.uidString());
    ESP_LOGI(TAG, "  House 0x%04X  room %u  node %u",
             (unsigned)id.house(), (unsigned)id.room(), (unsigned)id.node());
    if (pr.isPaired()) {
        ESP_LOGI(TAG, "  Controller %08X", (unsigned)pr.getControllerUid());
    } else {
        ESP_LOGI(TAG, "  Not paired — searching");
    }
    ESP_LOGI(TAG, "══════════════════════════════════════════");
}

/* ─── app_main ────────────────────────────────────────────────────────────── */

extern "C" void app_main(void) {

    /* NVS first — DeviceIdentity and AutoPair both persist here. */
    ConfigStore::instance().begin();

    /* Identity before anything that stamps or filters a packet. */
    if (DeviceIdentity::instance().begin() != ESP_OK) {
        ESP_LOGE(TAG, "DeviceIdentity failed — cannot continue");
        return;
    }

    ESP_LOGI(TAG, "Strip node starting (v3 protocol, %d LEDs on GPIO %d)...",
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

    /* ── Protocol ──────────────────────────────────────────────────────
     * Wired up BEFORE the radio starts. The ESP-NOW RX task begins
     * delivering packets the moment enm.begin() returns, and a packet
     * that arrives before msg.begin() would be handled with no identity.
     * ───────────────────────────────────────────────────────────────── */
    MessageProtocol& msg = MessageProtocol::instance();

    /* The UID→MAC seam. AutoPair owns the table today; device_registry v2
     * will take over these two lambdas and nothing else will change. */
    msg.setUidResolver([](DeviceUid uid, uint8_t mac[6]) {
        return AutoPair::instance().resolveUid(uid, mac);
    });
    msg.setPeerObservedCallback([](DeviceUid uid, const uint8_t mac[6]) {
        AutoPair::instance().noteAddress(uid, mac);
    });

    if (msg.begin() != ESP_OK) {
        ESP_LOGE(TAG, "MessageProtocol init failed");
        return;
    }
    msg.registerTransport(TRANSPORT_ESPNOW,
        [](const uint8_t* data, uint8_t len) {                    /* broadcast */
            static const uint8_t B[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
            return EspNowManager::instance().send(B, data, len);
        },
        [](const uint8_t dst[6], const uint8_t* data, uint8_t len) { /* unicast */
            return EspNowManager::instance().send(dst, data, len);
        });
    msg.setCommandHandler(onCommand);

    /* ── Pairing ───────────────────────────────────────────────────────
     * Callbacks before begin(): begin() replays peer-adds from NVS.
     * ───────────────────────────────────────────────────────────────── */
    AutoPair& pair = AutoPair::instance();
    pair.setPeerAddCallback([](const uint8_t mac[6]) {
        EspNowManager::instance().addPeer(mac);
    });
    pair.setLEDCallback(showPairLED);
    pair.setPairResultCallback([](bool ok, DeviceUid ctrl) {
        ESP_LOGI(TAG, "Pairing %s (hub %08X)",
                 ok ? "ACCEPTED" : "rejected", (unsigned)ctrl);
    });

    /* ── Radio ─────────────────────────────────────────────────────────
     * The third argument is the transport-level source MAC. It is the
     * only MAC that crosses into the protocol layer, and it exists so
     * the address table can learn who is where.
     * ───────────────────────────────────────────────────────────────── */
    /* OTA. begin() arms the rollback timer when this boot is unvalidated;
     * without it a bad image never rolls back. Before the radio, so an
     * offer arriving immediately has somewhere to land. */
    OTAManager& ota = OTAManager::instance();
    if (ota.begin(OTA_DEFAULT_TIMEOUT_S) != ESP_OK) {
        ESP_LOGE(TAG, "OTAManager init failed");
    }
    if (OtaBulkRx::instance().begin(DeviceRole::LIGHT) != ESP_OK) {
        ESP_LOGE(TAG, "OtaBulkRx init failed");
    }
    if (ota.isPendingValidation()) {
        ESP_LOGW(TAG, "Unvalidated boot - rolls back in %us unless a command arrives",
                 (unsigned)OTA_DEFAULT_TIMEOUT_S);
    }

    EspNowManager& enm = EspNowManager::instance();
    enm.setReceiveCallback([](const uint8_t* sender, const uint8_t* data, int len) {
        /* Bulk chunks are raw 248-byte frames, not 48-byte MessagePackets.
         * Checked first so they never reach the packet parser. */
        if (OtaBulkRx::instance().tryConsume(data, len)) return;
        MessageProtocol::instance().processMessage(data, (uint8_t)len, sender);
    });
    /* Channel before the radio starts. ESP-NOW peers must share a channel,
     * and the hub adopts the routers channel when it joins WiFi. A stored 0
     * means we were never told - boot on the firmware default and let the
     * recovery sweep go find the hub. */
    EspNowConfig enm_cfg;
    enm_cfg.channel = ConfigStore::instance().getU8(ConfigKeys::WIFI_CHANNEL, 0);
    if (enm_cfg.channel) {
        ESP_LOGI(TAG, "Stored channel %u", (unsigned)enm_cfg.channel);
    }

    if (enm.begin(enm_cfg) != ESP_OK) {       /* prints this device's MAC */
        ESP_LOGE(TAG, "ESP-NOW init failed");
        return;
    }

    pair.begin(DeviceRole::LIGHT, "Strip 1");

    logDeviceIdentity();
    logFirmwareIdentity();

    while (true) {
        pair.update();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}