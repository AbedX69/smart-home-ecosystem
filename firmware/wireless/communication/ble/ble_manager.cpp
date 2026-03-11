/*
 * =============================================================================
 * FILE:        ble_manager.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-14
 * VERSION:     1.0.0
 * =============================================================================
 */

#include "ble_manager.h"

static const char* TAG = "BLEManager";

/* =============================================================================
 * SINGLETON
 * ========================================================================== */

BLEManager& BLEManager::instance() {
    static BLEManager inst;
    return inst;
}

BLEManager::BLEManager()
    : _initialized(false)
    , _advertising(false)
    , _scanning(false)
    , _own_addr_type(0)
    , _event_cb(nullptr)
{
    memset(_device_name, 0, sizeof(_device_name));
    memset(_connections, 0, sizeof(_connections));
    _mutex = xSemaphoreCreateMutex();
}

BLEManager::~BLEManager() {
    end();
    if (_mutex) vSemaphoreDelete(_mutex);
}

/* =============================================================================
 * LIFECYCLE
 * =============================================================================
 * 
 * NimBLE initialization in ESP-IDF v5.x:
 *   1. Initialize NVS (for bonding data storage)
 *   2. Call nimble_port_init() - sets up the NimBLE host + controller
 *   3. Configure GAP device name
 *   4. Configure GATT services (must happen before host starts)
 *   5. Set host callbacks (on_sync, on_reset)
 *   6. Start NimBLE host task via nimble_port_freertos_init()
 * 
 * The host task runs forever handling BLE events. When it calls on_sync(),
 * the stack is ready and we can start advertising/scanning.
 * ========================================================================== */

esp_err_t BLEManager::begin(const char* device_name) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    if (_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        xSemaphoreGive(_mutex);
        return ESP_OK;
    }

    /* ── NVS (needed for bonding persistence) ──────────────────────── */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        xSemaphoreGive(_mutex);
        return ret;
    }

    /* ── Initialize NimBLE port ────────────────────────────────────── */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE port init failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(_mutex);
        return ret;
    }

    /* ── Store device name ─────────────────────────────────────────── */
    strncpy(_device_name, device_name, BLE_MAX_DEVICE_NAME);
    ble_svc_gap_device_name_set(_device_name);

    /* ── Initialize GAP and GATT services ──────────────────────────── */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* ── Configure host callbacks ──────────────────────────────────── 
     * on_sync: Called when the BLE host and controller are synchronized.
     *          This is where we determine our address type and start
     *          advertising or scanning.
     * on_reset: Called if the BLE host resets (error recovery). */
    ble_hs_cfg.sync_cb = onSync;
    ble_hs_cfg.reset_cb = onReset;

    /* Security manager config (sensible defaults) */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;    // No display/keyboard
    ble_hs_cfg.sm_bonding = 1;                       // Enable bonding
    ble_hs_cfg.sm_mitm = 0;                          // No MITM protection
    ble_hs_cfg.sm_sc = 1;                            // Secure connections

    /* Store config for bonding persistence */
    ble_store_config_init();

    /* ── Start NimBLE host task ────────────────────────────────────── */
    nimble_port_freertos_init(nimbleHostTask);

    _initialized = true;

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  BLE initialized (NimBLE)");
    ESP_LOGI(TAG, "  Device name: %s", _device_name);
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    xSemaphoreGive(_mutex);
    return ESP_OK;
}

esp_err_t BLEManager::end() {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    if (!_initialized) {
        xSemaphoreGive(_mutex);
        return ESP_OK;
    }

    /* Disconnect all connections */
    disconnectAll();

    /* Stop NimBLE. nimble_port_deinit() stops the host task. */
    int rc = nimble_port_stop();
    if (rc == 0) {
        nimble_port_deinit();
    }

    _initialized = false;
    _advertising = false;
    _scanning = false;

    ESP_LOGI(TAG, "BLE stopped");
    xSemaphoreGive(_mutex);
    return ESP_OK;
}

bool BLEManager::isReady() const { return _initialized; }

/* =============================================================================
 * NimBLE HOST TASK & CALLBACKS
 * ========================================================================== */

