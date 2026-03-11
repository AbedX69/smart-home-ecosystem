/*
 * =============================================================================
 * FILE:        wifi_http_server.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-13
 * VERSION:     1.0.0
 * PLATFORM:    ESP32 / ESP32-S3 / ESP32-C6 (ESP-IDF v5.x)
 * =============================================================================
 * 
 * WiFi HTTP Server - Lightweight web server for ESP32 devices.
 * 
 * Built on ESP-IDF's httpd component. Provides:
 *   - Easy route registration (GET, POST, PUT, DELETE)
 *   - Built-in captive portal WiFi setup page
 *   - JSON response helpers
 *   - Static content serving
 * 
 * =============================================================================
 * BEGINNER'S GUIDE: HTTP SERVER ON ESP32
 * =============================================================================
 * 
 * WHAT IS AN HTTP SERVER?
 * ~~~~~~~~~~~~~~~~~~~~~~~
 * An HTTP server lets your ESP32 serve web pages - just like Apache or Nginx
 * do for big websites, but tiny and running on a microcontroller.
 * 
 * When you type an address in your browser:
 * 
 *     Browser                    ESP32
 *     ┌──────┐   GET /         ┌──────┐
 *     │      │ ───────────────►│      │
 *     │      │                 │ HTTP │
 *     │      │  ◄───────────── │Server│
 *     │      │  <html>...</html>│      │
 *     └──────┘                 └──────┘
 * 
 * The browser sends a REQUEST (GET /), and the ESP32 sends back a RESPONSE
 * (HTML page, JSON data, etc.).
 * 
 * 
 * ROUTES:
 * ~~~~~~~
 * A "route" maps a URL path to a handler function:
 * 
 *     GET  /           → serve the main page
 *     GET  /api/status → return JSON with device status
 *     POST /api/wifi   → receive new WiFi credentials
 *     GET  /api/scan   → return list of WiFi networks
 * 
 * 
 * =============================================================================
 * CAPTIVE PORTAL
 * =============================================================================
 * 
 * When enabled, ALL HTTP requests are redirected to the setup page.
 * This is how phones detect captive portals:
 * 
 *     Phone connects to ESP32 AP
 *           │
 *           ▼
 *     Phone tries http://connectivitycheck.gstatic.com
 *           │
 *           ▼
 *     ESP32 DNS redirects ALL domains → 192.168.4.1
 *           │
 *           ▼
 *     ESP32 HTTP server serves WiFi setup page
 *           │
 *           ▼
 *     Phone shows "Sign in to network" popup
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "wifi_http_server.h"
 * 
 *     // Custom route handler
 *     esp_err_t handleStatus(httpd_req_t* req) {
 *         const char* json = "{\"status\":\"ok\",\"uptime\":12345}";
 *         httpd_resp_set_type(req, "application/json");
 *         return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
 *     }
 * 
 *     extern "C" void app_main(void) {
 *         // ... start WiFi first ...
 *         
 *         WiFiHttpServer& server = WiFiHttpServer::instance();
 *         server.addRoute("/api/status", HTTP_GET, handleStatus);
 *         server.begin();
 *         
 *         // Or start with captive portal:
 *         // server.beginCaptivePortal();
 *     }
 * 
 * =============================================================================
 */

#ifndef WIFI_HTTP_SERVER_H
#define WIFI_HTTP_SERVER_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"

/* ─── Constants ──────────────────────────────────────────────────────────── */
#define HTTP_SERVER_MAX_ROUTES  16
#define HTTP_SERVER_DEFAULT_PORT 80

/* ─── Main Class ─────────────────────────────────────────────────────────── */

class WiFiHttpServer {
public:
    static WiFiHttpServer& instance();
    WiFiHttpServer(const WiFiHttpServer&) = delete;
    WiFiHttpServer& operator=(const WiFiHttpServer&) = delete;

    /* ─── Lifecycle ────────────────────────────────────────────────────── */

    /**
     * @brief Start the HTTP server.
     * @param port  Port to listen on (default 80)
     * @return ESP_OK on success
     */
    esp_err_t begin(uint16_t port = HTTP_SERVER_DEFAULT_PORT);

    /**
     * @brief Start with captive portal (WiFi setup page + DNS redirect).
     * 
     * This registers built-in routes for WiFi configuration:
     *   GET  /           → WiFi setup page (HTML)
     *   GET  /api/scan   → Scan for WiFi networks (JSON)
     *   POST /api/wifi   → Save WiFi credentials and connect
     *   GET  /api/status → Current WiFi status (JSON)
     * 
     * Also starts a DNS server that redirects all queries to this device.
     * 
     * @return ESP_OK on success
     */
    esp_err_t beginCaptivePortal();

    /**
     * @brief Stop the HTTP server.
     * @return ESP_OK on success
     */
    esp_err_t stop();

    /** @brief Check if server is running */
    bool isRunning() const;

    /* ─── Route Registration ───────────────────────────────────────────── */

    /**
     * @brief Register a route handler.
     * 
     * @param uri       URL path (e.g., "/api/status")
     * @param method    HTTP method (HTTP_GET, HTTP_POST, etc.)
     * @param handler   Handler function
     * @param user_ctx  Optional user context passed to handler
     * @return ESP_OK on success
     * 
     * @note Must be called BEFORE begin().
     *       If called after begin(), the route is registered immediately.
     */
    esp_err_t addRoute(const char* uri, httpd_method_t method,
                       esp_err_t (*handler)(httpd_req_t*),
                       void* user_ctx = nullptr);

    /* ─── Response Helpers ─────────────────────────────────────────────── */

    /** @brief Send a JSON response */
    static esp_err_t sendJSON(httpd_req_t* req, const char* json);

    /** @brief Send an HTML response */
    static esp_err_t sendHTML(httpd_req_t* req, const char* html);

    /** @brief Send a 302 redirect */
    static esp_err_t sendRedirect(httpd_req_t* req, const char* location);

    /** @brief Send a plain text response */
    static esp_err_t sendText(httpd_req_t* req, const char* text);

    /** @brief Send an error response */
    static esp_err_t sendError(httpd_req_t* req, httpd_err_code_t code, const char* msg);

    /** @brief Read POST body into buffer. Returns bytes read. */
    static int readBody(httpd_req_t* req, char* buf, size_t buf_len);

    /** @brief Get a URL query parameter value */
    static bool getQueryParam(httpd_req_t* req, const char* key, char* val, size_t val_len);

    /** @brief Get the raw httpd handle (for advanced use) */
    httpd_handle_t getHandle() const;

private:
    WiFiHttpServer();
    ~WiFiHttpServer();

    /* Built-in captive portal handlers */
    static esp_err_t captiveRootHandler(httpd_req_t* req);
    static esp_err_t captiveScanHandler(httpd_req_t* req);
    static esp_err_t captiveWifiHandler(httpd_req_t* req);
    static esp_err_t captiveStatusHandler(httpd_req_t* req);
    static esp_err_t captiveCatchAllHandler(httpd_req_t* req);

    /* DNS server for captive portal */
    esp_err_t startDNS();
    esp_err_t stopDNS();
    static void dnsTask(void* arg);

    httpd_handle_t  _server;
    bool            _running;
    bool            _captive_portal;
    TaskHandle_t    _dns_task;
    int             _dns_socket;

    /* Pending routes (added before begin()) */
    struct PendingRoute {
        httpd_uri_t uri_handler;
        bool        used;
    };
    PendingRoute _pending[HTTP_SERVER_MAX_ROUTES];
};

#endif // WIFI_HTTP_SERVER_H
