/*
 * =============================================================================
 * FILE:        esp_now_manager.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-12
 * MODIFIED:    2026-02-12
 * VERSION:     1.0.0
 * =============================================================================
 * 
 * Implementation of the ESP-NOW Manager component.
 * 
 * Key design decisions explained inline:
 *   - Singleton pattern: ESP-NOW is global state in ESP-IDF, only one instance
 *   - Queue-based receive: ESP-NOW callback runs in WiFi task, unsafe for work
 *   - Mutex protection: multiple FreeRTOS tasks might call send() concurrently
 *   - Auto broadcast peer: broadcast is so common, we add it by default
 * 
 * =============================================================================
 */

#include "esp_now_manager.h"

/* ─── Logging Tag ────────────────────────────────────────────────────────── */
static const char* TAG = "EspNowManager";

/* =============================================================================
 * SINGLETON
 * =============================================================================
 * 
 * Why singleton? The ESP-IDF ESP-NOW API is inherently global. There's one
 * set of callbacks, one peer list, one initialization state. Having multiple
 * C++ objects would just cause confusion and conflicts.
 * 
 * The instance is created on first call to instance() and lives forever.
 * This is standard "Meyer's Singleton" - thread-safe in C++11 and later.
 * ========================================================================== */

EspNowManager& EspNowManager::instance() {
    static EspNowManager inst;
    return inst;
}

EspNowManager::EspNowManager()
    : _initialized(false)
    , _rx_queue(nullptr)
    , _rx_task(nullptr)
    , _mutex(nullptr)
    , _recv_cb(nullptr)
    , _send_cb(nullptr)
{
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        ESP_LOGE(TAG, "Failed to create mutex!");
    }
}

