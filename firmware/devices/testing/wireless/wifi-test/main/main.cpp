/*
 * =============================================================================
 * FILE:        main.cpp
 * PROJECT:     wifi-test
 * DESCRIPTION: Comprehensive test for the WiFi Manager component suite.
 * =============================================================================
 * 
 * MODES (set via build flags):
 * 
 *   -DWIFI_TEST_STA         → Connect to a router (set SSID/PASS build flags)
 *   -DWIFI_TEST_AP          → Start as access point only
 *   -DWIFI_TEST_NVS         → Load from NVS, captive portal fallback (DEFAULT)
 * 
 * For STA mode, also set:
 *   -DWIFI_SSID=\"MyNetwork\"
 *   -DWIFI_PASS=\"MyPassword\"
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 ******AbedX69******these are the commands to flash ota firmware:*******AbedX69*********
 *
 * To test OTA updates, use the provided curl commands from a terminal on the same network. Replace the IP address with your device's IP.
 * 
 * 
 * 
 * 
 * 
 *   PS C:\Users\AbedX69\embedded-project\firmware\wireless\testing\wifi-test> curl.exe -X POST --data-binary "@.pio\build\esp32d\firmware.bin" -H "Content-Type: application/octet-stream" http://192.168.31.11/api/ota
 *   {"status":"ok","bytes":976384,"message":"rebooting"}
 *   PS C:\Users\AbedX69\embedded-project\firmware\wireless\testing\wifi-test> curl.exe -X POST --data-binary "@.pio\build\esp32d\firmware.bin" -H "Content-Type: application/octet-stream" http://192.168.31.11/api/ota
 *   {"status":"ok","bytes":976368,"message":"rebooting"}
 *   PS C:\Users\AbedX69\embedded-project\firmware\wireless\testing\wifi-test> curl.exe -X POST --data-binary "@.pio\build\s3_wroom\firmware.bin" -H "Content-Type: application/octet-stream" http://192.168.31.205/api/ota
 *   {"status":"ok","bytes":977280,"message":"rebooting"}
 *   PS C:\Users\AbedX69\embedded-project\firmware\wireless\testing\wifi-test> curl.exe -X POST --data-binary "@.pio\build\s3_wroom\firmware.bin" -H "Content-Type: application/octet-stream" http://192.168.31.205/api/ota
 *   {"status":"ok","bytes":977296,"message":"rebooting"}
 * =============================================================================
 */

#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "wifi_manager.h"
#include "wifi_http_server.h"
#include "wifi_http_client.h"
#include "wifi_services.h"

static const char* TAG = "WiFiTest";

/* ─── Default Configuration ──────────────────────────────────────────────── */
#ifndef WIFI_SSID
#define WIFI_SSID       "YourSSID"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS       "YourPassword"
#endif

#ifndef WIFI_AP_SSID
#define WIFI_AP_SSID    "ESP32-SmartHome"
#endif

#ifndef WIFI_AP_PASS
#define WIFI_AP_PASS    ""   // Empty = open network
#endif

#ifndef WIFI_HOSTNAME
#define WIFI_HOSTNAME   "esp32-device"
#endif

/* Default to NVS mode (most useful for smart home) */
#if !defined(WIFI_TEST_STA) && !defined(WIFI_TEST_AP) && !defined(WIFI_TEST_NVS)
#define WIFI_TEST_NVS
#endif

/* =============================================================================
 * WiFi EVENT CALLBACK
 * ========================================================================== */

