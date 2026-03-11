/*
 * =============================================================================
 * FILE:        main.cpp
 * PROJECT:     ota-test
 * DESCRIPTION: Test for OTA Manager component.
 * =============================================================================
 * 
 * MODES (set via build flags):
 * 
 *   -DOTA_TEST_WEBUI     → Start WiFi AP + HTTP server + OTA web UI (default)
 *   -DOTA_TEST_PULL      → Connect to WiFi STA + check server for updates
 *   -DOTA_TEST_ROLLBACK  → Demonstrate rollback validation flow
 * 
 * All modes demonstrate:
 *   - Version reporting from esp_app_desc
 *   - Partition info display
 *   - Event callbacks
 * 
 * =============================================================================
 */

#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "ota_manager.h"

static const char* TAG = "OTATest";

/* Default to web UI mode */
#if !defined(OTA_TEST_WEBUI) && !defined(OTA_TEST_PULL) && !defined(OTA_TEST_ROLLBACK)
#define OTA_TEST_WEBUI
#endif

/* WiFi credentials for STA modes */
#ifndef WIFI_SSID
#define WIFI_SSID "YourNetwork"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "YourPassword"
#endif

/* Update server URL for pull mode */
#ifndef OTA_UPDATE_URL
#define OTA_UPDATE_URL "http://192.168.1.100:8080/firmware"
#endif

/* ─── OTA Event Callback ─────────────────────────────────────────────────── */

static void onOTAEvent(OTAEvent event, const OTAEventInfo* info) {
    switch (event) {
        case OTAEvent::UPDATE_STARTED:
            ESP_LOGI(TAG, "OTA update started!");
            break;

        case OTAEvent::PROGRESS:
            if (info) {
                ESP_LOGI(TAG, "OTA progress: %.1f%% (%lu bytes)",
                         info->progress_pct, (unsigned long)info->bytes_written);
            }
            break;

        case OTAEvent::UPDATE_COMPLETE:
            ESP_LOGI(TAG, "OTA complete! Rebooting...");
            break;

        case OTAEvent::UPDATE_FAILED:
            ESP_LOGE(TAG, "OTA failed: %s", info ? info->error_msg : "unknown");
            break;

        case OTAEvent::ROLLBACK_PENDING:
            ESP_LOGW(TAG, "Firmware is PENDING VALIDATION!");
            break;

        case OTAEvent::VALIDATED:
            ESP_LOGI(TAG, "Firmware validated successfully!");
            break;

        case OTAEvent::ROLLED_BACK:
            ESP_LOGW(TAG, "Rolling back to previous firmware!");
            break;

        case OTAEvent::VERSION_CHECK:
            if (info) {
                ESP_LOGI(TAG, "Server version: %s → %s",
                         info->new_version,
                         info->update_available ? "UPDATE AVAILABLE" : "up to date");
            }
            break;
    }
}

/* ─── Simple WiFi AP for Web UI mode ─────────────────────────────────────── */

static void start_wifi_ap() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    wifi_config_t ap_config = {};
    strcpy((char*)ap_config.ap.ssid, "ESP32-OTA-Update");
    ap_config.ap.ssid_len = strlen("ESP32-OTA-Update");
    ap_config.ap.channel = 1;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 4;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started: \"ESP32-OTA-Update\" (open)");
    ESP_LOGI(TAG, "Connect and browse to http://192.168.4.1/ota");
}

/* ─── Simple WiFi STA for pull mode ──────────────────────────────────────── */

#ifdef OTA_TEST_PULL
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    }
}

static void start_wifi_sta() {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any_id, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, nullptr, &inst_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, nullptr, &inst_got_ip));

    wifi_config_t sta_config = {};
    strcpy((char*)sta_config.sta.ssid, WIFI_SSID);
    strcpy((char*)sta_config.sta.password, WIFI_PASS);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to %s...", WIFI_SSID);
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}
#endif

/* ─── HTTP Server ────────────────────────────────────────────────────────── */