void BLEManager::nimbleHostTask(void* arg) {
    ESP_LOGI(TAG, "NimBLE host task started");
    /* This function runs the NimBLE host event loop. It returns only when
     * nimble_port_stop() is called. */
    nimble_port_run();
    /* Clean up after host task ends */
    nimble_port_freertos_deinit();
}

void BLEManager::onSync() {
    /* Determine the best address type to use.
     * This handles both public and random addresses. */
    BLEManager& mgr = instance();

    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Address ensure failed: %d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &mgr._own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Address type infer failed: %d", rc);
        return;
    }

    /* Print our BLE address */
    uint8_t addr[6] = {};
    ble_hs_id_copy_addr(mgr._own_addr_type, addr, nullptr);
    char addr_str[18];
    addrToStr(addr, addr_str);
    ESP_LOGI(TAG, "BLE address: %s (type=%d)", addr_str, mgr._own_addr_type);

    mgr.emitEvent(BLEEvent::INITIALIZED);
}

void BLEManager::onReset(int reason) {
    ESP_LOGE(TAG, "NimBLE host reset, reason: %d", reason);
}

/* =============================================================================
 * ADVERTISING
 * =============================================================================
 * 
 * Advertising makes this device visible to scanning centrals.
 * The advertising data contains:
 *   - Flags (general discoverable, BLE only)
 *   - TX power level
 *   - Device name
 * 
 * Optionally, 128-bit service UUIDs can be included so scanners know
 * what services we offer before connecting.
 * ========================================================================== */

esp_err_t BLEManager::startAdvertising(const BLEAdvConfig& config) {
    if (!_initialized) return ESP_ERR_INVALID_STATE;

    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = config.connectable ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = config.adv_itvl_min;
    adv_params.itvl_max = config.adv_itvl_max;

    /* Build advertising data */
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t*)_device_name;
    fields.name_len = strlen(_device_name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Setting adv fields failed: %d", rc);
        return ESP_FAIL;
    }

    /* Duration: convert ms to BLE units (10ms each). 0 = forever. */
    int32_t duration = (config.duration_ms > 0) ?
                       (config.duration_ms / 10) : BLE_HS_FOREVER;

    rc = ble_gap_adv_start(_own_addr_type, nullptr, duration,
                           &adv_params, gapEventHandler, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Advertising start failed: %d", rc);
        return ESP_FAIL;
    }

    _advertising = true;
    ESP_LOGI(TAG, "Advertising started: \"%s\"", _device_name);
    return ESP_OK;
}

