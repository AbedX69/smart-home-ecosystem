/*
 * =============================================================================
 * FILE:        wifi_manager.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-13
 * MODIFIED:    2026-02-13
 * VERSION:     1.0.0
 * =============================================================================
 * 
 * WiFi Manager implementation.
 * 
 * Key design decisions:
 *   - Singleton: ESP-IDF WiFi is inherently global (one radio)
 *   - Event-driven: Uses ESP event loop, not polling
 *   - Reconnect backoff: 1s → 2s → 4s → ... → 30s max
 *   - NVS storage: Credentials survive reboots
 *   - Channel tracking: For ESP-NOW coexistence
 * 
 * =============================================================================
 */

#include "wifi_manager.h"
#include "nvs.h"

static const char* TAG = "WiFiManager";

/* =============================================================================
 * SINGLETON
 * ========================================================================== */

WiFiManager& WiFiManager::instance() {
    static WiFiManager inst;
    return inst;
}

WiFiManager::WiFiManager()
    : _initialized(false)
    , _connected(false)
    , _auto_reconnect(true)
    , _max_retries(WIFI_MGR_DEFAULT_MAX_RETRY)
    , _retry_count(0)
    , _current_channel(0)
    , _sta_netif(nullptr)
    , _ap_netif(nullptr)
    , _event_group(nullptr)
    , _reconnect_task(nullptr)
    , _event_cb(nullptr)
    , _channel_cb(nullptr)
    , _wifi_handler_inst(nullptr)
    , _ip_handler_inst(nullptr)
{
    memset(_current_ssid, 0, sizeof(_current_ssid));
    memset(_current_ip, 0, sizeof(_current_ip));
    _mutex = xSemaphoreCreateMutex();
    _event_group = xEventGroupCreate();
}

WiFiManager::~WiFiManager() {
    end();
    if (_mutex) vSemaphoreDelete(_mutex);
    if (_event_group) vEventGroupDelete(_event_group);
}

/* =============================================================================
 * COMMON WiFi INITIALIZATION
 * =============================================================================
 * 
 * Both STA and AP modes need the same bootstrap sequence:
 *   1. NVS init (WiFi stores PHY calibration data here)
 *   2. Network interface init (TCP/IP stack)
 *   3. Event loop creation
 *   4. WiFi driver init with default config
 * 
 * This is called once, regardless of mode. Safe to call multiple times.
 * ========================================================================== */

esp_err_t WiFiManager::initWiFiCommon() {
    if (_initialized) return ESP_OK;

    esp_err_t ret;

    /* ── NVS ─────────────────────────────────────────────────────────── */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition issue, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── Network interface ───────────────────────────────────────────── */
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Netif init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── Event loop ──────────────────────────────────────────────────── */
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event loop failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── WiFi driver ─────────────────────────────────────────────────── */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Store config in RAM, not NVS. We manage NVS ourselves for 
     * credentials, and ESP-IDF's NVS WiFi storage is redundant. */
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    /* ── Register event handlers ─────────────────────────────────────── */
    ret = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        &wifiEventHandler, this, &_wifi_handler_inst);
    if (ret != ESP_OK) return ret;

    ret = esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID,
        &ipEventHandler, this, &_ip_handler_inst);
    if (ret != ESP_OK) return ret;

    _initialized = true;
    ESP_LOGI(TAG, "WiFi subsystem initialized");
    return ESP_OK;
}

/* =============================================================================
 * STA MODE
 * ========================================================================== */

esp_err_t WiFiManager::configureSTA(const WiFiSTAConfig& config) {
    /* Create STA netif if not already created.
     * esp_netif_create_default_wifi_sta() creates the network interface 
     * that handles DHCP, DNS, etc. for the station. */
    if (_sta_netif == nullptr) {
        _sta_netif = esp_netif_create_default_wifi_sta();
        if (_sta_netif == nullptr) {
            ESP_LOGE(TAG, "Failed to create STA netif");
            return ESP_FAIL;
        }
    }

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, config.ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, config.password, sizeof(wifi_config.sta.password) - 1);

    /* Set minimum auth mode. This prevents connecting to networks with 
     * weaker security than you specify. WPA2 is a safe default.
     * For open networks, set this to WIFI_AUTH_OPEN. */
    wifi_config.sta.threshold.authmode = config.auth_mode;

    /* PMF (Protected Management Frames) - security feature.
     * "capable" means we'll use it if the AP supports it.
     * "required" = false means we won't refuse to connect if AP doesn't support it. */
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "STA config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    strncpy(_current_ssid, config.ssid, WIFI_MGR_MAX_SSID_LEN);
    _auto_reconnect = config.auto_reconnect;
    _max_retries = config.max_retries;
    _retry_count = 0;

    return ESP_OK;
}

