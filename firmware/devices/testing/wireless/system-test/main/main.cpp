/*
 * =============================================================================
 * FILE:        main.cpp
 * PROJECT:     system-test
 * DESCRIPTION: Full integration: Device Registry + Config Store + Message Protocol.
 * =============================================================================
 * 
 * This is the "wow" demo. Flash 2-3 boards with different roles:
 * 
 *   Board A: CONTROLLER — discovers devices, sends ON/OFF commands,
 *            queries sensors, displays the network in serial log.
 * 
 *   Board B: LIGHT — receives ON/OFF/SET_LEVEL commands,
 *            toggles LED, sends ACKs, reports state changes.
 * 
 *   Board C: SENSOR — periodically broadcasts temperature,
 *            responds to GET_TEMPERATURE queries.
 * 
 * WHAT HAPPENS:
 *   1. All boards start, begin sending heartbeats via ESP-NOW
 *   2. Within 5s, all boards discover each other
 *   3. Controller finds the light → sends TOGGLE command
 *   4. Light toggles LED → sends ACK back → broadcasts state
 *   5. Controller finds the sensor → sends GET_TEMPERATURE query
 *   6. Sensor replies with current temp
 *   7. Sensor also broadcasts temp every 10s unsolicited
 *   8. Unplug a board → others detect it offline in ~30s
 * 
 * =============================================================================
 */

#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "device_registry.h"
#include "config_store.h"
#include "message_protocol.h"

static const char* TAG = "SysTest";

/* Default to LIGHT mode */
#if !defined(SYS_TEST_CONTROLLER) && !defined(SYS_TEST_LIGHT) && !defined(SYS_TEST_SENSOR)
#define SYS_TEST_LIGHT
#endif

#ifndef LED_PIN
#define LED_PIN 2       /* Generic ESP32 devkit onboard LED */
#endif

static bool light_state = false;
static uint8_t light_level = 0;
static float sensor_temp = 22.5f;

/* ═══════════════════════════════════════════════════════════════════════════
 * ESP-NOW TRANSPORT LAYER
 * ═══════════════════════════════════════════════════════════════════════════ */

static uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/*
 * Single receive callback that feeds BOTH the Device Registry
 * and the Message Protocol. They each check the magic bytes
 * and ignore packets that aren't theirs.
 */
static void espnow_recv_cb(const esp_now_recv_info_t* info,
                             const uint8_t* data, int len) {
    DeviceRegistry::instance().processAnnouncement(data, (uint8_t)len);
    MessageProtocol::instance().processMessage(data, (uint8_t)len);
}

static esp_err_t espnow_broadcast(const uint8_t* data, uint8_t len) {
    return esp_now_send(broadcast_mac, data, len);
}

static esp_err_t espnow_unicast(const uint8_t dst[6],
                                  const uint8_t* data, uint8_t len) {
    /* Add peer if not already added (ESP-NOW requires it) */
    if (!esp_now_is_peer_exist(dst)) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, dst, 6);
        peer.channel = 1;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }
    return esp_now_send(dst, data, len);
}

static void init_espnow() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcast_mac, 6);
    peer.channel = 1;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DEVICE REGISTRY EVENTS
 * ═══════════════════════════════════════════════════════════════════════════ */

static const char* roleName(DeviceRole r) {
    switch (r) {
        case DeviceRole::CONTROLLER: return "CONTROLLER";
        case DeviceRole::LIGHT:      return "LIGHT";
        case DeviceRole::SENSOR:     return "SENSOR";
        case DeviceRole::SWITCH:     return "SWITCH";
        case DeviceRole::SOCKET:     return "SOCKET";
        case DeviceRole::GARAGE:     return "GARAGE";
        default:                     return "UNKNOWN";
    }
}