static void onWiFiEvent(WiFiEvent event, const WiFiEventInfo* info) {
    switch (event) {
        case WiFiEvent::STA_STARTED:
            ESP_LOGI(TAG, "📡 STA started");
            break;

        case WiFiEvent::CONNECTED:
            ESP_LOGI(TAG, "✅ Connected to AP");
            break;

        case WiFiEvent::GOT_IP:
            if (info) {
                ESP_LOGI(TAG, "🌐 Got IP: %s", info->ip_str);
            }
            break;

        case WiFiEvent::DISCONNECTED:
            ESP_LOGW(TAG, "❌ Disconnected from AP");
            break;

        case WiFiEvent::RECONNECTING:
            if (info) {
                ESP_LOGI(TAG, "🔄 Reconnecting... attempt %lu", (unsigned long)info->retry_num);
            }
            break;

        case WiFiEvent::RECONNECT_FAILED:
            ESP_LOGE(TAG, "💀 All reconnect attempts failed!");
            break;

        case WiFiEvent::AP_STARTED:
            ESP_LOGI(TAG, "📶 AP started");
            break;

        case WiFiEvent::AP_CLIENT_CONNECTED:
            if (info) {
                ESP_LOGI(TAG, "👤 Client connected: %02X:%02X:%02X:%02X:%02X:%02X",
                         info->client_mac[0], info->client_mac[1], info->client_mac[2],
                         info->client_mac[3], info->client_mac[4], info->client_mac[5]);
            }
            break;

        case WiFiEvent::AP_CLIENT_DISCONNECTED:
            ESP_LOGI(TAG, "👤 Client disconnected");
            break;

        case WiFiEvent::CHANNEL_CHANGED:
            if (info) {
                ESP_LOGI(TAG, "📻 Channel changed to %d (ESP-NOW should use this!)",
                         info->channel);
            }
            break;

        default:
            break;
    }
}

/* =============================================================================
 * CUSTOM HTTP ROUTES (for when connected to WiFi)
 * ========================================================================== */

/** GET /api/hello - simple test endpoint */
static esp_err_t handleHello(httpd_req_t* req) {
    return WiFiHttpServer::sendJSON(req, "fuck you");
}

/** GET /api/info - device info endpoint */
static esp_err_t handleInfo(httpd_req_t* req) {
    WiFiManager& wifi = WiFiManager::instance();

    char ip[16] = {};
    wifi.getIP(ip, sizeof(ip));
    uint8_t mac[6];
    wifi.getMAC(mac);

    char json[512];
    snprintf(json, sizeof(json),
             "{\n\tfantastic shit\n"
             "\"connected\":%s,"
             "\"ssid\":\"%s\","
             "\"ip\":\"%s\","
             "\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
             "\"rssi\":%d,"
             "\"channel\":%d,"
             "\"heap\":%lu"
             "\"version\":\"1.0.1\"" 
             "}",
             wifi.isConnected() ? "true" : "false",
             wifi.getSSID(), ip,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             wifi.getRSSI(), wifi.getChannel(),
             (unsigned long)esp_get_free_heap_size());

    return WiFiHttpServer::sendJSON(req, json);
}

/* =============================================================================
 * OTA PROGRESS CALLBACK
 * ========================================================================== */

static void onOTAProgress(size_t written, size_t total) {
    if (total > 0) {
        ESP_LOGI(TAG, "OTA: %d / %d (%d%%)",
                 (int)written, (int)total, (int)(written * 100 / total));
    }
}

/* =============================================================================
 * MAIN
 * ========================================================================== */

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
#if defined(WIFI_TEST_STA)
    ESP_LOGI(TAG, "║    WiFi Test - STA MODE                  ║");
#elif defined(WIFI_TEST_AP)
    ESP_LOGI(TAG, "║    WiFi Test - AP MODE                   ║");
#else
    ESP_LOGI(TAG, "║    WiFi Test - NVS MODE                  ║");
#endif
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    WiFiManager& wifi = WiFiManager::instance();
    WiFiHttpServer& server = WiFiHttpServer::instance();

    /* Register event callback */
    wifi.setEventCallback(onWiFiEvent);

    /* Channel change callback for ESP-NOW coordination */
    wifi.setChannelChangeCallback([](uint8_t ch) {
        ESP_LOGI(TAG, "📻 [ESP-NOW] Would update peers to channel %d", ch);
    });

    esp_err_t ret;
    bool need_captive_portal = false;

    /* ── Start WiFi based on mode ──────────────────────────────────── */
#if defined(WIFI_TEST_STA)
    /* Direct STA mode - connect with hardcoded credentials */
    ret = wifi.beginSTA(WIFI_SSID, WIFI_PASS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi STA start failed: %s", esp_err_to_name(ret));
    }

#elif defined(WIFI_TEST_AP)
    /* AP-only mode */
    const char* ap_pass = strlen(WIFI_AP_PASS) > 0 ? WIFI_AP_PASS : nullptr;
    ret = wifi.beginAP(WIFI_AP_SSID, ap_pass);
    need_captive_portal = true;