esp_err_t WiFiManager::beginSTA(const char* ssid, const char* password,
                                 const WiFiSTAConfig* config) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    esp_err_t ret = initWiFiCommon();
    if (ret != ESP_OK) {
        xSemaphoreGive(_mutex);
        return ret;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        xSemaphoreGive(_mutex);
        return ret;
    }

    WiFiSTAConfig sta_cfg;
    if (config) {
        sta_cfg = *config;
    }
    strncpy(sta_cfg.ssid, ssid, WIFI_MGR_MAX_SSID_LEN);
    if (password) strncpy(sta_cfg.password, password, WIFI_MGR_MAX_PASS_LEN);

    /* Handle open networks: if no password, set auth to OPEN */
    if (strlen(sta_cfg.password) == 0) {
        sta_cfg.auth_mode = WIFI_AUTH_OPEN;
    }

    ret = configureSTA(sta_cfg);
    if (ret != ESP_OK) {
        xSemaphoreGive(_mutex);
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(_mutex);
        return ret;
    }

    /* Save to NVS if requested */
    if (sta_cfg.save_to_nvs) {
        saveCredentials(ssid, password ? password : "");
    }

    /* Initiate connection. The actual connection happens asynchronously.
     * We'll get WIFI_EVENT_STA_CONNECTED followed by IP_EVENT_STA_GOT_IP
     * when it's done. */
    ret = esp_wifi_connect();

    /* Print our MAC for easy identification */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  WiFi STA mode started");
    ESP_LOGI(TAG, "  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "  Connecting to: %s", ssid);
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    xSemaphoreGive(_mutex);
    return ret;
}

/* =============================================================================
 * AP MODE
 * ========================================================================== */

esp_err_t WiFiManager::configureAP(const WiFiAPConfig& config) {
    if (_ap_netif == nullptr) {
        _ap_netif = esp_netif_create_default_wifi_ap();
        if (_ap_netif == nullptr) {
            ESP_LOGE(TAG, "Failed to create AP netif");
            return ESP_FAIL;
        }
    }

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.ap.ssid, config.ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(config.ssid);
    wifi_config.ap.channel = config.channel;
    wifi_config.ap.max_connection = config.max_connections;
    wifi_config.ap.ssid_hidden = config.hidden ? 1 : 0;

        if (strlen(config.password) >= 8) {
            strncpy((char*)wifi_config.ap.password, config.password,
                sizeof(wifi_config.ap.password) - 1);
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        /* Open network. Espressif requires authmode = OPEN when no password. */
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        ESP_LOGW(TAG, "AP has no password - network is OPEN");
    }

    return esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
}

esp_err_t WiFiManager::beginAP(const char* ssid, const char* password,
                                const WiFiAPConfig* config) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    esp_err_t ret = initWiFiCommon();
    if (ret != ESP_OK) { xSemaphoreGive(_mutex); return ret; }

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) { xSemaphoreGive(_mutex); return ret; }

    WiFiAPConfig ap_cfg;
    if (config) ap_cfg = *config;
    if (ssid) strncpy(ap_cfg.ssid, ssid, WIFI_MGR_MAX_SSID_LEN);
    if (password) strncpy(ap_cfg.password, password, WIFI_MGR_MAX_PASS_LEN);

    ret = configureAP(ap_cfg);
    if (ret != ESP_OK) { xSemaphoreGive(_mutex); return ret; }

    ret = esp_wifi_start();

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  WiFi AP mode started");
    ESP_LOGI(TAG, "  SSID: %s", ap_cfg.ssid);
    ESP_LOGI(TAG, "  Password: %s", strlen(ap_cfg.password) > 0 ? "****" : "(open)");
    ESP_LOGI(TAG, "  Channel: %d", ap_cfg.channel);
    ESP_LOGI(TAG, "  IP: 192.168.4.1");
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    xSemaphoreGive(_mutex);
    return ret;
}