EspNowManager::~EspNowManager() {
    end();
    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

/* =============================================================================
 * LIFECYCLE
 * =============================================================================
 * 
 * Initialization order matters a LOT with ESP-IDF networking:
 *   1. NVS must be initialized first (WiFi stores calibration data there)
 *   2. Network interface (netif) must be created
 *   3. Event loop must exist for WiFi events
 *   4. WiFi must be initialized and started
 *   5. THEN you can init ESP-NOW
 * 
 * If you skip any step, you get cryptic errors. This begin() handles it all.
 * ========================================================================== */

esp_err_t EspNowManager::begin(const EspNowConfig& config) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    // Already initialized? That's fine, just return success.
    if (_initialized) {
        ESP_LOGW(TAG, "Already initialized, skipping begin()");
        xSemaphoreGive(_mutex);
        return ESP_OK;
    }

    esp_err_t ret;

    /* ── Step 1: NVS Flash ─────────────────────────────────────────────
     * NVS (Non-Volatile Storage) is where ESP-IDF stores WiFi calibration
     * data. If you don't init this, WiFi init will fail with a confusing
     * error about "NVS not initialized."
     * 
     * We use nvs_flash_init() first. If it fails because the partition is
     * corrupt or the layout changed, we erase it and try again. This is
     * the standard Espressif pattern.
     * ────────────────────────────────────────────────────────────────── */
    if (config.init_nvs) {
        ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS partition issue, erasing and re-initializing...");
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
            xSemaphoreGive(_mutex);
            return ret;
        }
        ESP_LOGI(TAG, "NVS initialized");
    }

    /* ── Step 2: Network Interface ─────────────────────────────────────
     * Even though we're not connecting to a router, ESP-IDF's WiFi stack
     * requires the network interface to be initialized. This is a one-time
     * call that sets up the internal TCP/IP stack.
     * ────────────────────────────────────────────────────────────────── */
    if (config.init_netif) {
        ret = esp_netif_init();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            // ESP_ERR_INVALID_STATE means already initialized, which is fine
            ESP_LOGE(TAG, "Netif init failed: %s", esp_err_to_name(ret));
            xSemaphoreGive(_mutex);
            return ret;
        }
    }

    /* ── Step 3: Event Loop ────────────────────────────────────────────
     * WiFi fires events through the ESP event loop system. We need the
     * default event loop to exist. Again, safe to call if already created.
     * ────────────────────────────────────────────────────────────────── */
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event loop creation failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(_mutex);
        return ret;
    }

    /* ── Step 4: WiFi Initialization ───────────────────────────────────
     * We init WiFi with default config and start it in the requested mode.
     * For ESP-NOW, STA mode is most common and recommended.
     * 
     * IMPORTANT: We call esp_wifi_start() but do NOT call esp_wifi_connect().
     * We just need the radio hardware powered on, not connected to any AP.
     * ────────────────────────────────────────────────────────────────── */
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(_mutex);
        return ret;
    }

    // Store WiFi config in RAM only (we don't need it persisted to NVS)
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    ret = esp_wifi_set_mode(config.wifi_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi set mode failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(_mutex);
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(_mutex);
        return ret;
    }

    /* Optional: Set WiFi channel if specified */
    if (config.channel > 0 && config.channel <= 14) {
        esp_wifi_set_channel(config.channel, WIFI_SECOND_CHAN_NONE);
        ESP_LOGI(TAG, "WiFi channel set to %d", config.channel);
    }

    /* Optional: Enable long range mode.
     * This uses a lower data rate (512 Kbps or 256 Kbps) for better range.
     * Only works with Espressif devices - not standard WiFi. */
    if (config.long_range) {
        esp_wifi_set_protocol(WIFI_IF_STA,
                              WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
                              WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
        ESP_LOGI(TAG, "Long range mode enabled");
    }

    ESP_LOGI(TAG, "WiFi started in %s mode",
             config.wifi_mode == WIFI_MODE_STA ? "STA" :
             config.wifi_mode == WIFI_MODE_AP  ? "AP"  : "STA+AP");

    /* ── Step 5: ESP-NOW Initialization ────────────────────────────────
     * Finally, the actual ESP-NOW init. This must come AFTER wifi_start().
     * ────────────────────────────────────────────────────────────────── */
    ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(_mutex);
        return ret;
    }

    /* ── Step 6: Register Internal Callbacks ───────────────────────────
     * These static functions forward to the singleton instance, which then
     * either enqueues to the receive queue or calls the user's send callback.
     * ────────────────────────────────────────────────────────────────── */
    esp_now_register_send_cb(onSendStatic);
    esp_now_register_recv_cb(onRecvStatic);

    /* ── Step 7: Add Broadcast Peer ────────────────────────────────────
     * Broadcast is extremely common, so we add it automatically.
     * This way the user can call broadcast() immediately without setup.
     * ────────────────────────────────────────────────────────────────── */
    const uint8_t broadcast_mac[] = ESPNOW_BROADCAST_MAC;
    addPeer(broadcast_mac, 0, false, nullptr);

    /* ── Step 8: Create Receive Queue and Task ─────────────────────────
     * The queue bridges the ISR-like callback context to a normal FreeRTOS
     * task where we can safely call the user's receive callback.
     * ────────────────────────────────────────────────────────────────── */
    _rx_queue = xQueueCreate(config.queue_size, sizeof(RxMessage));
    if (_rx_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create receive queue!");
        esp_now_deinit();
        xSemaphoreGive(_mutex);
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_ret = xTaskCreate(
        receiveTaskFunc,        // Task function
        "espnow_rx",            // Name (for debugging)
        config.task_stack,      // Stack size
        this,                   // Parameter (pointer to this instance)
        config.task_priority,   // Priority
        &_rx_task               // Task handle output
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create receive task!");
        vQueueDelete(_rx_queue);
        _rx_queue = nullptr;
        esp_now_deinit();
        xSemaphoreGive(_mutex);
        return ESP_ERR_NO_MEM;
    }

    /* ── Print MAC address for easy identification ─────────────────── */
    uint8_t own_mac[6];
    esp_read_mac(own_mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    macToStr(own_mac, mac_str);
    ESP_LOGI(TAG, "══════════════════════════════════════════");
    ESP_LOGI(TAG, "  ESP-NOW initialized successfully");
    ESP_LOGI(TAG, "  This device MAC: %s", mac_str);
    ESP_LOGI(TAG, "══════════════════════════════════════════");

    _initialized = true;
    xSemaphoreGive(_mutex);
    return ESP_OK;
}

esp_err_t EspNowManager::end() {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    if (!_initialized) {
        xSemaphoreGive(_mutex);
        return ESP_OK;
    }

    // Stop the receive task
    if (_rx_task) {
        vTaskDelete(_rx_task);
        _rx_task = nullptr;
    }

    // Delete the queue
    if (_rx_queue) {
        vQueueDelete(_rx_queue);
        _rx_queue = nullptr;
    }

    // Deinit ESP-NOW (removes all peers, unregisters callbacks)
    esp_now_deinit();

    _initialized = false;
    ESP_LOGI(TAG, "ESP-NOW deinitialized");

    xSemaphoreGive(_mutex);
    return ESP_OK;
}

bool EspNowManager::isReady() const {
    return _initialized;
}

/* =============================================================================
 * CALLBACKS
 * ========================================================================== */

void EspNowManager::setReceiveCallback(EspNowReceiveCb cb) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _recv_cb = cb;
    xSemaphoreGive(_mutex);
}