static void onRegistryEvent(const RegistryEventInfo* info) {
    if (!info || !info->device) return;

    switch (info->event) {
        case RegistryEvent::DEVICE_JOINED:
            ESP_LOGI(TAG, ">>> JOINED: \"%s\" (%s) MAC=...%02X:%02X",
                     info->device->name, roleName(info->device->role),
                     info->device->mac[4], info->device->mac[5]);
            break;
        case RegistryEvent::DEVICE_LEFT:
            ESP_LOGW(TAG, "<<< LEFT: \"%s\"", info->device->name);
            break;
        default:
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * LIGHT MESSAGE HANDLERS
 * ═══════════════════════════════════════════════════════════════════════════ */

#if defined(SYS_TEST_LIGHT)

static void initLED() {
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << LED_PIN);
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level((gpio_num_t)LED_PIN, 0);
}

static AckStatus onLightCommand(CmdId cmd, const uint8_t* payload,
                                  uint8_t len, const uint8_t src[6]) {
    MessageProtocol& msg = MessageProtocol::instance();

    switch (cmd) {
        case CmdId::ON:
            light_state = true;
            gpio_set_level((gpio_num_t)LED_PIN, 1);
            ESP_LOGI(TAG, "★ Light ON");
            msg.reportOnOff(true);
            return AckStatus::OK;

        case CmdId::OFF:
            light_state = false;
            gpio_set_level((gpio_num_t)LED_PIN, 0);
            ESP_LOGI(TAG, "★ Light OFF");
            msg.reportOnOff(false);
            return AckStatus::OK;

        case CmdId::TOGGLE:
            light_state = !light_state;
            gpio_set_level((gpio_num_t)LED_PIN, light_state ? 1 : 0);
            ESP_LOGI(TAG, "★ Light TOGGLE → %s", light_state ? "ON" : "OFF");
            msg.reportOnOff(light_state);
            return AckStatus::OK;

        case CmdId::SET_LEVEL:
            if (len >= 1) {
                light_level = payload[0];
                light_state = (light_level > 0);
                gpio_set_level((gpio_num_t)LED_PIN, light_state ? 1 : 0);
                ESP_LOGI(TAG, "★ Level → %d", light_level);
                msg.reportLevel(light_level);
                return AckStatus::OK;
            }
            return AckStatus::FAIL;

        case CmdId::PING:
            return AckStatus::OK;

        case CmdId::IDENTIFY:
            for (int i = 0; i < 3; i++) {
                gpio_set_level((gpio_num_t)LED_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(200));
                gpio_set_level((gpio_num_t)LED_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            gpio_set_level((gpio_num_t)LED_PIN, light_state ? 1 : 0);
            return AckStatus::OK;

        default:
            return AckStatus::UNKNOWN_CMD;
    }
}

static void onLightQuery(CmdId query, const uint8_t src[6], uint16_t seq) {
    MessageProtocol& msg = MessageProtocol::instance();
    if (query == CmdId::GET_STATE) {
        uint8_t val = light_state ? 1 : 0;
        msg.sendStateTo(src, CmdId::REPORT_ON_OFF, seq, &val, 1);
    }
}

#endif /* SYS_TEST_LIGHT */

/* ═══════════════════════════════════════════════════════════════════════════
 * SENSOR MESSAGE HANDLERS
 * ═══════════════════════════════════════════════════════════════════════════ */

#if defined(SYS_TEST_SENSOR)

static AckStatus onSensorCommand(CmdId cmd, const uint8_t* payload,
                                    uint8_t len, const uint8_t src[6]) {
    if (cmd == CmdId::PING) return AckStatus::OK;
    if (cmd == CmdId::IDENTIFY) {
        ESP_LOGI(TAG, "★ IDENTIFY — I'm the sensor!");
        return AckStatus::OK;
    }
    return AckStatus::NOT_SUPPORTED;
}

static void onSensorQuery(CmdId query, const uint8_t src[6], uint16_t seq) {
    MessageProtocol& msg = MessageProtocol::instance();
    if (query == CmdId::GET_TEMPERATURE) {
        int16_t temp_x100 = (int16_t)(sensor_temp * 100);
        uint8_t payload[2] = {
            (uint8_t)(temp_x100 >> 8),
            (uint8_t)(temp_x100 & 0xFF)
        };
        msg.sendStateTo(src, CmdId::REPORT_TEMP, seq, payload, 2);
        ESP_LOGI(TAG, "★ Replied temp: %.1f°C", sensor_temp);
    }
}

#endif /* SYS_TEST_SENSOR */

/* ═══════════════════════════════════════════════════════════════════════════
 * CONTROLLER HANDLERS
 * ═══════════════════════════════════════════════════════════════════════════ */

#if defined(SYS_TEST_CONTROLLER)

static void onControllerState(CmdId state_id, const uint8_t* payload,
                                uint8_t len, const uint8_t src[6]) {
    switch (state_id) {
        case CmdId::REPORT_TEMP:
            if (len >= 2) {
                int16_t raw = (payload[0] << 8) | payload[1];
                ESP_LOGI(TAG, "◆ Temp from ...%02X:%02X = %.1f°C",
                         src[4], src[5], raw / 100.0f);
            }
            break;
        case CmdId::REPORT_ON_OFF:
            if (len >= 1)
                ESP_LOGI(TAG, "◆ Light ...%02X:%02X → %s",
                         src[4], src[5], payload[0] ? "ON" : "OFF");
            break;
        case CmdId::REPORT_LEVEL:
            if (len >= 1)
                ESP_LOGI(TAG, "◆ Light ...%02X:%02X level=%d",
                         src[4], src[5], payload[0]);
            break;
        default:
            break;
    }
}

static void onControllerAck(CmdId cmd, AckStatus status, uint16_t seq,
                              const uint8_t src[6]) {
    ESP_LOGI(TAG, "◆ ACK %s: %s (seq=%d) from ...%02X:%02X",
             MessageProtocol::cmdName(cmd),
             (status == AckStatus::OK) ? "OK" : "FAIL",
             seq, src[4], src[5]);
}

#endif /* SYS_TEST_CONTROLLER */

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

extern "C" void app_main(void) {

    /* ── NVS ───────────────────────────────────────────────────────── */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ── Identity ──────────────────────────────────────────────────── */
    DeviceRole my_role;
    const char* my_name;

#if defined(SYS_TEST_CONTROLLER)
    my_role = DeviceRole::CONTROLLER;
    my_name = "Controller";
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  CONTROLLER — commands + queries          ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
#elif defined(SYS_TEST_SENSOR)
    my_role = DeviceRole::SENSOR;
    my_name = "Temp Sensor";
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  SENSOR — broadcasts temperature          ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
#else
    my_role = DeviceRole::LIGHT;
    my_name = "Kitchen Light";
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  LIGHT — ON/OFF/LEVEL commands            ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
#endif

    /* ── Config Store ──────────────────────────────────────────────── */
    ConfigStore& cfg = ConfigStore::instance();
    cfg.begin();

    if (!cfg.getBool(ConfigKeys::CONFIGURED, false)) {
        cfg.setString(ConfigKeys::DEVICE_NAME, my_name);
        cfg.setU8(ConfigKeys::DEVICE_ROLE, (uint8_t)my_role);
        cfg.setU8(ConfigKeys::GROUP_ID, 1);
        cfg.setU8(ConfigKeys::TRANSPORTS, TRANSPORT_ESPNOW);
        cfg.setU32(ConfigKeys::HEARTBEAT_MS, 5000);
        cfg.setBool(ConfigKeys::CONFIGURED, true);
    }

    char dev_name[32];
    cfg.getString(ConfigKeys::DEVICE_NAME, dev_name, sizeof(dev_name), "Unnamed");

    /* ── ESP-NOW ───────────────────────────────────────────────────── */
    init_espnow();

    /* ── Device Registry ───────────────────────────────────────────── */
    DeviceRegistry& reg = DeviceRegistry::instance();
    DeviceIdentity me;
    me.role = my_role;
    me.transports = TRANSPORT_ESPNOW;
    me.state_flags = STATE_FLAG_ONLINE | STATE_FLAG_CONFIGURED;
    me.setName(dev_name);

    reg.setSelf(me);
    reg.setEventCallback(onRegistryEvent);
    reg.registerTransport(TRANSPORT_ESPNOW, espnow_broadcast);
    reg.begin(5000, 30000);

    /* ── Message Protocol ──────────────────────────────────────────── */
    MessageProtocol& msg = MessageProtocol::instance();
    msg.begin();
    msg.registerTransport(TRANSPORT_ESPNOW, espnow_broadcast, espnow_unicast);

#if defined(SYS_TEST_LIGHT)
    initLED();
    msg.setCommandHandler(onLightCommand);
    msg.setQueryHandler(onLightQuery);
#elif defined(SYS_TEST_SENSOR)
    msg.setCommandHandler(onSensorCommand);
    msg.setQueryHandler(onSensorQuery);
#else
    msg.setStateHandler(onControllerState);
    msg.setAckHandler(onControllerAck);
#endif

    ESP_LOGI(TAG, "System ready.\n");

    /* ── Main Loop ─────────────────────────────────────────────────── */
    uint32_t tick = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        tick++;

        ESP_LOGI(TAG, "── tick %lu │ %d online ──",
                 (unsigned long)tick, reg.getOnlineCount());

#if defined(SYS_TEST_CONTROLLER)
        const DeviceEntry* light = reg.findByRole(DeviceRole::LIGHT);
        if (light && light->online) {
            msg.sendCommand(light->mac, CmdId::TOGGLE);
        }
        if (tick % 2 == 0) {
            const DeviceEntry* sensor = reg.findByRole(DeviceRole::SENSOR);
            if (sensor && sensor->online) {
                msg.sendQuery(sensor->mac, CmdId::GET_TEMPERATURE);
            }
        }

#elif defined(SYS_TEST_SENSOR)
        sensor_temp += ((float)(esp_random() % 100) - 50) / 100.0f;
        if (sensor_temp < 15.0f) sensor_temp = 15.0f;
        if (sensor_temp > 35.0f) sensor_temp = 35.0f;
        msg.reportTemperature(sensor_temp);

#else
        ESP_LOGI(TAG, "Light: %s  Level: %d",
                 light_state ? "ON" : "OFF", light_level);
#endif
    }
}
