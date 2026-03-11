/*
 * =============================================================================
 * FILE:        main.cpp
 * PROJECT:     ble-test
 * DESCRIPTION: Test for BLE Manager component (NimBLE-based).
 * =============================================================================
 * 
 * MODES (set via build flags):
 * 
 *   -DBLE_TEST_SERVER    → BLE peripheral with custom GATT service (default)
 *   -DBLE_TEST_CLIENT    → BLE central that scans, connects, reads
 *   -DBLE_TEST_SCANNER   → Scan only, print all found devices
 * 
 * SERVER mode exposes:
 *   Service:  "12345678-1234-1234-1234-123456789ABC"
 *     - Temperature (read + notify): "12345678-1234-1234-1234-100000000001"
 *     - LED control (read + write):  "12345678-1234-1234-1234-100000000002"
 *     - Device name (read only):     "12345678-1234-1234-1234-100000000003"
 * 
 * =============================================================================
 */

#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "ble_manager.h"
#include "ble_server.h"
#include "ble_client.h"

static const char* TAG = "BLETest";

/* Default to server mode */
#if !defined(BLE_TEST_SERVER) && !defined(BLE_TEST_CLIENT) && !defined(BLE_TEST_SCANNER)
#define BLE_TEST_SERVER
#endif

#ifndef BLE_DEVICE_NAME
#define BLE_DEVICE_NAME "ESP32-SmartHome"
#endif

/* ─── UUIDs ──────────────────────────────────────────────────────────────── */
#define SVC_UUID        "12345678-1234-1234-1234-123456789ABC"
#define CHR_TEMP_UUID   "12345678-1234-1234-1234-100000000001"
#define CHR_LED_UUID    "12345678-1234-1234-1234-100000000002"
#define CHR_NAME_UUID   "12345678-1234-1234-1234-100000000003"

/* ─── Characteristic Value Buffers (must persist!) ───────────────────────── */
static uint8_t temp_value[4] = {0x00, 0x16, 0x00, 0x00};  // 22°C as uint16 LE
static uint8_t led_value[1] = {0x00};                       // 0=off, 1=on
static char    name_value[32] = "ESP32 Smart Device";

/* ─── Attribute handles (filled by NimBLE during init) ───────────────────── */
static uint16_t temp_handle = 0;
static uint16_t led_handle = 0;
static uint16_t name_handle = 0;

/* =============================================================================
 * GAP EVENT CALLBACK
 * ========================================================================== */

static void onBLEEvent(BLEEvent event, const BLEEventInfo* info) {
    char addr_str[18];

    switch (event) {
        case BLEEvent::INITIALIZED:
            ESP_LOGI(TAG, "BLE ready!");
            break;

        case BLEEvent::CONNECTED:
            if (info) {
                BLEManager::addrToStr(info->peer_addr, addr_str);
                ESP_LOGI(TAG, "Connected: %s (handle=%d)", addr_str, info->conn_handle);
            }
            break;

        case BLEEvent::DISCONNECTED:
            if (info) {
                ESP_LOGI(TAG, "Disconnected (handle=%d)", info->conn_handle);
            }
            break;

        case BLEEvent::MTU_CHANGED:
            if (info) {
                ESP_LOGI(TAG, "MTU: %d", info->mtu);
            }
            break;

        case BLEEvent::SUBSCRIBE:
            ESP_LOGI(TAG, "Client subscribed to attr=%d", info ? info->attr_handle : 0);
            break;

        case BLEEvent::UNSUBSCRIBE:
            ESP_LOGI(TAG, "Client unsubscribed from attr=%d", info ? info->attr_handle : 0);
            break;

        case BLEEvent::SCAN_RESULT:
            if (info) {
                BLEManager::addrToStr(info->peer_addr, addr_str);
                ESP_LOGI(TAG, "Found: %s \"%s\" RSSI=%d",
                         addr_str,
                         strlen(info->name) > 0 ? info->name : "(unknown)",
                         info->rssi);
            }
            break;

        case BLEEvent::SCAN_COMPLETE:
            ESP_LOGI(TAG, "Scan complete");
            break;

        default:
            break;
    }
}