#else  /* WIFI_TEST_NVS - Default mode */
    /* Try to load from NVS, fall back to AP with captive portal */
    ret = wifi.beginFromNVS(true, WIFI_AP_SSID);
    
    /* beginFromNVS returns ESP_OK even when falling back to AP.
     * We'll check connection status after a delay to determine mode. */
    need_captive_portal = true;  // Assume portal needed, will update after delay
#endif

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* ── Wait for connection (STA modes) ───────────────────────────── */
#if defined(WIFI_TEST_STA)
    /* Wait up to 10 seconds for connection */
    for (int i = 0; i < 100 && !wifi.isConnected(); i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
#elif defined(WIFI_TEST_NVS)
    /* Wait for auto-connect from NVS (up to 5 seconds) */
    ESP_LOGI(TAG, "Waiting for connection...");
    for (int i = 0; i < 50 && !wifi.isConnected(); i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    /* Check if connected now */
    if (wifi.isConnected()) {
        need_captive_portal = false;
        ESP_LOGI(TAG, "✅ Connected from saved credentials!");
    } else {
        ESP_LOGI(TAG, "No saved credentials - starting captive portal");
    }
#endif

    /* ── Start HTTP Server ─────────────────────────────────────────── */
    if (need_captive_portal) {
        /* Start captive portal for WiFi setup */
        server.beginCaptivePortal();
        ESP_LOGI(TAG, "═══════════════════════════════════════════");
        ESP_LOGI(TAG, "  CAPTIVE PORTAL ACTIVE");
        ESP_LOGI(TAG, "  1. Connect to WiFi: %s", WIFI_AP_SSID);
        ESP_LOGI(TAG, "  2. Open browser or wait for popup");
        ESP_LOGI(TAG, "  3. Enter your WiFi credentials");
        ESP_LOGI(TAG, "═══════════════════════════════════════════");
    } else {
        /* Normal mode - custom API routes */
        server.addRoute("/api/hello", HTTP_GET, handleHello);
        server.addRoute("/api/info", HTTP_GET, handleInfo);
        server.begin();
        ESP_LOGI(TAG, "HTTP server started with custom routes");
        ESP_LOGI(TAG, "  GET /api/hello - Test endpoint");
        ESP_LOGI(TAG, "  GET /api/info  - Device info");
    }

    /* ── Start mDNS ────────────────────────────────────────────────── */
    WiFiServices::startMDNS(WIFI_HOSTNAME, "ESP32 Smart Home Device");
    WiFiServices::addMDNSService("_http", "_tcp", 80);
    ESP_LOGI(TAG, "Device reachable at http://%s.local", WIFI_HOSTNAME);

    /* ── Register OTA endpoint ─────────────────────────────────────── */
    WiFiServices::setOTAProgressCallback(onOTAProgress);
    WiFiServices::registerOTAHandler(server.getHandle());
    ESP_LOGI(TAG, "OTA available at POST /api/ota");

    /* ── Validate firmware (if booted from OTA) ────────────────────── */
    WiFiServices::otaValidate();

    /* ── Status reporting loop ─────────────────────────────────────── */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        char ip[16] = {};
        wifi.getIP(ip, sizeof(ip));

        ESP_LOGI(TAG, "[Status] Connected=%s | SSID=%s | IP=%s | RSSI=%d | Ch=%d | Heap=%lu",
                 wifi.isConnected() ? "yes" : "no",
                 wifi.getSSID(), ip, wifi.getRSSI(), wifi.getChannel(),
                 (unsigned long)esp_get_free_heap_size());

#if defined(WIFI_TEST_STA) || defined(WIFI_TEST_NVS)
        /* Test HTTP client when connected to internet */
        if (wifi.isConnected()) {
            char resp[512] = {};
            int status = WiFiHttpClient::get("http://httpbin.org/ip", resp, sizeof(resp));
            if (status == 200) {
                ESP_LOGI(TAG, "HTTP GET test: %s", resp);
            } else {
                ESP_LOGW(TAG, "HTTP GET test failed: status=%d", status);
            }
        }
#endif
    }
}