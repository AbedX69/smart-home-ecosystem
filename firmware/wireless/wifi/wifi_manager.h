/*
 * =============================================================================
 * FILE:        wifi_manager.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-13
 * MODIFIED:    2026-02-13
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    ESP32 / ESP32-S3 / ESP32-C6 (ESP-IDF v5.x)
 * =============================================================================
 * 
 * WiFi Manager - Comprehensive WiFi management for ESP32 smart home devices.
 * 
 * Handles STA mode (connect to router), AP mode (be a hotspot), and STA+AP 
 * mode (both simultaneously). Includes auto-reconnect, NVS credential storage,
 * captive portal for initial WiFi setup, and optional ESP-NOW channel 
 * coordination.
 * 
 * =============================================================================
 * BEGINNER'S GUIDE: WiFi ON ESP32
 * =============================================================================
 * 
 * WHAT IS WiFi?
 * ~~~~~~~~~~~~~
 * WiFi lets your ESP32 connect to the internet through a wireless router,
 * just like your phone or laptop does. Once connected, your ESP32 can:
 *   - Host a web page you can view on your phone
 *   - Send data to cloud services
 *   - Receive commands from anywhere in the world
 *   - Stream video (ESP32-CAM)
 *   - Communicate with other WiFi devices on the network
 * 
 * 
 * THREE WiFi MODES ON ESP32:
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~
 * 
 * 1. STATION MODE (STA) - "Connect to a router"
 * 
 *     ┌──────────┐        WiFi         ┌──────────┐       ┌──────────┐
 *     │  ESP32   │ ◄──────────────────► │  Router  │ ────► │ Internet │
 *     │  (STA)   │   connects to AP     │          │       │          │
 *     └──────────┘                       └──────────┘       └──────────┘
 * 
 *    Your ESP32 joins your home WiFi network. It gets an IP address from the
 *    router (via DHCP) and can access the internet.
 * 
 * 
 * 2. ACCESS POINT MODE (AP) - "Be a router"
 * 
 *     ┌──────────┐        WiFi         ┌──────────┐
 *     │  Phone   │ ◄──────────────────► │  ESP32   │
 *     │          │   connects to ESP32  │  (AP)    │
 *     └──────────┘                       └──────────┘
 * 
 *    Your ESP32 creates its own WiFi network. Other devices (phones, laptops)
 *    can connect to it directly. NO internet access - just local communication.
 *    Perfect for initial setup or direct device control.
 * 
 * 
 * 3. STA+AP MODE - "Both at once"
 * 
 *     ┌──────────┐                       ┌──────────┐       ┌──────────┐
 *     │  Phone   │ ◄─── AP ────────────► │  ESP32   │ ────► │  Router  │
 *     │          │   direct connection   │ (STA+AP) │  STA  │          │
 *     └──────────┘                       └──────────┘       └──────────┘
 * 
 *    ESP32 is connected to your router AND hosting its own network.
 *    This is the "captive portal" mode - ESP32 hosts a setup page on its
 *    AP while maintaining internet access through the STA connection.
 * 
 *    IMPORTANT: In STA+AP mode, the AP is FORCED to the same channel as the
 *    STA connection. You can't pick the AP channel independently.
 * 
 * 
 * =============================================================================
 * AUTO-RECONNECT: HOW IT WORKS
 * =============================================================================
 * 
 *     Boot
 *      │
 *      ▼
 *     ┌───────────────────┐
 *     │ Load creds from   │
 *     │ NVS (if saved)    │
 *     └────────┬──────────┘
 *              │
 *              ▼
 *     ┌───────────────────┐     success    ┌──────────────────┐
 *     │ Try to connect    │ ──────────────►│    CONNECTED     │
 *     │ to saved AP       │                │  (got IP addr)   │
 *     └────────┬──────────┘                └────────┬─────────┘
 *              │ fail                               │
 *              ▼                                    │ disconnect
 *     ┌───────────────────┐                         │ event
 *     │ Wait & retry      │◄───────────────────────┘
 *     │ (backoff: 1→30s)  │
 *     └────────┬──────────┘
 *              │ max retries
 *              ▼
 *     ┌───────────────────┐
 *     │ Start AP mode     │  ← Optional: captive portal fallback
 *     │ for configuration │
 *     └───────────────────┘
 * 
 * NVS (Non-Volatile Storage) persists credentials across reboots.
 * The backoff increases delay between retries to avoid hammering the router.
 * 
 * 
 * =============================================================================
 * ESP-NOW CHANNEL COORDINATION
 * =============================================================================
 * 
 * PROBLEM: ESP-NOW and WiFi share the same radio. When connected to a router
 * in STA mode, the WiFi channel is locked to whatever the router uses.
 * ESP-NOW peers MUST be on the same channel to communicate.
 * 
 *     Router (channel 6)
 *          │
 *          ▼
 *     ┌──────────┐  channel 6    ┌──────────┐
 *     │  ESP32   │ ◄────────────►│  ESP32   │  ← Must also be on ch 6!
 *     │  (STA)   │   ESP-NOW     │  (peer)  │
 *     └──────────┘               └──────────┘
 * 
 * SOLUTION: This component provides getChannel() so the ESP-NOW manager
 * can query what channel WiFi is using. When WiFi connects, it fires a 
 * callback with the channel number.
 * 
 * Use setChannelChangeCallback() to get notified when the channel changes
 * (e.g., after connecting to a router). Then update your ESP-NOW peers.
 * 
 * If using WiFi Manager standalone (without ESP-NOW), just ignore this.
 * 
 * 
 * =============================================================================
 * CAPTIVE PORTAL
 * =============================================================================
 * 
 * A captive portal is the page that pops up when you connect to hotel WiFi.
 * We use the same trick for initial ESP32 setup:
 * 
 *   1. ESP32 starts in AP mode (creates "SmartHome-Setup" network)
 *   2. User connects to it from their phone
 *   3. Phone automatically opens the captive portal page
 *   4. User enters their home WiFi SSID and password
 *   5. ESP32 saves credentials to NVS and connects to the home WiFi
 *   6. AP shuts down (or stays up if STA+AP mode)
 * 
 * The captive portal is served by the HTTP server component (wifi_http_server).
 * This component handles the WiFi side - starting AP, DNS redirect, etc.
 * 
 * 
 * =============================================================================
 * NVS CREDENTIAL STORAGE
 * =============================================================================
 * 
 * Credentials are stored in NVS under the namespace "wifi_mgr":
 *   Key "ssid"     → WiFi network name (max 32 chars)
 *   Key "password"  → WiFi password (max 64 chars)
 * 
 * Call saveCredentials() to persist, loadCredentials() to retrieve,
 * clearCredentials() to erase.
 * 
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "wifi_manager.h"
 * 
 *     void onWiFiEvent(WiFiEvent event, void* data) {
 *         switch (event) {
 *             case WiFiEvent::CONNECTED:
 *                 ESP_LOGI("APP", "Connected to WiFi!");
 *                 break;
 *             case WiFiEvent::GOT_IP: {
 *                 auto* info = static_cast<WiFiEventInfo*>(data);
 *                 ESP_LOGI("APP", "IP: %s", info->ip_str);
 *                 break;
 *             }
 *             case WiFiEvent::DISCONNECTED:
 *                 ESP_LOGW("APP", "WiFi disconnected, auto-reconnecting...");
 *                 break;
 *         }
 *     }
 * 
 *     extern "C" void app_main(void) {
 *         WiFiManager& wifi = WiFiManager::instance();
 *         wifi.setEventCallback(onWiFiEvent);
 * 
 *         // Connect to known network
 *         wifi.beginSTA("MyNetwork", "MyPassword");
 * 
 *         // Or start AP for setup
 *         // wifi.beginAP("SmartHome-Setup", "12345678");
 * 
 *         // Or do both
 *         // wifi.beginSTAAP("MyNetwork", "MyPassword", "SmartHome-Config");
 *     }
 * 
 * =============================================================================
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

/* ─── Includes ───────────────────────────────────────────────────────────── */
#include <cstdint>
#include <cstring>
#include <functional>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_mac.h"