/* =============================================================================
 * SERVER MODE - GATT ACCESS CALLBACK
 * ========================================================================== */

#ifdef BLE_TEST_SERVER
static void onCharAccess(BLECharAccess* access) {
    if (!access) return;

    if (access->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        /* Something was written */
        if (access->attr_handle == led_handle) {
            bool on = (access->data[0] != 0);
            ESP_LOGI(TAG, "LED command: %s", on ? "ON" : "OFF");
            /* In a real app: gpio_set_level(LED_PIN, on); */
        }
    }

    if (access->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (access->attr_handle == temp_handle) {
            ESP_LOGD(TAG, "Temperature read");
            /* Could update temp_value here with fresh sensor data */
        }
    }
}
#endif

/* =============================================================================
 * CLIENT MODE - NOTIFICATION CALLBACK
 * ========================================================================== */

#ifdef BLE_TEST_CLIENT
static void onNotification(const BLENotifyData* data) {
    if (!data) return;
    ESP_LOGI(TAG, "Notification: conn=%d attr=%d len=%d",
             data->conn_handle, data->attr_handle, data->data_len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data->data, data->data_len, ESP_LOG_INFO);
}

static void onDiscoveryDone(uint16_t conn_handle, int svc_count) {
    ESP_LOGI(TAG, "Discovery done: %d services found", svc_count);

    BLEClient& client = BLEClient::instance();
    int count;
    const BLEDiscoveredService* svcs = client.getServices(conn_handle, count);

    for (int s = 0; s < count; s++) {
        ESP_LOGI(TAG, "  Service %d: handles %d-%d, %d chars",
                 s, svcs[s].start_handle, svcs[s].end_handle, svcs[s].char_count);
        for (int c = 0; c < svcs[s].char_count; c++) {
            ESP_LOGI(TAG, "    Char: val_handle=%d props=0x%02X",
                     svcs[s].chars[c].val_handle, svcs[s].chars[c].properties);
        }
    }

    /* Try to find and read the temperature characteristic */
    const BLEDiscoveredChar* temp_chr = client.getCharByUUID(conn_handle, CHR_TEMP_UUID);
    if (temp_chr) {
        uint8_t buf[16];
        uint16_t len;
        esp_err_t ret = client.read(conn_handle, temp_chr->val_handle, buf, sizeof(buf), &len);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Temperature read: %d bytes", len);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, len, ESP_LOG_INFO);
        }

        /* Subscribe to temperature notifications */
        if (temp_chr->properties & BLE_GATT_CHR_PROP_NOTIFY) {
            client.subscribe(conn_handle, temp_chr->val_handle, true);
        }
    }
}

/* Track the address of the first server we find to connect to */
static uint8_t s_target_addr[6] = {};
static uint8_t s_target_addr_type = 0;
static bool s_target_found = false;

static void clientScanHandler(BLEEvent event, const BLEEventInfo* info) {
    /* Call the main handler first */
    onBLEEvent(event, info);

    if (event == BLEEvent::SCAN_RESULT && info && !s_target_found) {
        /* Look for our server by name */
        if (strstr(info->name, "SmartHome") || strstr(info->name, "ESP32")) {
            memcpy(s_target_addr, info->peer_addr, 6);
            s_target_addr_type = info->peer_addr_type;
            s_target_found = true;
            ESP_LOGI(TAG, "Target found! Will connect after scan.");
        }
    }

    if (event == BLEEvent::CONNECTED && info) {
        /* Start service discovery on connect */
        BLEClient::instance().discoverServices(info->conn_handle);
    }
}
#endif

/* =============================================================================
 * MAIN
 * ========================================================================== */

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
#if defined(BLE_TEST_SERVER)
    ESP_LOGI(TAG, "║    BLE Test - SERVER (Peripheral) MODE   ║");
#elif defined(BLE_TEST_CLIENT)
    ESP_LOGI(TAG, "║    BLE Test - CLIENT (Central) MODE      ║");
#else
    ESP_LOGI(TAG, "║    BLE Test - SCANNER MODE               ║");