/* =============================================================================
 * STA+AP MODE
 * ========================================================================== */

esp_err_t WiFiManager::beginSTAAP(const char* sta_ssid, const char* sta_password,
                                   const char* ap_ssid, const char* ap_password) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    esp_err_t ret = initWiFiCommon();
    if (ret != ESP_OK) { xSemaphoreGive(_mutex); return ret; }

    ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret != ESP_OK) { xSemaphoreGive(_mutex); return ret; }

    /* Configure STA */
    WiFiSTAConfig sta_cfg;
    strncpy(sta_cfg.ssid, sta_ssid, WIFI_MGR_MAX_SSID_LEN);
    if (sta_password) strncpy(sta_cfg.password, sta_password, WIFI_MGR_MAX_PASS_LEN);
    if (strlen(sta_cfg.password) == 0) sta_cfg.auth_mode = WIFI_AUTH_OPEN;

    ret = configureSTA(sta_cfg);
    if (ret != ESP_OK) { xSemaphoreGive(_mutex); return ret; }

    /* Configure AP */
    WiFiAPConfig ap_cfg;
    if (ap_ssid) strncpy(ap_cfg.ssid, ap_ssid, WIFI_MGR_MAX_SSID_LEN);
    if (ap_password) strncpy(ap_cfg.password, ap_password, WIFI_MGR_MAX_PASS_LEN);

    ret = configureAP(ap_cfg);
    if (ret != ESP_OK) { xSemaphoreGive(_mutex); return ret; }

    ret = esp_wifi_start();
    if (ret != ESP_OK) { xSemaphoreGive(_mutex); return ret; }

    ret = esp_wifi_connect();

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  WiFi STA+AP mode started");
    ESP_LOGI(TAG, "  STA → connecting to: %s", sta_ssid);
    ESP_LOGI(TAG, "  AP  → broadcasting: %s", ap_cfg.ssid);
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    if (sta_cfg.save_to_nvs) {
        saveCredentials(sta_ssid, sta_password ? sta_password : "");
    }

    xSemaphoreGive(_mutex);
    return ret;
}

/* =============================================================================
 * NVS-BASED START
 * ========================================================================== */

esp_err_t WiFiManager::beginFromNVS(bool fallback_ap, const char* ap_ssid) {
        esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition issue, erasing...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
        if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    char ssid[WIFI_MGR_MAX_SSID_LEN + 1] = {};
    char pass[WIFI_MGR_MAX_PASS_LEN + 1] = {};

     ret = loadCredentials(ssid, pass);
    if (ret == ESP_OK && strlen(ssid) > 0) {
        ESP_LOGI(TAG, "Loaded credentials from NVS: SSID=%s", ssid);
        return beginSTA(ssid, pass);
    }

    ESP_LOGW(TAG, "No saved WiFi credentials in NVS");

    if (fallback_ap) {
        const char* fallback_ssid = ap_ssid ? ap_ssid : "ESP32-Setup";
        ESP_LOGI(TAG, "Falling back to AP mode: %s", fallback_ssid);
        return beginAP(fallback_ssid);
    }

    return ESP_ERR_NOT_FOUND;
}

/* =============================================================================
 * STOP / DISCONNECT / RECONNECT
 * ========================================================================== */

esp_err_t WiFiManager::end() {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    if (!_initialized) {
        xSemaphoreGive(_mutex);
        return ESP_OK;
    }

    if (_reconnect_task) {
        vTaskDelete(_reconnect_task);
        _reconnect_task = nullptr;
    }

    esp_wifi_disconnect();
    esp_wifi_stop();

    if (_wifi_handler_inst) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, _wifi_handler_inst);
        _wifi_handler_inst = nullptr;
    }
    if (_ip_handler_inst) {
        esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, _ip_handler_inst);
        _ip_handler_inst = nullptr;
    }

    esp_wifi_deinit();

    if (_sta_netif) {
        esp_netif_destroy(_sta_netif);
        _sta_netif = nullptr;
    }
    if (_ap_netif) {
        esp_netif_destroy(_ap_netif);
        _ap_netif = nullptr;
    }

    _initialized = false;
    _connected = false;
    memset(_current_ip, 0, sizeof(_current_ip));

    ESP_LOGI(TAG, "WiFi stopped");
    xSemaphoreGive(_mutex);
    return ESP_OK;
}

