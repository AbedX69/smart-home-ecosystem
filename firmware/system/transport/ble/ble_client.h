/*
 * =============================================================================
 * FILE:        ble_client.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-14
 * VERSION:     1.0.0
 * PLATFORM:    ESP32 / ESP32-S3 / ESP32-C6 (ESP-IDF v5.x + NimBLE)
 * =============================================================================
 * 
 * BLE Client - GATT Client for reading/writing remote BLE peripherals.
 * 
 * Makes your ESP32 a BLE central that can connect to other BLE devices
 * (like sensors, locks, other ESP32s) and read/write their data.
 * 
 * =============================================================================
 * BEGINNER'S GUIDE: GATT CLIENT
 * =============================================================================
 * 
 * The client is the "asker" - it connects to a server and accesses data.
 * 
 *     ESP32 (GATT Client)             BLE Sensor (GATT Server)
 *     ┌──────────────┐                ┌──────────────────────┐
 *     │              │  1. Connect    │                      │
 *     │              │───────────────►│                      │
 *     │              │  2. Discover   │                      │
 *     │ What services│───────────────►│ I have: Temp Service │
 *     │  do you have?│                │                      │
 *     │              │  3. Read       │                      │
 *     │ Give me temp │───────────────►│ Here: 22.5°C         │
 *     │              │◄───────────────│                      │
 *     │              │  4. Subscribe  │                      │
 *     │ Notify me on │───────────────►│ OK, will notify      │
 *     │  changes     │◄─ NOTIFY ──────│ New: 23.0°C          │
 *     └──────────────┘                └──────────────────────┘
 * 
 * WORKFLOW:
 *   1. Scan for peripherals (BLEManager::startScan)
 *   2. Connect to one (BLEManager::connect)
 *   3. Discover services (BLEClient::discoverServices)
 *   4. Read/write/subscribe to characteristics
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "ble_manager.h"
 *     #include "ble_client.h"
 * 
 *     // After connecting to a peripheral:
 *     BLEClient& client = BLEClient::instance();
 *     
 *     // Discover what services the peripheral offers
 *     client.discoverServices(conn_handle);
 *     
 *     // Read a characteristic
 *     uint8_t buf[32];
 *     uint16_t len;
 *     client.read(conn_handle, char_handle, buf, sizeof(buf), &len);
 *     
 *     // Write a value
 *     uint8_t cmd = 0x01;
 *     client.write(conn_handle, char_handle, &cmd, 1);
 *     
 *     // Subscribe to notifications
 *     client.subscribe(conn_handle, char_handle, true);
 * 
 * =============================================================================
 */

#ifndef BLE_CLIENT_H
#define BLE_CLIENT_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "esp_err.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"

/* ─── Constants ──────────────────────────────────────────────────────────── */
#define BLE_CLI_MAX_SERVICES    8
#define BLE_CLI_MAX_CHARS       32

/* ─── Discovered Service/Characteristic Info ─────────────────────────────── */

struct BLEDiscoveredChar {
    uint16_t        def_handle;     ///< Characteristic definition handle
    uint16_t        val_handle;     ///< Characteristic value handle
    ble_uuid_any_t  uuid;           ///< Characteristic UUID
    uint8_t         properties;     ///< Property flags
};

struct BLEDiscoveredService {
    uint16_t        start_handle;
    uint16_t        end_handle;
    ble_uuid_any_t  uuid;
    BLEDiscoveredChar chars[BLE_CLI_MAX_CHARS];
    int             char_count;
};

/* ─── Callback Types ─────────────────────────────────────────────────────── */

/** @brief Called when a notification/indication is received from a server */
struct BLENotifyData {
    uint16_t    conn_handle;
    uint16_t    attr_handle;
    uint8_t*    data;
    uint16_t    data_len;
};

using BLENotifyCb = std::function<void(const BLENotifyData* data)>;

/** @brief Called when service discovery completes */
using BLEDiscoveryCb = std::function<void(uint16_t conn_handle, int service_count)>;

/* ─── Main Class ─────────────────────────────────────────────────────────── */

class BLEClient {
public:
    static BLEClient& instance();
    BLEClient(const BLEClient&) = delete;
    BLEClient& operator=(const BLEClient&) = delete;

    /* ─── Service Discovery ────────────────────────────────────────────── */

    /**
     * @brief Discover all services and characteristics on a connected peripheral.
     * 
     * Asynchronous - results arrive in the discovery callback.
     * After discovery, use getService()/getCharByUUID() to find handles.
     * 
     * @param conn_handle  Connection handle of the peripheral
     * @return ESP_OK if discovery started
     */
    esp_err_t discoverServices(uint16_t conn_handle);