#endif
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    BLEManager& ble = BLEManager::instance();

    /* ── SERVER MODE ───────────────────────────────────────────────── */
#if defined(BLE_TEST_SERVER)
    ble.setEventCallback(onBLEEvent);

    /* Build GATT table BEFORE calling begin() */
    BLEServer& server = BLEServer::instance();
    server.setAccessCallback(onCharAccess);

    server.addService(SVC_UUID);
    server.addCharacteristic(CHR_TEMP_UUID,
                              BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                              temp_value, sizeof(temp_value),
                              sizeof(temp_value), &temp_handle);
    server.addCharacteristic(CHR_LED_UUID,
                              BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                              led_value, sizeof(led_value),
                              sizeof(led_value), &led_handle);
    server.addCharacteristic(CHR_NAME_UUID,
                              BLE_GATT_CHR_F_READ,
                              (uint8_t*)name_value, strlen(name_value),
                              sizeof(name_value), &name_handle);
    server.buildServices();

    /* Now start BLE */
    ble.begin(BLE_DEVICE_NAME);

    /* Wait for stack sync, then start advertising */
    vTaskDelay(pdMS_TO_TICKS(500));
    ble.startAdvertising();

    /* Simulate temperature changes and notify */
    int16_t temp = 2200;  // 22.00°C * 100
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        /* Update temperature */
        temp += (esp_random() % 100) - 50;  // Random +-0.5°C
        if (temp < 1500) temp = 1500;
        if (temp > 3500) temp = 3500;

        temp_value[0] = temp & 0xFF;
        temp_value[1] = (temp >> 8) & 0xFF;

        ESP_LOGI(TAG, "Temperature: %.2f°C | LED: %s | Connections: %d",
                 temp / 100.0f,
                 led_value[0] ? "ON" : "OFF",
                 ble.getConnectionCount());

        /* Notify connected clients */
        if (ble.getConnectionCount() > 0 && temp_handle != 0) {
            server.notify(temp_handle, temp_value, sizeof(temp_value));
        }
    }

    /* ── CLIENT MODE ───────────────────────────────────────────────── */
#elif defined(BLE_TEST_CLIENT)
    BLEClient& client = BLEClient::instance();
    client.setNotifyCallback(onNotification);
    client.setDiscoveryCallback(onDiscoveryDone);

    ble.setEventCallback(clientScanHandler);
    ble.begin("ESP32-Central");

    vTaskDelay(pdMS_TO_TICKS(500));

    /* Scan for peripherals */
    ESP_LOGI(TAG, "Scanning for BLE devices...");
    BLEScanConfig scan_cfg;
    scan_cfg.duration_ms = 10000;
    ble.startScan(scan_cfg);

    /* Wait for scan to complete */
    vTaskDelay(pdMS_TO_TICKS(11000));

    if (s_target_found) {
        char addr_str[18];
        BLEManager::addrToStr(s_target_addr, addr_str);
        ESP_LOGI(TAG, "Connecting to target: %s", addr_str);
        ble.connect(s_target_addr, s_target_addr_type);
    } else {
        ESP_LOGW(TAG, "No target device found. Scanning again in 10s...");
    }

    /* Keep running */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        if (!s_target_found) {
            ESP_LOGI(TAG, "Re-scanning...");
            s_target_found = false;
            ble.startScan(scan_cfg);
            vTaskDelay(pdMS_TO_TICKS(11000));
            if (s_target_found) {
                ble.connect(s_target_addr, s_target_addr_type);
            }
        }
    }

    /* ── SCANNER MODE ──────────────────────────────────────────────── */
#else
    ble.setEventCallback(onBLEEvent);
    ble.begin("ESP32-Scanner");

    vTaskDelay(pdMS_TO_TICKS(500));

    while (true) {
        ESP_LOGI(TAG, "Scanning for 10 seconds...");
        BLEScanConfig cfg;
        cfg.duration_ms = 10000;
        cfg.filter_dup = true;
        ble.startScan(cfg);

        vTaskDelay(pdMS_TO_TICKS(15000));
    }
#endif
}