esp_err_t WiFiManager::disconnect() {
    _auto_reconnect = false;  // Don't auto-reconnect after manual disconnect
    return esp_wifi_disconnect();
}

esp_err_t WiFiManager::reconnect() {
    _retry_count = 0;
    _auto_reconnect = true;
    return esp_wifi_connect();
}

/* =============================================================================
 * STATUS QUERIES
 * ========================================================================== */

bool WiFiManager::isConnected() const { return _connected; }
bool WiFiManager::isReady() const { return _initialized; }

uint8_t WiFiManager::getChannel() const {
    uint8_t primary;
    wifi_second_chan_t secondary;
    if (esp_wifi_get_channel(&primary, &secondary) == ESP_OK) {
        return primary;
    }
    return 0;
}

void WiFiManager::getIP(char* buf, size_t buf_len) const {
    if (buf && buf_len > 0) {
        strncpy(buf, _current_ip, buf_len - 1);
        buf[buf_len - 1] = '\0';
    }
}

esp_err_t WiFiManager::getMAC(uint8_t* mac) const {
    if (!mac) return ESP_ERR_INVALID_ARG;
    return esp_read_mac(mac, ESP_MAC_WIFI_STA);
}

int8_t WiFiManager::getRSSI() const {
    if (!_connected) return 0;
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

const char* WiFiManager::getSSID() const { return _current_ssid; }

/* =============================================================================
 * NVS CREDENTIAL MANAGEMENT
 * =============================================================================
 * 
 * We use a dedicated NVS namespace "wifi_mgr" to avoid collisions with other 
 * components. NVS is a key-value store in flash memory that survives reboots.
 * ========================================================================== */

esp_err_t WiFiManager::saveCredentials(const char* ssid, const char* password) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(WIFI_MGR_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_str(handle, "ssid", ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "password", password ? password : "");
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Credentials saved to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to save credentials: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t WiFiManager::loadCredentials(char* ssid, char* password) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(WIFI_MGR_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(ret));  // ADD THIS
        return ret;
    }

    size_t ssid_len = WIFI_MGR_MAX_SSID_LEN + 1;
    size_t pass_len = WIFI_MGR_MAX_PASS_LEN + 1;

    ret = nvs_get_str(handle, "ssid", ssid, &ssid_len);
    if (ret == ESP_OK) {
        ret = nvs_get_str(handle, "password", password, &pass_len);
    }

    nvs_close(handle);
    return ret;
}

esp_err_t WiFiManager::clearCredentials() {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(WIFI_MGR_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_erase_key(handle, "ssid");
    nvs_erase_key(handle, "password");
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Credentials cleared from NVS");
    return ESP_OK;
}

/* =============================================================================
 * WiFi SCANNING
 * ========================================================================== */

esp_err_t WiFiManager::scan(wifi_ap_record_t* results, uint16_t max_count, uint16_t& found) {
    if (!_initialized) return ESP_ERR_INVALID_STATE;

    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = true;

    /* Blocking scan. This takes ~2-4 seconds depending on channel count. */
    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, true);
    if (ret != ESP_OK) return ret;

    found = max_count;
    ret = esp_wifi_scan_get_ap_records(&found, results);

    ESP_LOGI(TAG, "Scan found %d networks", found);
    return ret;
}

/* =============================================================================
 * CALLBACKS
 * ========================================================================== */

void WiFiManager::setEventCallback(WiFiEventCb cb) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _event_cb = cb;
    xSemaphoreGive(_mutex);
}

void WiFiManager::setChannelChangeCallback(WiFiChannelChangeCb cb) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _channel_cb = cb;
    xSemaphoreGive(_mutex);
}