#include "lwip/inet.h"

/* ─── Constants ──────────────────────────────────────────────────────────── */

#define WIFI_MGR_MAX_SSID_LEN       32
#define WIFI_MGR_MAX_PASS_LEN       64
#define WIFI_MGR_NVS_NAMESPACE      "wifi_mgr"
#define WIFI_MGR_DEFAULT_AP_CHANNEL  1
#define WIFI_MGR_DEFAULT_MAX_STA     4       ///< Max clients on AP
#define WIFI_MGR_DEFAULT_MAX_RETRY   10      ///< Reconnect attempts before giving up
#define WIFI_MGR_RECONNECT_BASE_MS  1000     ///< Initial reconnect delay
#define WIFI_MGR_RECONNECT_MAX_MS   30000    ///< Maximum reconnect delay

/* ─── Event Group Bits ───────────────────────────────────────────────────── */
#define WIFI_CONNECTED_BIT   BIT0
#define WIFI_FAIL_BIT        BIT1
#define WIFI_GOT_IP_BIT      BIT2

/* ─── Event Types ────────────────────────────────────────────────────────── */

/**
 * @brief WiFi events reported to the user callback.
 */
enum class WiFiEvent {
    STA_STARTED,        ///< Station mode started
    STA_STOPPED,        ///< Station mode stopped
    CONNECTED,          ///< Connected to AP (no IP yet)
    DISCONNECTED,       ///< Disconnected from AP
    GOT_IP,             ///< Got IP address (fully operational)
    LOST_IP,            ///< Lost IP address
    AP_STARTED,         ///< Access point started
    AP_STOPPED,         ///< Access point stopped
    AP_CLIENT_CONNECTED,    ///< A client connected to our AP
    AP_CLIENT_DISCONNECTED, ///< A client disconnected from our AP
    CHANNEL_CHANGED,    ///< WiFi channel changed (relevant for ESP-NOW)
    RECONNECTING,       ///< Auto-reconnect attempt in progress
    RECONNECT_FAILED,   ///< All reconnect attempts exhausted
};