esp_err_t BLEManager::stopAdvertising() {
    int rc = ble_gap_adv_stop();
    _advertising = false;
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

bool BLEManager::isAdvertising() const { return _advertising; }

/* =============================================================================
 * SCANNING
 * ========================================================================== */

esp_err_t BLEManager::startScan(const BLEScanConfig& config) {
    if (!_initialized) return ESP_ERR_INVALID_STATE;

    struct ble_gap_disc_params disc_params = {};
    disc_params.passive = config.passive ? 1 : 0;
    disc_params.filter_duplicates = config.filter_dup ? 1 : 0;
    disc_params.itvl = config.itvl;
    disc_params.window = config.window;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    int32_t duration = (config.duration_ms > 0) ?
                       (config.duration_ms / 10) : BLE_HS_FOREVER;

    int rc = ble_gap_disc(_own_addr_type, duration, &disc_params,
                          gapEventHandler, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Scan start failed: %d", rc);
        return ESP_FAIL;
    }

    _scanning = true;
    ESP_LOGI(TAG, "Scanning started (%ld ms)", (long)config.duration_ms);
    return ESP_OK;
}

esp_err_t BLEManager::stopScan() {
    ble_gap_disc_cancel();
    _scanning = false;
    return ESP_OK;
}

bool BLEManager::isScanning() const { return _scanning; }

/* =============================================================================
 * CONNECTION MANAGEMENT
 * ========================================================================== */

esp_err_t BLEManager::connect(const uint8_t* addr, uint8_t addr_type) {
    if (!_initialized || !addr) return ESP_ERR_INVALID_ARG;

    /* Stop scanning before connecting (required by NimBLE) */
    if (_scanning) stopScan();

    ble_addr_t peer_addr;
    peer_addr.type = addr_type;
    memcpy(peer_addr.val, addr, 6);

    int rc = ble_gap_connect(_own_addr_type, &peer_addr, 30000,
                             nullptr, gapEventHandler, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Connect failed: %d", rc);
        return ESP_FAIL;
    }

    char addr_str[18];
    addrToStr(addr, addr_str);
    ESP_LOGI(TAG, "Connecting to %s...", addr_str);
    return ESP_OK;
}

esp_err_t BLEManager::disconnect(uint16_t conn_handle) {
    int rc = ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t BLEManager::disconnectAll() {
    for (int i = 0; i < BLE_MAX_CONNECTIONS; i++) {
        if (_connections[i].active) {
            ble_gap_terminate(_connections[i].handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
    return ESP_OK;
}

int BLEManager::getConnectionCount() const {
    int count = 0;
    for (int i = 0; i < BLE_MAX_CONNECTIONS; i++) {
        if (_connections[i].active) count++;
    }
    return count;
}

bool BLEManager::isConnected(uint16_t conn_handle) const {
    for (int i = 0; i < BLE_MAX_CONNECTIONS; i++) {
        if (_connections[i].active && _connections[i].handle == conn_handle) {
            return true;
        }
    }
    return false;
}

/* =============================================================================
 * GAP EVENT HANDLER
 * =============================================================================
 * 
 * This is the central callback for all GAP events in NimBLE.
 * It handles connections, disconnections, advertising events, scan results, etc.
 * 
 * NimBLE calls this from its host task thread. It's safe to do moderate work
 * here, but avoid long blocking operations.
 * ========================================================================== */

int BLEManager::gapEventHandler(struct ble_gap_event* event, void* arg) {
    BLEManager& mgr = instance();
    BLEEventInfo info = {};
    struct ble_gap_conn_desc desc;

    switch (event->type) {

        case BLE_GAP_EVENT_CONNECT: {
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "Connection established (handle=%d)", event->connect.conn_handle);

                ble_gap_conn_find(event->connect.conn_handle, &desc);
                info.conn_handle = event->connect.conn_handle;
                memcpy(info.peer_addr, desc.peer_id_addr.val, 6);
                info.peer_addr_type = desc.peer_id_addr.type;

                /* Track connection */
                for (int i = 0; i < BLE_MAX_CONNECTIONS; i++) {
                    if (!mgr._connections[i].active) {
                        mgr._connections[i].handle = event->connect.conn_handle;
                        mgr._connections[i].active = true;
                        memcpy(mgr._connections[i].addr, desc.peer_id_addr.val, 6);
                        break;
                    }
                }

                mgr.emitEvent(BLEEvent::CONNECTED, &info);
            } else {
                ESP_LOGW(TAG, "Connection failed: status=%d", event->connect.status);
                /* Restart advertising if we were a peripheral */
                mgr.startAdvertising();
            }
            return 0;
        }

        case BLE_GAP_EVENT_DISCONNECT: {
            ESP_LOGI(TAG, "Disconnected (handle=%d, reason=%d)",
                     event->disconnect.conn.conn_handle,
                     event->disconnect.reason);

            info.conn_handle = event->disconnect.conn.conn_handle;
            memcpy(info.peer_addr, event->disconnect.conn.peer_id_addr.val, 6);

            /* Remove from tracked connections */
            for (int i = 0; i < BLE_MAX_CONNECTIONS; i++) {
                if (mgr._connections[i].active &&
                    mgr._connections[i].handle == event->disconnect.conn.conn_handle) {
                    mgr._connections[i].active = false;
                    break;
                }
            }

            mgr.emitEvent(BLEEvent::DISCONNECTED, &info);

            /* Auto-restart advertising after disconnect */
            mgr.startAdvertising();
            return 0;
        }

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, "Advertising complete");
            mgr._advertising = false;
            mgr.emitEvent(BLEEvent::ADV_COMPLETE);
            return 0;

        case BLE_GAP_EVENT_DISC: {
            /* Scan result */
            info.rssi = event->disc.rssi;
            memcpy(info.peer_addr, event->disc.addr.val, 6);
            info.peer_addr_type = event->disc.addr.type;
            info.adv_data_len = event->disc.length_data;
            if (event->disc.length_data <= sizeof(info.adv_data)) {
                memcpy(info.adv_data, event->disc.data, event->disc.length_data);
            }
            mgr.extractNameFromAdv(event->disc.data, event->disc.length_data,
                                   info.name, sizeof(info.name));

            mgr.emitEvent(BLEEvent::SCAN_RESULT, &info);
            return 0;
        }

        case BLE_GAP_EVENT_DISC_COMPLETE:
            ESP_LOGI(TAG, "Scan complete");
            mgr._scanning = false;
            mgr.emitEvent(BLEEvent::SCAN_COMPLETE);
            return 0;

        case BLE_GAP_EVENT_CONN_UPDATE:
            info.conn_handle = event->conn_update.conn_handle;
            mgr.emitEvent(BLEEvent::CONN_UPDATED, &info);
            return 0;

        case BLE_GAP_EVENT_MTU: {
            info.conn_handle = event->mtu.conn_handle;
            info.mtu = event->mtu.value;
            ESP_LOGI(TAG, "MTU updated: %d (handle=%d)", event->mtu.value,
                     event->mtu.conn_handle);
            mgr.emitEvent(BLEEvent::MTU_CHANGED, &info);
            return 0;
        }

        case BLE_GAP_EVENT_SUBSCRIBE: {
            info.conn_handle = event->subscribe.conn_handle;
            info.attr_handle = event->subscribe.attr_handle;
            bool subscribed = (event->subscribe.cur_notify || event->subscribe.cur_indicate);
            ESP_LOGI(TAG, "%s (handle=%d, attr=%d)",
                     subscribed ? "Subscribed" : "Unsubscribed",
                     event->subscribe.conn_handle, event->subscribe.attr_handle);
            mgr.emitEvent(subscribed ? BLEEvent::SUBSCRIBE : BLEEvent::UNSUBSCRIBE, &info);
            return 0;
        }

        case BLE_GAP_EVENT_ENC_CHANGE: {
            ble_gap_conn_find(event->enc_change.conn_handle, &desc);
            info.conn_handle = event->enc_change.conn_handle;
            info.encrypted = desc.sec_state.encrypted;
            info.authenticated = desc.sec_state.authenticated;
            ESP_LOGI(TAG, "Encryption changed: enc=%d auth=%d",
                     desc.sec_state.encrypted, desc.sec_state.authenticated);
            mgr.emitEvent(BLEEvent::ENC_CHANGE, &info);
            return 0;
        }

        default:
            ESP_LOGD(TAG, "Unhandled GAP event: %d", event->type);
            return 0;
    }
}

/* =============================================================================
 * CALLBACKS & UTILITIES
 * ========================================================================== */

void BLEManager::setEventCallback(BLEEventCb cb) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _event_cb = cb;
    xSemaphoreGive(_mutex);
}

void BLEManager::emitEvent(BLEEvent event, const BLEEventInfo* info) {
    if (_event_cb) _event_cb(event, info);
}

const char* BLEManager::getDeviceName() const { return _device_name; }

esp_err_t BLEManager::getAddress(uint8_t* addr) const {
    if (!addr) return ESP_ERR_INVALID_ARG;
    ble_hs_id_copy_addr(_own_addr_type, addr, nullptr);
    return ESP_OK;
}

void BLEManager::addrToStr(const uint8_t* addr, char* buf) {
    if (!addr || !buf) return;
    snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

void BLEManager::extractNameFromAdv(const uint8_t* data, uint8_t len,
                                     char* name, size_t name_len) {
    memset(name, 0, name_len);
    /* Walk through AD structures looking for name field */
    uint8_t pos = 0;
    while (pos < len) {
        uint8_t ad_len = data[pos];
        if (ad_len == 0 || pos + ad_len >= len) break;
        uint8_t ad_type = data[pos + 1];
        /* 0x09 = Complete Local Name, 0x08 = Shortened Local Name */
        if (ad_type == 0x09 || ad_type == 0x08) {
            uint8_t copy_len = ad_len - 1;
            if (copy_len >= name_len) copy_len = name_len - 1;
            memcpy(name, &data[pos + 2], copy_len);
            name[copy_len] = '\0';
            return;
        }
        pos += ad_len + 1;
    }
}
