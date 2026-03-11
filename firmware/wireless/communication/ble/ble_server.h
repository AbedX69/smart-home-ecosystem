/*
 * =============================================================================
 * FILE:        ble_server.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-14
 * VERSION:     1.0.0
 * PLATFORM:    ESP32 / ESP32-S3 / ESP32-C6 (ESP-IDF v5.x + NimBLE)
 * =============================================================================
 * 
 * BLE Server - GATT Server for hosting services and characteristics.
 * 
 * Makes your ESP32 a BLE peripheral that phones or other centrals can
 * connect to and read/write data from.
 * 
 * =============================================================================
 * BEGINNER'S GUIDE: GATT SERVER
 * =============================================================================
 * 
 * WHAT DOES A GATT SERVER DO?
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * It exposes data that clients can read, write, or subscribe to.
 * Think of it as a tiny database that BLE clients can access.
 * 
 *     Phone (GATT Client)                ESP32 (GATT Server)
 *     ┌──────────────┐                   ┌──────────────────────┐
 *     │              │                   │ Service: SmartHome   │
 *     │  Read temp   │──── READ ────────►│  ├─ Char: Temp       │
 *     │  Got: 22.5°C │◄─── 22.5 ────────│  │   val: 22.5       │
 *     │              │                   │  ├─ Char: LED        │
 *     │  Turn on LED │── WRITE 0x01 ────►│  │   val: ON         │
 *     │              │                   │  └─ Char: Motion     │
 *     │  Motion!     │◄── NOTIFY ────────│      val: detected!  │
 *     └──────────────┘                   └──────────────────────┘
 * 
 * 
 * DATA ACCESS TYPES:
 * ~~~~~~~~~~~~~~~~~~
 *   READ       Client asks for current value → server responds
 *   WRITE      Client sends a value → server stores/acts on it
 *   NOTIFY     Server pushes updates to client (no confirmation)
 *   INDICATE   Server pushes updates to client (with confirmation)
 * 
 * 
 * HOW TO DEFINE SERVICES:
 * ~~~~~~~~~~~~~~~~~~~~~~~
 * 
 * In NimBLE, GATT tables are defined as static arrays of structs BEFORE
 * the stack starts. This is different from Bluedroid where you can add
 * services dynamically. The flow is:
 * 
 *   1. Define your service table (array of ble_gatt_svc_def)
 *   2. Call BLEServer::registerServices() with your table
 *   3. Call BLEManager::begin() to start the stack
 *   4. NimBLE registers your services automatically during init
 * 
 * This component simplifies that process - you define characteristics with
 * simple structs and it builds the NimBLE table for you.
 * 
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "ble_manager.h"
 *     #include "ble_server.h"
 * 
 *     // Define characteristics
 *     static uint8_t temp_val[2] = {0, 22};    // Temperature
 *     static uint8_t led_val = 0;               // LED on/off
 * 
 *     // Called when client reads/writes
 *     void onAccess(BLECharAccess* access) {
 *         if (access->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
 *             if (access->char_uuid == led_uuid) {
 *                 led_val = access->data[0];
 *                 gpio_set_level(LED_PIN, led_val);
 *             }
 *         }
 *     }
 * 
 *     extern "C" void app_main(void) {
 *         BLEServer& server = BLEServer::instance();
 *         server.setAccessCallback(onAccess);
 * 
 *         // Create a service with characteristics
 *         server.addService("12345678-1234-1234-1234-123456789ABC");
 *         server.addCharacteristic("12345678-1234-1234-1234-123456789001",
 *                                  BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
 *                                  temp_val, sizeof(temp_val));
 *         server.addCharacteristic("12345678-1234-1234-1234-123456789002",
 *                                  BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
 *                                  &led_val, sizeof(led_val));
 *         server.buildServices();  // Finalize the GATT table
 * 
 *         // THEN start BLE
 *         BLEManager::instance().begin("SmartLight");
 *         BLEManager::instance().startAdvertising();
 * 
 *         // Later, notify clients of temperature change
 *         temp_val[1] = 25;
 *         server.notify(temp_char_handle, temp_val, 2);
 *     }
 * 
 * =============================================================================
 */

#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "esp_err.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"

/* ─── Constants ──────────────────────────────────────────────────────────── */
#define BLE_SRV_MAX_SERVICES        4
#define BLE_SRV_MAX_CHARS_PER_SVC   8
#define BLE_SRV_MAX_CHARS_TOTAL     (BLE_SRV_MAX_SERVICES * BLE_SRV_MAX_CHARS_PER_SVC)

/* ─── Characteristic Access Info ─────────────────────────────────────────── */

/**
 * @brief Info passed to the access callback when a client reads/writes.
 */
struct BLECharAccess {
    uint16_t    conn_handle;    ///< Connection handle of the client
    uint16_t    attr_handle;    ///< Attribute handle of the characteristic
    uint8_t     op;             ///< BLE_GATT_ACCESS_OP_READ_CHR or _WRITE_CHR
    ble_uuid_any_t char_uuid;  ///< UUID of the characteristic being accessed
    uint8_t*    data;           ///< Data pointer (write: incoming data; read: fill this)
    uint16_t    data_len;       ///< Data length
};

using BLEAccessCb = std::function<void(BLECharAccess* access)>;