/**
 * @brief Extra info passed with some WiFi events.
 */
struct WiFiEventInfo {
    char        ip_str[16];     ///< IP address string (for GOT_IP event)
    uint8_t     channel;        ///< WiFi channel (for CHANNEL_CHANGED event)
    uint32_t    retry_num;      ///< Current retry number (for RECONNECTING)
    uint8_t     client_mac[6];  ///< Client MAC (for AP_CLIENT_* events)
};

/* ─── Callback Types ─────────────────────────────────────────────────────── */

/**
 * @brief User event callback.
 * @param event  The event type
 * @param info   Optional info (may be nullptr for some events)
 */
using WiFiEventCb = std::function<void(WiFiEvent event, const WiFiEventInfo* info)>;

/**
 * @brief Channel change callback (specifically for ESP-NOW coordination).
 * @param channel  The new WiFi channel (1-14)
 */
using WiFiChannelChangeCb = std::function<void(uint8_t channel)>;

/* ─── Configuration ──────────────────────────────────────────────────────── */

/**
 * @brief STA (station) mode configuration.
 */
struct WiFiSTAConfig {
    char        ssid[WIFI_MGR_MAX_SSID_LEN + 1]  = {};
    char        password[WIFI_MGR_MAX_PASS_LEN + 1] = {};
    bool        auto_reconnect  = true;         ///< Auto-reconnect on disconnect
    uint32_t    max_retries     = WIFI_MGR_DEFAULT_MAX_RETRY;
    bool        save_to_nvs     = true;         ///< Persist credentials to NVS
    wifi_auth_mode_t auth_mode  = WIFI_AUTH_WPA2_PSK;  ///< Minimum auth mode
};

/**
 * @brief AP (access point) mode configuration.
 */