    /**
     * @brief Get discovered services after discovery completes.
     * @param conn_handle  Connection handle
     * @param count        Output: number of services found
     * @return Pointer to array of services, or nullptr
     */
    const BLEDiscoveredService* getServices(uint16_t conn_handle, int& count) const;

    /**
     * @brief Find a characteristic by UUID across all discovered services.
     * @param conn_handle  Connection handle
     * @param uuid_str     UUID string to find
     * @return Pointer to discovered characteristic, or nullptr
     */
    const BLEDiscoveredChar* getCharByUUID(uint16_t conn_handle,
                                            const char* uuid_str) const;

    /* ─── Read / Write ─────────────────────────────────────────────────── */

    /**
     * @brief Read a characteristic value from the remote device.
     * 
     * Blocking call - waits for the response.
     * 
     * @param conn_handle   Connection handle
     * @param attr_handle   Characteristic value handle
     * @param buf           Buffer to store the read value
     * @param buf_len       Buffer size
     * @param out_len       Output: actual bytes read
     * @return ESP_OK on success
     */
    esp_err_t read(uint16_t conn_handle, uint16_t attr_handle,
                   uint8_t* buf, uint16_t buf_len, uint16_t* out_len);

    /**
     * @brief Write a value to a remote characteristic (with response).
     * 
     * Blocking call - waits for confirmation from the server.
     * 
     * @param conn_handle   Connection handle
     * @param attr_handle   Characteristic value handle
     * @param data          Data to write
     * @param len           Data length
     * @return ESP_OK on success
     */
    esp_err_t write(uint16_t conn_handle, uint16_t attr_handle,
                    const uint8_t* data, uint16_t len);

    /**
     * @brief Write without response (fire-and-forget).
     * 
     * Faster but no confirmation. Use for high-frequency updates.
     */
    esp_err_t writeNoResponse(uint16_t conn_handle, uint16_t attr_handle,
                               const uint8_t* data, uint16_t len);

    /* ─── Notifications / Indications ──────────────────────────────────── */

    /**
     * @brief Subscribe to notifications or indications from a characteristic.
     * 
     * Writes to the CCCD (Client Characteristic Configuration Descriptor)
     * to enable notifications (0x0001) or indications (0x0002).
     * 
     * @param conn_handle   Connection handle
     * @param attr_handle   Characteristic value handle
     * @param notify        true = notifications, false = indications
     * @return ESP_OK on success
     */
    esp_err_t subscribe(uint16_t conn_handle, uint16_t attr_handle, bool notify = true);

    /**
     * @brief Unsubscribe from notifications/indications.
     */
    esp_err_t unsubscribe(uint16_t conn_handle, uint16_t attr_handle);

    /* ─── Callbacks ────────────────────────────────────────────────────── */

    /** @brief Set callback for incoming notifications/indications */
    void setNotifyCallback(BLENotifyCb cb);

    /** @brief Set callback for discovery completion */
    void setDiscoveryCallback(BLEDiscoveryCb cb);

    /* ─── Internal (called by BLEManager's GAP handler) ────────────────── */

    /** @brief Handle incoming notification event from NimBLE */
    void handleNotify(uint16_t conn_handle, uint16_t attr_handle,
                      struct os_mbuf* om);

private:
    BLEClient();
    ~BLEClient();

    /* NimBLE discovery callbacks */
    static int svcDiscoveryCb(uint16_t conn_handle,
                               const struct ble_gatt_error* error,
                               const struct ble_gatt_svc* service, void* arg);
    static int chrDiscoveryCb(uint16_t conn_handle,
                               const struct ble_gatt_error* error,
                               const struct ble_gatt_chr* chr, void* arg);
    /* Read callback */
    static int readCb(uint16_t conn_handle,
                       const struct ble_gatt_error* error,
                       struct ble_gatt_attr* attr, void* arg);
    /* Write callback */
    static int writeCb(uint16_t conn_handle,
                        const struct ble_gatt_error* error,
                        struct ble_gatt_attr* attr, void* arg);

    static bool parseUUID(const char* str, ble_uuid_any_t* uuid);
    static bool uuidMatch(const ble_uuid_any_t* a, const ble_uuid_any_t* b);

    /* Per-connection discovery state */
    struct DiscoveryState {
        uint16_t            conn_handle;
        BLEDiscoveredService services[BLE_CLI_MAX_SERVICES];
        int                 svc_count;
        int                 current_svc;    // Which service we're discovering chars for
        bool                in_progress;
    };
    DiscoveryState _discovery;

    /* Synchronous read/write state */
    struct SyncOpState {
        SemaphoreHandle_t   sem;
        uint8_t*            buf;
        uint16_t            buf_len;
        uint16_t            result_len;
        int                 status;
    };
    SyncOpState _sync_op;

    BLENotifyCb     _notify_cb;
    BLEDiscoveryCb  _discovery_cb;
};

#endif // BLE_CLIENT_H