void EspNowManager::setSendCallback(EspNowSendCb cb) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _send_cb = cb;
    xSemaphoreGive(_mutex);
}

/* =============================================================================
 * PEER MANAGEMENT
 * =============================================================================
 * 
 * ESP-NOW maintains an internal peer list. You MUST add a device as a peer 
 * before sending to it (unicast). The peer list has a hard limit of 20 entries.
 * 
 * Each peer entry stores:
 *   - MAC address (6 bytes)
 *   - Channel (0 = current, 1-14 = specific)
 *   - WiFi interface (STA or AP)
 *   - Encryption flag + LMK key
 * 
 * Gotcha: If you're in STA mode, set the peer's ifidx to WIFI_IF_STA.
 *         If AP mode, use WIFI_IF_AP. Mismatch = send fails silently.
 * ========================================================================== */

esp_err_t EspNowManager::addPeer(const uint8_t* mac, uint8_t channel,
                                  bool encrypt, const uint8_t* lmk) {
    if (mac == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check if already added (not an error, just skip)
    if (esp_now_is_peer_exist(mac)) {
        char mac_str[18];
        macToStr(mac, mac_str);
        ESP_LOGD(TAG, "Peer %s already exists, skipping", mac_str);
        return ESP_OK;
    }

    /* Zero-initialize the peer info struct. This is critical because 
     * ESP-IDF checks ALL fields, and uninitialized garbage will cause
     * weird failures. Always memset to 0 first. */
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, mac, 6);
    peer_info.channel = channel;
    peer_info.ifidx   = WIFI_IF_STA;   // Match the WiFi mode we started
    peer_info.encrypt = encrypt;

    if (encrypt && lmk != nullptr) {
        memcpy(peer_info.lmk, lmk, ESP_NOW_KEY_LEN);
    }

    esp_err_t ret = esp_now_add_peer(&peer_info);
    if (ret != ESP_OK) {
        char mac_str[18];
        macToStr(mac, mac_str);
        ESP_LOGE(TAG, "Failed to add peer %s: %s", mac_str, esp_err_to_name(ret));
    } else {
        char mac_str[18];
        macToStr(mac, mac_str);
        ESP_LOGI(TAG, "Peer added: %s (ch=%d, encrypt=%s)",
                 mac_str, channel, encrypt ? "yes" : "no");
    }

    return ret;
}

