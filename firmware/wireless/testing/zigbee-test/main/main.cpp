/*
 * =============================================================================
 * FILE:        main.cpp
 * PROJECT:     zigbee-test
 * DESCRIPTION: Test for Zigbee Manager component.
 * =============================================================================
 * 
 * MODES (set via build flags):
 * 
 *   -DZB_TEST_ONOFF_LIGHT  → On/Off light end device (default)
 *   -DZB_TEST_DIMMABLE     → Dimmable light router
 *   -DZB_TEST_TEMP_SENSOR  → Temperature sensor end device
 *   -DZB_TEST_COORDINATOR  → Coordinator (forms network)
 * 
 * TESTING:
 *   1. Flash coordinator on one C6 board
 *   2. Flash end device (light or sensor) on another C6 board
 *   3. Power coordinator first, then end device
 *   4. End device should join automatically
 *   5. Use Zigbee2MQTT or ZHA to control
 * 
 * =============================================================================
 */

#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "zigbee_manager.h"

static const char* TAG = "ZBTest";

/* Default to On/Off Light */
#if !defined(ZB_TEST_ONOFF_LIGHT) && !defined(ZB_TEST_DIMMABLE) && \
    !defined(ZB_TEST_TEMP_SENSOR) && !defined(ZB_TEST_COORDINATOR)
#define ZB_TEST_ONOFF_LIGHT
#endif

/* LED pin for light demos (XIAO C6 = GPIO15, generic C6 devkit = GPIO8) */
#ifndef LED_PIN
#define LED_PIN 15
#endif

/* ─── LED Control (for light modes) ──────────────────────────────────────── */

static void initLED() {
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << LED_PIN);
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level((gpio_num_t)LED_PIN, 0);
}

static void onOnOff(bool on) {
    ESP_LOGI(TAG, "LED → %s", on ? "ON" : "OFF");
    gpio_set_level((gpio_num_t)LED_PIN, on ? 1 : 0);
}

static void onLevel(uint8_t level) {
    /* Simple threshold: >127 = on, else off
     * (Real implementation would use PWM) */
    bool on = (level > 0);
    ESP_LOGI(TAG, "Level → %d (%s)", level, on ? "ON" : "OFF");
    gpio_set_level((gpio_num_t)LED_PIN, on ? 1 : 0);
}

/* ─── Network Event Callback ─────────────────────────────────────────────── */

static void onNetwork(ZBEvent event, const ZBEventInfo* info) {
    switch (event) {
        case ZBEvent::STACK_READY:
            ESP_LOGI(TAG, "Stack ready");
            break;
        case ZBEvent::NETWORK_FORMED:
            ESP_LOGI(TAG, "Network formed: PAN=0x%04X CH=%d",
                     info->pan_id, info->channel);
            break;
        case ZBEvent::NETWORK_JOINED:
            ESP_LOGI(TAG, "Joined network: PAN=0x%04X CH=%d Addr=0x%04X",
                     info->pan_id, info->channel, info->short_addr);
            break;
        case ZBEvent::NETWORK_FAILED:
            ESP_LOGW(TAG, "Network join failed, retrying...");
            break;
        case ZBEvent::NETWORK_LEFT:
            ESP_LOGW(TAG, "Left network");
            break;
        case ZBEvent::PERMIT_JOIN:
            ESP_LOGI(TAG, "Permit join: %s (%ds)",
                     info->permit_join_open ? "OPEN" : "CLOSED",
                     info->permit_duration);
            break;
        default:
            break;
    }
}

/* =============================================================================
 * MAIN
 * ========================================================================== */

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
#if defined(ZB_TEST_ONOFF_LIGHT)
    ESP_LOGI(TAG, "║   Zigbee Test - ON/OFF LIGHT (ED)         ║");
#elif defined(ZB_TEST_DIMMABLE)
    ESP_LOGI(TAG, "║   Zigbee Test - DIMMABLE LIGHT (Router)   ║");
#elif defined(ZB_TEST_TEMP_SENSOR)
    ESP_LOGI(TAG, "║   Zigbee Test - TEMP SENSOR (ED)          ║");
#else
    ESP_LOGI(TAG, "║   Zigbee Test - COORDINATOR               ║");
#endif
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    /* NVS init (required for Zigbee NVRAM) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ZigbeeManager& zb = ZigbeeManager::instance();
    zb.setNetworkCallback(onNetwork);

    ZBConfig cfg;
    cfg.endpoint = 10;
    cfg.erase_nvram = false;  // Set to true for factory reset
    strncpy(cfg.manufacturer, "SmartHome", sizeof(cfg.manufacturer));
    strncpy(cfg.model, "ESP32-C6", sizeof(cfg.model));

    /* ── ON/OFF LIGHT MODE ─────────────────────────────────────────── */
#if defined(ZB_TEST_ONOFF_LIGHT)
    initLED();
    zb.setOnOffCallback(onOnOff);
    zb.begin(ZBRole::END_DEVICE, ZBDeviceType::ON_OFF_LIGHT, cfg);

    while (true) {
        if (zb.isJoined()) {
            ESP_LOGI(TAG, "Light active | Addr=0x%04X CH=%d",
                     zb.getShortAddr(), zb.getChannel());
        } else {
            ESP_LOGI(TAG, "Waiting to join network...");
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    /* ── DIMMABLE LIGHT MODE ───────────────────────────────────────── */
#elif defined(ZB_TEST_DIMMABLE)
    initLED();
    zb.setOnOffCallback(onOnOff);
    zb.setLevelCallback(onLevel);
    zb.begin(ZBRole::ROUTER, ZBDeviceType::DIMMABLE_LIGHT, cfg);

    while (true) {
        if (zb.isJoined()) {
            ESP_LOGI(TAG, "Dimmable light active | Addr=0x%04X",
                     zb.getShortAddr());
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    /* ── TEMPERATURE SENSOR MODE ───────────────────────────────────── */
#elif defined(ZB_TEST_TEMP_SENSOR)
    zb.begin(ZBRole::END_DEVICE, ZBDeviceType::TEMPERATURE_SENSOR, cfg);

    float simulated_temp = 22.0f;

    while (true) {
        if (zb.isJoined()) {
            /* Simulate temperature fluctuation */
            simulated_temp += ((float)(esp_random() % 100) - 50) / 100.0f;
            if (simulated_temp < 15.0f) simulated_temp = 15.0f;
            if (simulated_temp > 35.0f) simulated_temp = 35.0f;

            zb.reportTemperature(simulated_temp);
            ESP_LOGI(TAG, "Reported: %.2f°C", simulated_temp);
        } else {
            ESP_LOGI(TAG, "Waiting to join network...");
        }
        vTaskDelay(pdMS_TO_TICKS(30000));  // Report every 30s
    }

    /* ── COORDINATOR MODE ──────────────────────────────────────────── */
#else
    /* Coordinator forms the network. Other devices join it. */
    zb.begin(ZBRole::COORDINATOR, ZBDeviceType::ON_OFF_LIGHT, cfg);

    while (true) {
        if (zb.isJoined()) {
            ESP_LOGI(TAG, "Coordinator running | PAN=0x%04X CH=%d",
                     zb.getPanId(), zb.getChannel());
        } else {
            ESP_LOGI(TAG, "Forming network...");
        }
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
#endif
}
