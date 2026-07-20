/*
 * =============================================================================
 * FILE:        wifi_services.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-13
 * VERSION:     1.0.0
 * PLATFORM:    ESP32 / ESP32-S3 / ESP32-C6 (ESP-IDF v5.x)
 * =============================================================================
 * 
 * WiFi Services - mDNS discovery and OTA (Over-The-Air) firmware updates.
 * 
 * =============================================================================
 * BEGINNER'S GUIDE: mDNS & OTA
 * =============================================================================
 * 
 * mDNS (Multicast DNS)
 * ~~~~~~~~~~~~~~~~~~~~
 * Instead of typing 192.168.1.42 to reach your ESP32, mDNS lets you use a 
 * friendly name like "smartlight.local". No DNS server needed - devices on 
 * the local network discover each other automatically.
 * 
 *     Browser                          Local Network
 *     ┌──────────┐   "smartlight.local"  ┌──────────┐
 *     │          │ ──────── mDNS ───────►│  ESP32   │
 *     │          │   resolves to          │ 192.168  │
 *     │          │   192.168.1.42         │ .1.42    │
 *     └──────────┘                        └──────────┘
 * 
 * Perfect for smart home: name each device by its function.
 * 
 * 
 * OTA (Over-The-Air Updates)
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Update your ESP32 firmware without plugging in a USB cable! Upload a new
 * .bin file via HTTP and the ESP32 writes it to the OTA partition and reboots.
 * 
 *     ┌──────────┐   POST /api/ota     ┌──────────┐
 *     │  Browser  │ ──── .bin file ────►│  ESP32   │
 *     │          │                      │          │
 *     │          │                      │ Writes   │
 *     │          │                      │ to OTA   │
 *     │          │                      │ partition│
 *     └──────────┘                      └──┬───────┘
 *                                          │ reboot
 *                                          ▼
 *                                     New firmware!
 * 
 * IMPORTANT: Your partition table must have OTA partitions (ota_0, ota_1).
 * The default "single factory" partition won't work for OTA.
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "wifi_services.h"
 * 
 *     extern "C" void app_main(void) {
 *         // ... start WiFi first ...
 *         
 *         // Start mDNS so "smartlight.local" works
 *         WiFiServices::startMDNS("smartlight", "Smart Light Controller");
 *         
 *         // Add a service advertisement (tells other devices we exist)
 *         WiFiServices::addMDNSService("_http", "_tcp", 80);
 *         
 *         // Register OTA endpoint on your HTTP server
 *         WiFiServices::registerOTAHandler(server.getHandle());
 *     }
 * 
 * =============================================================================
 */

#ifndef WIFI_SERVICES_H
#define WIFI_SERVICES_H

#include <cstdint>
#include <functional>
#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief OTA progress callback.
 * @param bytes_written  Bytes written so far
 * @param total_bytes    Total firmware size (0 if unknown)
 */
using OTAProgressCb = std::function<void(size_t bytes_written, size_t total_bytes)>;

class WiFiServices {
public:
    /* ─── mDNS ─────────────────────────────────────────────────────────── */

    /**
     * @brief Start mDNS responder.
     * 
     * After this, the device is reachable at <hostname>.local
     * 
     * @param hostname    Device name (e.g., "smartlight" → smartlight.local)
     * @param instance    Human-readable instance name (shown in discovery)
     * @return ESP_OK on success
     */
    static esp_err_t startMDNS(const char* hostname,
                                const char* instance = nullptr);

    /**
     * @brief Stop mDNS responder.
     */
    static esp_err_t stopMDNS();

    /**
     * @brief Advertise a service via mDNS.
     * 
     * This tells other devices on the network what services we offer.
     * E.g., "_http._tcp" on port 80 means "I have a web server."
     * 
     * @param service   Service type (e.g., "_http")
     * @param proto     Protocol (e.g., "_tcp")
     * @param port      Port number
     * @return ESP_OK on success
     */
    static esp_err_t addMDNSService(const char* service, const char* proto,
                                     uint16_t port);

    /* ─── OTA Updates ──────────────────────────────────────────────────── */

    /**
     * @brief Register OTA update endpoint on an HTTP server.
     * 
     * Registers POST /api/ota that accepts a firmware .bin upload.
     * The firmware is written to the next OTA partition and the device reboots.
     * 
     * @param server    httpd_handle_t from WiFiHttpServer::getHandle()
     * @return ESP_OK on success
     * 
     * @note Partition table must have OTA partitions.
     */
    static esp_err_t registerOTAHandler(httpd_handle_t server);

    /**
     * @brief Set a progress callback for OTA updates.
     * @param cb  Callback function
     */
    static void setOTAProgressCallback(OTAProgressCb cb);

    /**
     * @brief Perform OTA from a URL (pull-based update).
     * 
     * Downloads firmware from the given URL and flashes it.
     * 
     * @param url  URL to the .bin firmware file
     * @return ESP_OK on success (device will reboot)
     */
    static esp_err_t otaFromURL(const char* url);

    /**
     * @brief Rollback to the previous OTA firmware.
     * 
     * Marks the previous OTA partition as active and reboots.
     * Only works if the current firmware hasn't been validated yet.
     * 
     * @return ESP_OK on success
     */
    static esp_err_t otaRollback();

    /**
     * @brief Mark current firmware as valid (prevent auto-rollback).
     * @return ESP_OK on success
     */
    static esp_err_t otaValidate();

private:
    static esp_err_t otaUploadHandler(httpd_req_t* req);
    static OTAProgressCb _ota_progress_cb;
};

#endif // WIFI_SERVICES_H
