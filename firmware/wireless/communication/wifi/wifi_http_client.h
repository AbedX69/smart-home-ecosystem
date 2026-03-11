/*
 * =============================================================================
 * FILE:        wifi_http_client.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-13
 * VERSION:     1.0.0
 * PLATFORM:    ESP32 / ESP32-S3 / ESP32-C6 (ESP-IDF v5.x)
 * =============================================================================
 * 
 * WiFi HTTP Client - Simple wrapper for making HTTP requests from ESP32.
 * 
 * Built on ESP-IDF's esp_http_client. Provides simple GET/POST methods
 * that return the response body as a string.
 * 
 * =============================================================================
 * BEGINNER'S GUIDE: HTTP CLIENT
 * =============================================================================
 * 
 * An HTTP client is the opposite of a server - it MAKES requests instead
 * of receiving them. Your ESP32 can:
 * 
 *     ESP32 (client)              Remote Server
 *     ┌──────────┐  GET /api     ┌──────────────┐
 *     │          │ ─────────────►│              │
 *     │          │               │  weather.com │
 *     │          │ ◄───────────── │              │
 *     │          │  JSON data    │              │
 *     └──────────┘               └──────────────┘
 * 
 * Common uses: send sensor data to cloud, fetch weather, call APIs.
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "wifi_http_client.h"
 * 
 *     // Simple GET
 *     char response[1024];
 *     int status = WiFiHttpClient::get("http://api.example.com/data",
 *                                       response, sizeof(response));
 *     if (status == 200) {
 *         ESP_LOGI("APP", "Response: %s", response);
 *     }
 * 
 *     // POST with JSON body
 *     const char* json = "{\"temp\":22.5}";
 *     status = WiFiHttpClient::post("http://api.example.com/sensor",
 *                                    json, response, sizeof(response));
 * 
 * =============================================================================
 */

#ifndef WIFI_HTTP_CLIENT_H
#define WIFI_HTTP_CLIENT_H

#include <cstdint>
#include "esp_err.h"
#include "esp_http_client.h"

class WiFiHttpClient {
public:
    /**
     * @brief Perform an HTTP GET request.
     * 
     * @param url           Full URL (http:// or https://)
     * @param response_buf  Buffer to store response body
     * @param buf_len       Size of response buffer
     * @param timeout_ms    Request timeout (default 10s)
     * @return HTTP status code (200, 404, etc.) or -1 on error
     */
    static int get(const char* url, char* response_buf, size_t buf_len,
                   int timeout_ms = 10000);

    /**
     * @brief Perform an HTTP POST request with a body.
     * 
     * @param url           Full URL
     * @param body          Request body (JSON, form data, etc.)
     * @param response_buf  Buffer to store response body
     * @param buf_len       Size of response buffer
     * @param content_type  Content-Type header (default "application/json")
     * @param timeout_ms    Request timeout
     * @return HTTP status code or -1 on error
     */
    static int post(const char* url, const char* body,
                    char* response_buf, size_t buf_len,
                    const char* content_type = "application/json",
                    int timeout_ms = 10000);

    /**
     * @brief Perform an HTTP PUT request with a body.
     */
    static int put(const char* url, const char* body,
                   char* response_buf, size_t buf_len,
                   const char* content_type = "application/json",
                   int timeout_ms = 10000);

    /**
     * @brief Perform an HTTP DELETE request.
     */
    static int del(const char* url, char* response_buf, size_t buf_len,
                   int timeout_ms = 10000);

private:
    /** @brief Internal shared implementation */
    static int performRequest(esp_http_client_method_t method,
                              const char* url, const char* body,
                              const char* content_type,
                              char* response_buf, size_t buf_len,
                              int timeout_ms);

    /** @brief HTTP event handler (collects response data) */
    static esp_err_t httpEventHandler(esp_http_client_event_t* evt);
};

#endif // WIFI_HTTP_CLIENT_H
