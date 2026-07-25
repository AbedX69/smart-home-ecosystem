/**
 * @file main.cpp
 * @brief HUB (ESP32-S3 WROOM) — MessageProtocol v2 + AutoPair controller.
 *
 * No hardcoded strip MAC. The hub auto-accepts pair requests (serial-accept
 * phase — GC9A01 popup UI comes later), persists them in NVS, and targets
 * paired LIGHT devices by panel index: panel 0 -> first light, panel 1 ->
 * second light when it exists.
 *
 * TX policy per the v2 reliability model:
 *   knob turning  -> fire-and-forget stream (stale values never retried)
 *   knob settled  -> ONE reliable send (retry-until-ACK)
 *   every 30 s     -> fire-and-forget keepalive (covers node reboot)
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <cstring>
#include "gc9a01.h"
#include "smart_light_remote.h"
#include "esp_now_manager.h"
#include "message_protocol.h"
#include "auto_pair.h"
#include "config_store.h"
#include "device_identity.h"
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_partition.h>
#include <esp_app_format.h>

static const char* TAG = "hub";

/* ─── Pin map (unchanged) ────────────────────────────────────────────────── */

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

#define FINAL_SETTLE_US   300000LL     


#define HEARTBEAT_US      30000000LL   
static int64_t s_last_tx_us[2] = {0, 0};


/* ─── Paired-light lookup: panel index -> Nth paired LIGHT ───────────────── */