void WiFiManager::emitEvent(WiFiEvent event, const WiFiEventInfo* info) {
    if (_event_cb) {
        _event_cb(event, info);
    }
}

/* =============================================================================
 * EVENT HANDLERS
 * =============================================================================
 * 
 * ESP-IDF uses a centralized event loop. WiFi events and IP events come 
 * through separate event bases. We register handlers for both.
 * 
 * The handler runs in the system event task context. It's safe to log and 
 * do moderate work, but don't block for long.
 * ========================================================================== */

void WiFiManager::wifiEventHandler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data) {
    WiFiManager* mgr = static_cast<WiFiManager*>(arg);

    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started");
            mgr->emitEvent(WiFiEvent::STA_STARTED);
            break;

        case WIFI_EVENT_STA_STOP:
            ESP_LOGI(TAG, "STA stopped");
            mgr->emitEvent(WiFiEvent::STA_STOPPED);
            break;

        case WIFI_EVENT_STA_CONNECTED: {
            ESP_LOGI(TAG, "Connected to AP");
            mgr->_retry_count = 0;

            /* Track channel for ESP-NOW coordination */
            uint8_t ch = mgr->getChannel();
            if (ch != mgr->_current_channel) {
                mgr->_current_channel = ch;
                WiFiEventInfo info = {};
                info.channel = ch;
                mgr->emitEvent(WiFiEvent::CHANNEL_CHANGED, &info);
                if (mgr->_channel_cb) {
                    mgr->_channel_cb(ch);
                }
                ESP_LOGI(TAG, "Channel: %d", ch);
            }

            mgr->emitEvent(WiFiEvent::CONNECTED);
            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED: {
            ESP_LOGW(TAG, "Disconnected from AP");
            mgr->_connected = false;
            memset(mgr->_current_ip, 0, sizeof(mgr->_current_ip));
            xEventGroupClearBits(mgr->_event_group, WIFI_CONNECTED_BIT | WIFI_GOT_IP_BIT);

            mgr->emitEvent(WiFiEvent::DISCONNECTED);

            /* Auto-reconnect with exponential backoff */
            if (mgr->_auto_reconnect && mgr->_retry_count < mgr->_max_retries) {
                mgr->scheduleReconnect();
            } else if (mgr->_auto_reconnect) {
                ESP_LOGE(TAG, "Max reconnect retries (%lu) reached", (unsigned long)mgr->_max_retries);
                xEventGroupSetBits(mgr->_event_group, WIFI_FAIL_BIT);
                mgr->emitEvent(WiFiEvent::RECONNECT_FAILED);
            }
            break;
        }

        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP started");
            mgr->emitEvent(WiFiEvent::AP_STARTED);
            break;

        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "AP stopped");
            mgr->emitEvent(WiFiEvent::AP_STOPPED);
            break;

        case WIFI_EVENT_AP_STACONNECTED: {
            auto* ev = static_cast<wifi_event_ap_staconnected_t*>(event_data);
            ESP_LOGI(TAG, "Client connected to AP: %02X:%02X:%02X:%02X:%02X:%02X",
                     ev->mac[0], ev->mac[1], ev->mac[2],
                     ev->mac[3], ev->mac[4], ev->mac[5]);
            WiFiEventInfo info = {};
            memcpy(info.client_mac, ev->mac, 6);
            mgr->emitEvent(WiFiEvent::AP_CLIENT_CONNECTED, &info);
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            auto* ev = static_cast<wifi_event_ap_stadisconnected_t*>(event_data);
            ESP_LOGI(TAG, "Client disconnected from AP");
            WiFiEventInfo info = {};
            memcpy(info.client_mac, ev->mac, 6);
            mgr->emitEvent(WiFiEvent::AP_CLIENT_DISCONNECTED, &info);
            break;
        }

        default:
            break;
    }
}