esp_err_t EspNowManager::removePeer(const uint8_t* mac) {
    if (mac == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = esp_now_del_peer(mac);
    if (ret == ESP_OK) {
        char mac_str[18];
        macToStr(mac, mac_str);
        ESP_LOGI(TAG, "Peer removed: %s", mac_str);
    }
    return ret;
}

bool EspNowManager::hasPeer(const uint8_t* mac) const {
    if (mac == nullptr) return false;
    return esp_now_is_peer_exist(mac);
}

esp_err_t EspNowManager::getPeerCount(int& total, int& encrypted) const {
    esp_now_peer_num_t num = {};
    esp_err_t ret = esp_now_get_peer_num(&num);
    if (ret == ESP_OK) {
        total     = num.total_num;
        encrypted = num.encrypt_num;
    }
    return ret;
}

/* =============================================================================
 * SENDING
 * =============================================================================
 * 
 * esp_now_send() is non-blocking. It puts the data in an internal queue and 
 * returns immediately. The actual radio transmission happens later, and the 
 * send callback tells you if it succeeded at the MAC layer.
 * 
 * IMPORTANT: "Success" in the send callback means the receiving device's MAC
 * layer acknowledged the frame. It does NOT mean the application processed it.
 * Think of it like a TCP ACK vs application-level acknowledgment.
 * 
 * For broadcast, there's no ACK at all - success just means "we transmitted it."
 * ========================================================================== */

esp_err_t EspNowManager::send(const uint8_t* dest_mac, const uint8_t* data, size_t len) {
    if (!_initialized) {
        ESP_LOGE(TAG, "Not initialized! Call begin() first.");
        return ESP_ERR_INVALID_STATE;
    }

    if (data == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > ESP_NOW_MAX_DATA_LEN) {
        ESP_LOGE(TAG, "Data too long: %d bytes (max %d)", (int)len, ESP_NOW_MAX_DATA_LEN);
        return ESP_ERR_INVALID_ARG;
    }

    if (dest_mac == nullptr) {
        ESP_LOGE(TAG, "dest_mac is NULL. Use sendToAll() or broadcast() instead.");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = esp_now_send(dest_mac, data, len);
    if (ret != ESP_OK) {
        char mac_str[18];
        macToStr(dest_mac, mac_str);
        ESP_LOGE(TAG, "Send to %s failed: %s", mac_str, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t EspNowManager::sendToAll(const uint8_t* data, size_t len) {
    if (!_initialized) {
        ESP_LOGE(TAG, "Not initialized! Call begin() first.");
        return ESP_ERR_INVALID_STATE;
    }

    if (data == nullptr || len == 0 || len > ESP_NOW_MAX_DATA_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Passing NULL as dest_mac tells ESP-NOW to send to ALL registered peers.
     * This is different from broadcast - it sends individual unicast frames 
     * to each peer in the list, each with its own delivery confirmation. */
    return esp_now_send(nullptr, data, len);
}

esp_err_t EspNowManager::broadcast(const uint8_t* data, size_t len) {
    const uint8_t bcast[] = ESPNOW_BROADCAST_MAC;
    return send(bcast, data, len);
}

/* =============================================================================
 * UTILITIES
 * ========================================================================== */

esp_err_t EspNowManager::getOwnMac(uint8_t* mac) const {
    if (mac == nullptr) return ESP_ERR_INVALID_ARG;
    return esp_read_mac(mac, ESP_MAC_WIFI_STA);
}

void EspNowManager::macToStr(const uint8_t* mac, char* buf) {
    if (mac == nullptr || buf == nullptr) return;
    snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* =============================================================================
 * INTERNAL CALLBACKS (STATIC)
 * =============================================================================
 * 
 * ESP-IDF expects plain C function pointers for callbacks. We use static 
 * member functions that forward to the singleton instance.
 * 
 * CRITICAL: The receive callback runs in the WiFi task context. You MUST NOT:
 *   - Call ESP_LOGx (it may take a mutex internally)
 *   - Allocate memory with new/malloc
 *   - Take mutexes or semaphores  
 *   - Do anything that might block
 * 
 * Instead, we copy the data and shove it into a FreeRTOS queue (which IS
 * safe to do from a task context, just not from a true ISR). The receive 
 * task then picks it up and safely calls the user's callback.
 * 
 * The SEND callback is less restricted - it runs in a normal task context,
 * so we can call the user callback directly, but we still keep it brief.
 * ========================================================================== */

void EspNowManager::onSendStatic(const esp_now_send_info_t* tx_info,
                                  esp_now_send_status_t status) {
    EspNowManager& mgr = instance();

    if (mgr._send_cb) {
        /* tx_info->dest_mac contains the destination. In ESP-IDF v5.x, 
         * tx_info is a struct with dest_mac, tx_status, and tx_rate fields.
         * We extract the success/fail from the legacy status parameter for 
         * backward compatibility, though tx_info->tx_status also works. */
        bool success = (status == ESP_NOW_SEND_SUCCESS);
        mgr._send_cb(tx_info->des_addr, success);
    }
}

void EspNowManager::onRecvStatic(const esp_now_recv_info_t* recv_info,
                                  const uint8_t* data, int data_len) {
    EspNowManager& mgr = instance();

    if (mgr._rx_queue == nullptr) return;

    /* Copy the data into a stack-allocated struct and push to queue.
     * We copy because the data/mac pointers are only valid during this 
     * callback - they'll be freed/reused immediately after we return. */
    RxMessage msg = {};
    memcpy(msg.sender_mac, recv_info->src_addr, 6);

    // Clamp data length to our buffer size (defensive coding)
    int copy_len = (data_len > ESP_NOW_MAX_DATA_LEN) ? ESP_NOW_MAX_DATA_LEN : data_len;
    memcpy(msg.data, data, copy_len);
    msg.data_len = copy_len;

    /* xQueueSend with 0 timeout: if queue is full, we drop the message.
     * This is intentional - better to drop a message than to block the 
     * WiFi task (which would cause a watchdog timeout). */
    if (xQueueSend(mgr._rx_queue, &msg, 0) != pdTRUE) {
        /* Can't use ESP_LOGW here safely in all contexts.
         * In practice this runs in a task context (not true ISR) so 
         * a brief log is usually okay, but we keep it minimal. */
        esp_rom_printf("ESP-NOW: RX queue full, message dropped!\n");
    }
}

/* =============================================================================
 * RECEIVE TASK
 * =============================================================================
 * 
 * This FreeRTOS task runs forever, waiting for messages to appear in the 
 * receive queue. When a message arrives, it calls the user's callback in 
 * this safe task context where anything goes (logging, delays, networking).
 * 
 * The task blocks on xQueueReceive with portMAX_DELAY, so it uses zero CPU
 * when no messages are coming in. It wakes up instantly when data arrives.
 * ========================================================================== */

void EspNowManager::receiveTaskFunc(void* arg) {
    EspNowManager* mgr = static_cast<EspNowManager*>(arg);
    RxMessage msg;

    ESP_LOGI(TAG, "Receive task started");

    while (true) {
        // Block until a message arrives (no timeout, wait forever)
        if (xQueueReceive(mgr->_rx_queue, &msg, portMAX_DELAY) == pdTRUE) {
            // Log at debug level (verbose, disable in production)
            char mac_str[18];
            macToStr(msg.sender_mac, mac_str);
            ESP_LOGD(TAG, "RX: %d bytes from %s", msg.data_len, mac_str);

            // Call user's callback if set
            if (mgr->_recv_cb) {
                mgr->_recv_cb(msg.sender_mac, msg.data, msg.data_len);
            }
        }
    }

    // Should never reach here, but just in case:
    vTaskDelete(nullptr);
}