struct WiFiAPConfig {
    char        ssid[WIFI_MGR_MAX_SSID_LEN + 1]  = "ESP32-SmartHome";
    char        password[WIFI_MGR_MAX_PASS_LEN + 1] = {};  ///< Empty = open network
    uint8_t     channel         = WIFI_MGR_DEFAULT_AP_CHANNEL;
    uint8_t     max_connections = WIFI_MGR_DEFAULT_MAX_STA;
    bool        hidden          = false;        ///< Hide SSID from scans
    bool        captive_portal  = false;        ///< Enable DNS redirect for captive portal
};

/* ─── Main Class ─────────────────────────────────────────────────────────── */

/**
 * @brief WiFi Manager - singleton for comprehensive WiFi management.
 * 
 * Singleton because ESP-IDF WiFi is global state (one radio, one config).
 * Thread-safe: all public methods protected by mutex.
 * 
 * Typical lifecycle:
 *   1. Get instance()
 *   2. Set callbacks
 *   3. Call beginSTA(), beginAP(), or beginSTAAP()
 *   4. React to events in your callback
 *   5. Optionally call end() to shut down
 */
class WiFiManager {
public:
    /* ─── Singleton ────────────────────────────────────────────────────── */
    static WiFiManager& instance();
    WiFiManager(const WiFiManager&) = delete;
    WiFiManager& operator=(const WiFiManager&) = delete;

    /* ─── Lifecycle ────────────────────────────────────────────────────── */

    /**
     * @brief Start WiFi in Station mode (connect to a router).
     * 
     * @param ssid      Network name to connect to
     * @param password  Network password (empty string for open networks)
     * @param config    Optional advanced STA config
     * @return ESP_OK on success (connection attempt started, not yet connected).
     *         Listen for GOT_IP event to know when fully connected.
     */
    esp_err_t beginSTA(const char* ssid, const char* password,
                       const WiFiSTAConfig* config = nullptr);

    /**
     * @brief Start WiFi in Access Point mode (create a hotspot).
     * 
     * @param ssid      Network name to broadcast
     * @param password  Network password (empty or nullptr = open network, 
     *                  min 8 chars if set)
     * @param config    Optional advanced AP config
     * @return ESP_OK on success
     */
    esp_err_t beginAP(const char* ssid, const char* password = nullptr,
                      const WiFiAPConfig* config = nullptr);

    /**
     * @brief Start WiFi in STA+AP mode (connect to router AND be a hotspot).
     * 
     * @param sta_ssid      Router SSID to connect to
     * @param sta_password  Router password
     * @param ap_ssid       Hotspot name to broadcast
     * @param ap_password   Hotspot password (nullptr = open)
     * @return ESP_OK on success
     * 
     * @note The AP channel is forced to match the STA channel once connected.
     */
    esp_err_t beginSTAAP(const char* sta_ssid, const char* sta_password,
                         const char* ap_ssid, const char* ap_password = nullptr);

    /**
     * @brief Start in AP mode using credentials saved in NVS.
     * 
     * Loads SSID/password from NVS and connects. If no saved credentials,
     * falls back to AP mode for configuration (if fallback_ap is set).
     * 
     * @param fallback_ap  If true and no NVS creds found, start AP for setup
     * @param ap_ssid      AP SSID for fallback (uses default if nullptr)
     * @return ESP_OK if connection attempt started, ESP_ERR_NOT_FOUND if no creds
     */
    esp_err_t beginFromNVS(bool fallback_ap = true, const char* ap_ssid = nullptr);

    /**
     * @brief Stop WiFi completely.
     * @return ESP_OK on success
     */
    esp_err_t end();

    /**
     * @brief Disconnect from AP (STA mode only). Does not stop WiFi.
     * @return ESP_OK on success
     */
    esp_err_t disconnect();

    /**
     * @brief Reconnect to the last configured AP.
     * @return ESP_OK if reconnect attempt started
     */
    esp_err_t reconnect();

    /* ─── Status ───────────────────────────────────────────────────────── */