void WiFiManager::ipEventHandler(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data) {
    WiFiManager* mgr = static_cast<WiFiManager*>(arg);

    switch (event_id) {
        case IP_EVENT_STA_GOT_IP: {
            auto* ev = static_cast<ip_event_got_ip_t*>(event_data);

            /* Convert binary IP to string.
             * esp_ip4addr_ntoa is the ESP-IDF helper for this. */
            esp_ip4addr_ntoa(&ev->ip_info.ip, mgr->_current_ip, sizeof(mgr->_current_ip));

            mgr->_connected = true;
            xEventGroupSetBits(mgr->_event_group, WIFI_CONNECTED_BIT | WIFI_GOT_IP_BIT);

            ESP_LOGI(TAG, "═══════════════════════════════════════════");
            ESP_LOGI(TAG, "  Got IP: %s", mgr->_current_ip);
            ESP_LOGI(TAG, "═══════════════════════════════════════════");

            WiFiEventInfo info = {};
            strncpy(info.ip_str, mgr->_current_ip, sizeof(info.ip_str) - 1);
            mgr->emitEvent(WiFiEvent::GOT_IP, &info);
            break;
        }

        case IP_EVENT_STA_LOST_IP:
            ESP_LOGW(TAG, "Lost IP address");
            mgr->_connected = false;
            memset(mgr->_current_ip, 0, sizeof(mgr->_current_ip));
            xEventGroupClearBits(mgr->_event_group, WIFI_GOT_IP_BIT);
            mgr->emitEvent(WiFiEvent::LOST_IP);
            break;

        default:
            break;
    }
}

/* =============================================================================
 * RECONNECT LOGIC
 * =============================================================================
 * 
 * Exponential backoff: each retry waits longer to avoid hammering the AP.
 *   Retry 0: 1000ms
 *   Retry 1: 2000ms
 *   Retry 2: 4000ms
 *   ...
 *   Max: 30000ms
 * 
 * We use a separate FreeRTOS task for the delay so we don't block the 
 * event handler. The task deletes itself after initiating the reconnect.
 * ========================================================================== */

void WiFiManager::scheduleReconnect() {
    _retry_count++;

    /* Calculate backoff delay: base * 2^(retry-1), capped at max */
    uint32_t delay_ms = WIFI_MGR_RECONNECT_BASE_MS;
    for (uint32_t i = 1; i < _retry_count && delay_ms < WIFI_MGR_RECONNECT_MAX_MS; i++) {
        delay_ms *= 2;
    }
    if (delay_ms > WIFI_MGR_RECONNECT_MAX_MS) {
        delay_ms = WIFI_MGR_RECONNECT_MAX_MS;
    }

    ESP_LOGI(TAG, "Reconnect attempt %lu/%lu in %lu ms",
             (unsigned long)_retry_count, (unsigned long)_max_retries,
             (unsigned long)delay_ms);

    WiFiEventInfo info = {};
    info.retry_num = _retry_count;
    emitEvent(WiFiEvent::RECONNECTING, &info);

    /* Create a one-shot task to handle the delayed reconnect.
     * We pass the delay in the task parameter (cast to void*). */
    if (_reconnect_task) {
        vTaskDelete(_reconnect_task);
        _reconnect_task = nullptr;
    }

    /* Store delay in a static so the task can read it.
     * This is safe because only one reconnect task runs at a time. */
    static uint32_t s_delay_ms;
    s_delay_ms = delay_ms;

    xTaskCreate(reconnectTask, "wifi_reconn", 2048, this, 3, &_reconnect_task);
}

void WiFiManager::reconnectTask(void* arg) {
    WiFiManager* mgr = static_cast<WiFiManager*>(arg);

    /* Read delay from the static variable set in scheduleReconnect */
    extern uint32_t s_delay_ms;  // Declared static in scheduleReconnect scope - actually let's use a simpler approach
    
    /* Simple approach: calculate from retry count */
    uint32_t delay = WIFI_MGR_RECONNECT_BASE_MS;
    for (uint32_t i = 1; i < mgr->_retry_count && delay < WIFI_MGR_RECONNECT_MAX_MS; i++) {
        delay *= 2;
    }
    if (delay > WIFI_MGR_RECONNECT_MAX_MS) delay = WIFI_MGR_RECONNECT_MAX_MS;

    vTaskDelay(pdMS_TO_TICKS(delay));

    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reconnect call failed: %s", esp_err_to_name(ret));
    }

    mgr->_reconnect_task = nullptr;
    vTaskDelete(nullptr);  // Self-delete
}