static httpd_handle_t start_http_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    /* Increase stack for OTA writes */
    config.stack_size = 8192;
    /* Allow large firmware uploads */
    config.recv_wait_timeout = 30;

    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return nullptr;
    }
    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return server;
}

/* =============================================================================
 * MAIN
 * ========================================================================== */

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
#if defined(OTA_TEST_WEBUI)
    ESP_LOGI(TAG, "║      OTA Test - WEB UI MODE              ║");
#elif defined(OTA_TEST_PULL)
    ESP_LOGI(TAG, "║      OTA Test - PULL UPDATE MODE          ║");
#else
    ESP_LOGI(TAG, "║      OTA Test - ROLLBACK DEMO MODE        ║");
#endif
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    /* NVS init */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ── Initialize OTA Manager ────────────────────────────────────── */
    OTAManager& ota = OTAManager::instance();
    ota.setEventCallback(onOTAEvent);

#if defined(OTA_TEST_ROLLBACK)
    /* Rollback mode: use 30s validation timeout */
    ota.begin(30);
#else
    /* Other modes: 60s default timeout */
    ota.begin(60);
#endif

    /* Print partition info */
    OTAPartitionInfo pinfo = {};
    ota.getPartitionInfo(pinfo);
    ESP_LOGI(TAG, "Running: %s v%s", pinfo.running_label, pinfo.running_version);
    ESP_LOGI(TAG, "Next:    %s", pinfo.next_label);

    /* ── WEB UI MODE ───────────────────────────────────────────────── */
#if defined(OTA_TEST_WEBUI)
    start_wifi_ap();

    httpd_handle_t server = start_http_server();
    if (server) {
        ota.registerWebUI(server);
        ota.registerUploadHandler(server);
        ota.registerStatusHandler(server);
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "  Browse to: http://192.168.4.1/ota");
        ESP_LOGI(TAG, "");
    }

    /* Validate immediately in web UI mode (it's for testing) */
    ota.validate();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Version: %s | Connections active", ota.getVersion());
    }

    /* ── PULL UPDATE MODE ──────────────────────────────────────────── */
#elif defined(OTA_TEST_PULL)
    start_wifi_sta();

    /* Also start HTTP server for status/web UI */
    httpd_handle_t server = start_http_server();
    if (server) {
        ota.registerWebUI(server);
        ota.registerUploadHandler(server);
        ota.registerStatusHandler(server);
    }

    /* Validate current firmware */
    ota.validate();

    /* Set update server URL */
    ota.setUpdateURL(OTA_UPDATE_URL);

    /* Check for update every 60 seconds */
    while (true) {
        ESP_LOGI(TAG, "Checking for updates...");
        ota.checkForUpdate(true);  // auto_update = true

        vTaskDelay(pdMS_TO_TICKS(60000));
    }

    /* ── ROLLBACK DEMO MODE ────────────────────────────────────────── */
#else
    start_wifi_ap();

    httpd_handle_t server = start_http_server();
    if (server) {
        ota.registerWebUI(server);
        ota.registerUploadHandler(server);
        ota.registerStatusHandler(server);
    }

    if (ota.isPendingValidation()) {
        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "  This firmware is UNVALIDATED!");
        ESP_LOGW(TAG, "  It will auto-rollback in 30 seconds");
        ESP_LOGW(TAG, "  unless you validate via the web UI.");
        ESP_LOGW(TAG, "");

        /* Simulate self-test: wait 10s then validate */
        ESP_LOGI(TAG, "Running self-tests...");
        vTaskDelay(pdMS_TO_TICKS(10000));

        /* Uncomment to auto-validate, or leave commented to see rollback */
        // ota.validate();

        ESP_LOGW(TAG, "Self-tests not calling validate() - rollback will happen in ~20s");
    } else {
        ESP_LOGI(TAG, "Firmware is already validated.");
        ota.validate();
    }

    while (true) {
        ESP_LOGI(TAG, "Running... pending=%s", ota.isPendingValidation() ? "YES" : "no");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
#endif
}