    /** @brief Check if connected to an AP (STA mode) */
    bool isConnected() const;

    /** @brief Check if WiFi is initialized and running */
    bool isReady() const;

    /** @brief Get the current WiFi channel (1-14, 0 if unknown) */
    uint8_t getChannel() const;

    /** @brief Get the current IP address as a string. Empty if not connected. */
    void getIP(char* buf, size_t buf_len) const;

    /** @brief Get the STA MAC address */
    esp_err_t getMAC(uint8_t* mac) const;

    /** @brief Get RSSI of current AP connection (dBm). 0 if not connected. */
    int8_t getRSSI() const;

    /** @brief Get the SSID we're connected to (or trying to connect to) */
    const char* getSSID() const;

    /* ─── NVS Credential Management ────────────────────────────────────── */

    /**
     * @brief Save WiFi credentials to NVS.
     * @param ssid      SSID to save
     * @param password  Password to save
     * @return ESP_OK on success
     */
    esp_err_t saveCredentials(const char* ssid, const char* password);

    /**
     * @brief Load WiFi credentials from NVS.
     * @param ssid      Output buffer for SSID (min 33 bytes)
     * @param password  Output buffer for password (min 65 bytes)
     * @return ESP_OK if credentials found, ESP_ERR_NOT_FOUND if not
     */
    esp_err_t loadCredentials(char* ssid, char* password);

    /**
     * @brief Erase saved WiFi credentials from NVS.
     * @return ESP_OK on success
     */
    esp_err_t clearCredentials();

    /* ─── WiFi Scanning ────────────────────────────────────────────────── */

    /**
     * @brief Scan for available WiFi networks.
     * 
     * Blocking call. Results are returned in the provided array.
     * 
     * @param results   Output array for scan results
     * @param max_count Maximum number of results to store
     * @param found     Output: actual number of networks found
     * @return ESP_OK on success
     * 
     * @note WiFi must be started (STA or STA+AP mode) before scanning.
     */
    esp_err_t scan(wifi_ap_record_t* results, uint16_t max_count, uint16_t& found);

    /* ─── Callbacks ────────────────────────────────────────────────────── */

    /** @brief Set the main event callback */
    void setEventCallback(WiFiEventCb cb);

    /** @brief Set channel change callback (for ESP-NOW coordination) */
    void setChannelChangeCallback(WiFiChannelChangeCb cb);

private:
    WiFiManager();
    ~WiFiManager();

    /* ─── Internal WiFi Init ───────────────────────────────────────────── */
    esp_err_t initWiFiCommon();
    esp_err_t configureSTA(const WiFiSTAConfig& config);
    esp_err_t configureAP(const WiFiAPConfig& config);

    /* ─── Event Handlers ───────────────────────────────────────────────── */
    static void wifiEventHandler(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data);
    static void ipEventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);

    /* ─── Reconnect Logic ──────────────────────────────────────────────── */
    static void reconnectTask(void* arg);
    void scheduleReconnect();

    /* ─── Emit Events ──────────────────────────────────────────────────── */
    void emitEvent(WiFiEvent event, const WiFiEventInfo* info = nullptr);

    /* ─── State ────────────────────────────────────────────────────────── */
    bool                _initialized;
    bool                _connected;
    bool                _auto_reconnect;
    uint32_t            _max_retries;
    uint32_t            _retry_count;
    uint8_t             _current_channel;
    char                _current_ssid[WIFI_MGR_MAX_SSID_LEN + 1];
    char                _current_ip[16];

    esp_netif_t*        _sta_netif;
    esp_netif_t*        _ap_netif;

    EventGroupHandle_t  _event_group;
    SemaphoreHandle_t   _mutex;
    TaskHandle_t        _reconnect_task;

    WiFiEventCb         _event_cb;
    WiFiChannelChangeCb _channel_cb;

    esp_event_handler_instance_t _wifi_handler_inst;
    esp_event_handler_instance_t _ip_handler_inst;
};

#endif // WIFI_MANAGER_H