/* ─── Characteristic Definition ──────────────────────────────────────────── */

/**
 * @brief User-friendly characteristic definition.
 * 
 * This gets converted into NimBLE's ble_gatt_chr_def internally.
 */
struct BLECharDef {
    ble_uuid_any_t  uuid;
    uint16_t        flags;          ///< BLE_GATT_CHR_F_READ | _WRITE | _NOTIFY etc.
    uint8_t*        value;          ///< Pointer to the value buffer
    uint16_t        value_len;      ///< Current length of value
    uint16_t        max_len;        ///< Max buffer capacity
    uint16_t*       val_handle;     ///< Output: NimBLE fills this with the attr handle
};

/* ─── Main Class ─────────────────────────────────────────────────────────── */

class BLEServer {
public:
    static BLEServer& instance();
    BLEServer(const BLEServer&) = delete;
    BLEServer& operator=(const BLEServer&) = delete;

    /* ─── Service Building ─────────────────────────────────────────────── */

    /**
     * @brief Start defining a new service.
     * 
     * @param uuid_str  128-bit UUID string (e.g., "12345678-1234-...")
     *                  or 16-bit hex (e.g., "0x180D" for Heart Rate)
     * @return Index of the service, or -1 on error
     */
    int addService(const char* uuid_str);

    /**
     * @brief Add a characteristic to the LAST added service.
     * 
     * @param uuid_str   Characteristic UUID string
     * @param flags      Property flags (OR together):
     *                     BLE_GATT_CHR_F_READ
     *                     BLE_GATT_CHR_F_WRITE
     *                     BLE_GATT_CHR_F_WRITE_NO_RSP
     *                     BLE_GATT_CHR_F_NOTIFY
     *                     BLE_GATT_CHR_F_INDICATE
     * @param value      Pointer to value buffer (must persist!)
     * @param value_len  Current length of value
     * @param max_len    Max capacity of value buffer (default = value_len)
     * @param val_handle Output: receives the attribute handle after registration
     * @return 0 on success, -1 on error
     */
    int addCharacteristic(const char* uuid_str, uint16_t flags,
                          uint8_t* value, uint16_t value_len,
                          uint16_t max_len = 0,
                          uint16_t* val_handle = nullptr);

    /**
     * @brief Finalize and register all services with NimBLE.
     * 
     * MUST be called after all addService/addCharacteristic calls
     * and BEFORE BLEManager::begin().
     * 
     * @return ESP_OK on success
     */
    esp_err_t buildServices();

    /* ─── Runtime Operations ───────────────────────────────────────────── */

    /**
     * @brief Send a notification to a connected client.
     * 
     * @param attr_handle  The val_handle from addCharacteristic
     * @param data         Data to send
     * @param len          Data length
     * @param conn_handle  Specific connection (0xFFFF = all connected)
     * @return ESP_OK on success
     */
    esp_err_t notify(uint16_t attr_handle, const uint8_t* data, uint16_t len,
                     uint16_t conn_handle = 0xFFFF);

    /**
     * @brief Send an indication to a connected client (with ACK).
     */
    esp_err_t indicate(uint16_t attr_handle, const uint8_t* data, uint16_t len,
                       uint16_t conn_handle = 0xFFFF);

    /**
     * @brief Update a characteristic value locally.
     * 
     * Updates the stored value. Does NOT notify/indicate clients -
     * call notify() or indicate() separately if needed.
     */
    esp_err_t setValue(uint16_t attr_handle, const uint8_t* data, uint16_t len);

    /* ─── Callbacks ────────────────────────────────────────────────────── */

    /**
     * @brief Set callback for characteristic read/write access.
     * 
     * Called whenever a client reads or writes a characteristic.
     * For writes, process the incoming data.
     * For reads, optionally update the value before it's sent.
     */
    void setAccessCallback(BLEAccessCb cb);

private:
    BLEServer();
    ~BLEServer();

    /* NimBLE GATT access callback */
    static int gattAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt* ctxt, void* arg);

    /* UUID parsing */
    static bool parseUUID(const char* str, ble_uuid_any_t* uuid);

    /* ─── Internal GATT Table Storage ──────────────────────────────────── */

    /* NimBLE requires these arrays to persist for the lifetime of the stack */
    struct SvcStorage {
        ble_uuid_any_t              uuid;
        ble_gatt_chr_def            chars[BLE_SRV_MAX_CHARS_PER_SVC + 1];  // +1 for terminator
        ble_uuid_any_t              char_uuids[BLE_SRV_MAX_CHARS_PER_SVC];
        uint16_t*                   char_val_handles[BLE_SRV_MAX_CHARS_PER_SVC];
        int                         char_count;
    };

    SvcStorage          _svcs[BLE_SRV_MAX_SERVICES];
    int                 _svc_count;
    ble_gatt_svc_def    _svc_defs[BLE_SRV_MAX_SERVICES + 1];  // +1 for terminator
    bool                _built;

    /* Characteristic value storage for access callback */
    struct CharValueRef {
        uint8_t*    buf;
        uint16_t    len;
        uint16_t    max_len;
    };
    CharValueRef        _char_values[BLE_SRV_MAX_CHARS_TOTAL];
    int                 _total_chars;

    BLEAccessCb         _access_cb;
};

#endif // BLE_SERVER_H