static bool getLightMac(int which, uint8_t out[6]) {
    AutoPair& pair = AutoPair::instance();
    int seen = 0;
    for (uint8_t i = 0; i < pair.getPairedCount(); i++) {
        const PairedDevice* d = pair.getPairedDevice(i);
        if (d && d->role == DeviceRole::LIGHT) {
            if (seen == which) {
                memcpy(out, d->mac, 6);
                return true;
            }
            seen++;
        }
    }
    return false;
}
static void sendPanelState(const SmartLightRemote& panel, bool reliable) {
    s_last_tx_us[panel.index()] = esp_timer_get_time();   /* stamp first */

    uint8_t mac[6];
    if (!getLightMac(panel.index(), mac)) return;    /* no light paired yet */

    MessageProtocol::instance().sendLightState(
        mac, panel.isOn(), panel.brightness(), panel.hue(),
        panel.whiteBright(), reliable);
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


/* App descriptor sits right after the image header (24B) + first
 * segment header (8B). Hardcoded rather than sizeof() to avoid
 * pulling in bootloader_support. */
#define APP_DESC_OFFSET  32

static void logStagedImage(void) {
    const esp_partition_t* store = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    if (!store) {
        ESP_LOGW(TAG, "Staged image: no storage partition found");
        return;
    }

    esp_app_desc_t d;
    if (esp_partition_read(store, APP_DESC_OFFSET, &d, sizeof(d)) != ESP_OK) {
        ESP_LOGW(TAG, "Staged image: read failed");
        return;
    }
    if (d.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        ESP_LOGI(TAG, "Staged image: none (storage empty)");
        return;
    }
    ESP_LOGI(TAG, "Staged image: \"%s\" v%s  built %s %s",
             d.project_name, d.version, d.date, d.time);
}
/* ─── app_main ───────────────────────────────────────────────────────────── */

extern "C" void app_main(void) {
    logFirmwareIdentity();
    logStagedImage();
    DeviceIdentity& id = DeviceIdentity::instance();
    id.begin();
    if (!id.isProvisioned()) id.provisionAsNewHouse();
    ESP_LOGI(TAG, "Hub starting (v2 protocol + AutoPair controller)...");

    SmartLightRemote::buildAngleLUT();

    GC9A01 tft0(SPI_MOSI, SPI_SCK, GC1_CS, GC1_DC, GC1_RST, GC_BLK,      SPI2_HOST);
    GC9A01 tft1(SPI_MOSI, SPI_SCK, GC2_CS, GC2_DC, GC2_RST, GPIO_NUM_NC, SPI2_HOST);
    if (!tft0.init()) ESP_LOGE(TAG, "TFT 0 init failed");
    if (!tft1.init()) ESP_LOGE(TAG, "TFT 1 init failed");

    SmartLightRemote panel0(tft0, 0, ENC1_CLK, ENC1_DT, ENC1_SW, TOUCH1_PIN);
    SmartLightRemote panel1(tft1, 1, ENC2_CLK, ENC2_DT, ENC2_SW, TOUCH2_PIN);
    panel0.init();
    panel1.init();

    /* NVS */
    ConfigStore::instance().begin();

    /* Radio */
    EspNowManager& enm = EspNowManager::instance();
    enm.setReceiveCallback([](const uint8_t* sender, const uint8_t* data, int len) {
        MessageProtocol::instance().processMessage(data, (uint8_t)len);
    });
    if (enm.begin() != ESP_OK) ESP_LOGE(TAG, "ESP-NOW init failed");

    /* Protocol */
    MessageProtocol& msg = MessageProtocol::instance();
    msg.begin();
    msg.registerTransport(TRANSPORT_ESPNOW,
        [](const uint8_t* data, uint8_t len) {
            static const uint8_t B[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
            return EspNowManager::instance().send(B, data, len);
        },
        [](const uint8_t dst[6], const uint8_t* data, uint8_t len) {
            return EspNowManager::instance().send(dst, data, len);
        });
    msg.setCommandHandler([](CmdId cmd, const uint8_t* payload, uint8_t len,
                             const uint8_t src[6]) {
        if (AutoPair::handlesCmd(cmd)) {
            AutoPair::instance().processPairMessage(cmd, payload, len, src);
            return AckStatus::OK;
        }
        return AckStatus::UNKNOWN_CMD;
    });
    msg.setDeliveryCallback([](uint16_t seq, CmdId cmd, bool delivered,
                               AckStatus st, const uint8_t dst[6]) {
        if (!delivered) {
            ESP_LOGW(TAG, "%s seq=%u NOT delivered to %02X:%02X — node offline?",
                     MessageProtocol::cmdName(cmd), seq, dst[4], dst[5]);
        }
    });

    /* Pairing — controller */
    AutoPair& pair = AutoPair::instance();
    pair.setPeerAddCallback([](const uint8_t mac[6]) {
        EspNowManager::instance().addPeer(mac);
    });
    pair.setPairRequestCallback([](const PairRequestInfo* info) {
        ESP_LOGI(TAG, "PAIR REQUEST: \"%s\" role=%s (%02X:%02X:%02X:%02X:%02X:%02X)",
                 info->name, deviceRoleName(info->role),
                 info->mac[0], info->mac[1], info->mac[2],
                 info->mac[3], info->mac[4], info->mac[5]);
    });
    pair.beginAsController();

    panel0.render();
    panel1.render();

    ESP_LOGI(TAG, "══════════════════════════════════════════");
    ESP_LOGI(TAG, "  UID %s", id.uidString());
    ESP_LOGI(TAG, "  House 0x%04X  room %u  node %u",
             (unsigned)id.house(), (unsigned)id.room(), (unsigned)id.node());
    ESP_LOGI(TAG, "══════════════════════════════════════════");
    ESP_LOGI(TAG, "Entering main loop...");

    int64_t  last_change_us[2] = {0, 0};
    bool     final_due[2]      = {false, false};
    uint32_t slow_tick         = 0;

    while (true) {
        /* Live knob stream: fire-and-forget */
        if (panel0.update()) {
            sendPanelState(panel0, false);
            final_due[0] = true;
            last_change_us[0] = esp_timer_get_time();
        }
        if (panel1.update()) {
            sendPanelState(panel1, false);
            final_due[1] = true;
            last_change_us[1] = esp_timer_get_time();
        }

        /* Knob settled: one reliable send */
        int64_t now = esp_timer_get_time();
        if (final_due[0] && now - last_change_us[0] > FINAL_SETTLE_US) {
            final_due[0] = false;
            sendPanelState(panel0, true);
        }
        if (final_due[1] && now - last_change_us[1] > FINAL_SETTLE_US) {
            final_due[1] = false;
            sendPanelState(panel1, true);
        }

        /* Auto-accept pending pair requests (serial-accept phase).
         * Done here, NOT inside the request callback — keeps AutoPair's
         * mutex out of its own callback path. */
        if (pair.getPendingCount() > 0) {
            const PairRequestInfo* pi = pair.getPending(0);
            if (pi) {
                uint8_t mac[6];
                memcpy(mac, pi->mac, 6);
                ESP_LOGI(TAG, "Auto-accepting \"%s\"", pi->name);
                pair.acceptDevice(mac);
                /* Push current state to the fresh node, reliably */
                sendPanelState(panel0, true);
                sendPanelState(panel1, true);
            }
        }

        /* ~500 ms housekeeping */
        if (++slow_tick >= 50) {
            slow_tick = 0;
            pair.update();

            int64_t hb = esp_timer_get_time();
            if (hb - s_last_tx_us[0] > HEARTBEAT_US) sendPanelState(panel0, false);
            if (hb - s_last_tx_us[1] > HEARTBEAT_US) sendPanelState(panel1, false);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}