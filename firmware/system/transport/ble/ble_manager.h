/*
 * =============================================================================
 * FILE:        ble_manager.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-14
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    ESP32 / ESP32-S3 / ESP32-C6 (ESP-IDF v5.x + NimBLE)
 * =============================================================================
 * 
 * BLE Manager - Core Bluetooth Low Energy management using NimBLE stack.
 * 
 * Handles NimBLE initialization, GAP advertising/scanning, connection events,
 * and serves as the foundation for BLE Server and BLE Client modules.
 * 
 * =============================================================================
 * BEGINNER'S GUIDE: BLUETOOTH LOW ENERGY (BLE)
 * =============================================================================
 * 
 * WHAT IS BLE?
 * ~~~~~~~~~~~~
 * BLE (Bluetooth Low Energy) is a wireless technology for short-range 
 * communication. Unlike "Classic" Bluetooth (used for audio streaming),
 * BLE is designed for tiny bursts of data with minimal power consumption.
 * 
 * Perfect for: sensors, remote controls, beacons, phone ↔ device communication.
 * 
 * 
 * KEY CONCEPTS:
 * ~~~~~~~~~~~~~
 * 
 * 1. GAP (Generic Access Profile) - WHO can connect and HOW
 * 
 *    ┌─────────────┐          ┌─────────────┐
 *    │ PERIPHERAL  │          │   CENTRAL   │
 *    │ (Server)    │          │  (Client)   │
 *    │             │          │             │
 *    │ Advertises  │ ◄─ scan ─│ Scans       │
 *    │ "I'm here!" │          │ "Who's out  │
 *    │             │          │  there?"    │
 *    │             │── conn ──│             │
 *    │             │  request │             │
 *    └─────────────┘          └─────────────┘
 * 
 *    PERIPHERAL: Broadcasts advertisements ("I exist, here's my name")
 *    CENTRAL: Scans for peripherals, initiates connections
 * 
 * 
 * 2. GATT (Generic Attribute Profile) - WHAT data is exchanged
 * 
 *    After connecting, data is organized as:
 * 
 *    ┌─ Service (e.g., "Temperature Sensor")     UUID: 0x1809
 *    │   ├─ Characteristic: "Temperature"         UUID: 0x2A1C
 *    │   │   ├─ Value: 22.5°C
 *    │   │   └─ Descriptor: CCCD (enable notifications)
 *    │   └─ Characteristic: "Units"               UUID: custom
 *    │       └─ Value: "celsius"
 *    │
 *    └─ Service (e.g., "Device Control")          UUID: custom
 *        ├─ Characteristic: "LED"                 UUID: custom
 *        │   └─ Value: 0x01 (on/off)
 *        └─ Characteristic: "Buzzer"              UUID: custom
 *            └─ Value: 0x00
 * 
 *    Services contain Characteristics. Characteristics have Values.
 *    Think of it like folders (services) containing files (characteristics).
 * 
 * 
 * 3. UUIDs - Unique identifiers for services and characteristics
 * 
 *    Standard (16-bit):  0x1809 = Health Thermometer Service
 *    Custom (128-bit):   12345678-1234-1234-1234-123456789ABC
 * 
 *    Use standard UUIDs when your data matches a Bluetooth SIG definition.
 *    Use custom 128-bit UUIDs for your own proprietary data.
 * 
 * 
 * =============================================================================
 * WHY NimBLE INSTEAD OF BLUEDROID?
 * =============================================================================
 * 
 *     ┌─────────────────────────────────────────────────────┐
 *     │  Feature          │  NimBLE        │  Bluedroid     │
 *     ├─────────────────────────────────────────────────────┤
 *     │  RAM usage        │  ~40 KB        │  ~80 KB        │
 *     │  Flash usage      │  ~200 KB       │  ~350 KB       │
 *     │  BLE support      │  YES           │  YES           │
 *     │  Classic BT       │  NO            │  YES           │
 *     │  API complexity   │  Lower         │  Higher        │
 *     │  Maintained       │  Active        │  Legacy-ish    │
 *     └─────────────────────────────────────────────────────┘
 * 
 * Since we only need BLE (not Classic), NimBLE saves ~40KB RAM.
 * That's significant when you're also running WiFi + ESP-NOW.
 * 
 * 
 * =============================================================================
 * BLE + WiFi + ESP-NOW COEXISTENCE
 * =============================================================================
 * 
 * ESP32 can run BLE and WiFi simultaneously! They share the same 2.4GHz
 * radio but the hardware has a coexistence arbiter that time-slices between
 * them. Performance of both degrades slightly when running together.
 * 
 *     ┌──────────────────────────────────┐
 *     │           ESP32 Radio            │
 *     │  ┌────────┐ ┌──────┐ ┌────────┐ │
 *     │  │  WiFi  │ │ BLE  │ │ESP-NOW │ │
 *     │  │  STA   │ │Server│ │  Msgs  │ │
 *     │  └────────┘ └──────┘ └────────┘ │
 *     │     ◄── time-shared ──►         │
 *     └──────────────────────────────────┘
 * 
 * 
 * =============================================================================
 * SMART HOME USE CASES FOR BLE
 * =============================================================================
 * 
 *   • Phone app → ESP32: Direct device control (no WiFi router needed)
 *   • ESP32 → Phone: Push sensor data to a custom app
 *   • Proximity detection: Unlock door when phone is near
 *   • Beacons: Room presence detection for automation
 *   • WiFi provisioning: Send WiFi credentials via BLE from phone app
 *   • Mesh: BLE Mesh for whole-home device communication (future)
 * 
 * 
 * =============================================================================
 * USAGE EXAMPLE
 * =============================================================================
 * 
 *     #include "ble_manager.h"
 * 
 *     void onBLEEvent(BLEEvent event, const BLEEventInfo* info) {
 *         if (event == BLEEvent::CONNECTED) {
 *             ESP_LOGI("APP", "Phone connected!");
 *         }
 *     }
 * 
 *     extern "C" void app_main(void) {
 *         BLEManager& ble = BLEManager::instance();
 *         ble.setEventCallback(onBLEEvent);
 *         ble.begin("SmartHome-Light");
 *         ble.startAdvertising();
 *     }
 * 
 * =============================================================================
 */

#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include "esp_nimble_hci.h"
#include "nvs_flash.h"

/* ─── Constants ──────────────────────────────────────────────────────────── */
#define BLE_MAX_DEVICE_NAME     32
#define BLE_MAX_CONNECTIONS     3
#define BLE_SCAN_DURATION_MS    10000   ///< Default scan duration

/* ─── Event Types ────────────────────────────────────────────────────────── */

enum class BLEEvent {
    INITIALIZED,        ///< NimBLE stack synced and ready
    CONNECTED,          ///< A device connected
    DISCONNECTED,       ///< A device disconnected
    CONN_UPDATED,       ///< Connection parameters updated
    MTU_CHANGED,        ///< MTU negotiated (data size per packet)
    SCAN_RESULT,        ///< A device found during scanning
    SCAN_COMPLETE,      ///< Scanning finished
    ADV_COMPLETE,       ///< Advertising timed out
    SUBSCRIBE,          ///< Client subscribed to notifications/indications
    UNSUBSCRIBE,        ///< Client unsubscribed
    PASSKEY_REQUEST,    ///< Pairing passkey needed
    ENC_CHANGE,         ///< Encryption status changed
};

struct BLEEventInfo {
    uint16_t    conn_handle;        ///< Connection handle
    uint8_t     peer_addr[6];       ///< Peer MAC address
    uint8_t     peer_addr_type;     ///< Peer address type
    int8_t      rssi;               ///< RSSI (for scan results)
    uint16_t    mtu;                ///< Negotiated MTU
    uint16_t    attr_handle;        ///< Attribute handle (for subscribe events)
    bool        encrypted;          ///< Connection is encrypted
    bool        authenticated;      ///< Connection is authenticated
    /* Scan result fields */
    char        name[BLE_MAX_DEVICE_NAME + 1];  ///< Advertised device name
    uint8_t     adv_data[31];       ///< Raw advertising data
    uint8_t     adv_data_len;       ///< Advertising data length
};

/* ─── Callback ───────────────────────────────────────────────────────────── */

using BLEEventCb = std::function<void(BLEEvent event, const BLEEventInfo* info)>;

/* ─── Advertising Config ─────────────────────────────────────────────────── */

struct BLEAdvConfig {
    uint16_t    adv_itvl_min    = 0x0020;   ///< Min interval (units of 0.625ms)
    uint16_t    adv_itvl_max    = 0x0040;   ///< Max interval
    bool        connectable     = true;     ///< Allow connections
    int32_t     duration_ms     = 0;        ///< 0 = advertise forever
};

/* ─── Scan Config ────────────────────────────────────────────────────────── */

struct BLEScanConfig {
    int32_t     duration_ms     = BLE_SCAN_DURATION_MS;
    bool        passive         = false;    ///< Passive scan (no scan requests)
    bool        filter_dup      = true;     ///< Filter duplicate advertisements
    uint16_t    itvl            = 0;        ///< Scan interval (0 = default)
    uint16_t    window          = 0;        ///< Scan window (0 = default)
};

/* ─── Main Class ─────────────────────────────────────────────────────────── */

/**
 * @brief BLE Manager - singleton for NimBLE lifecycle and GAP operations.
 * 
 * Handles:
 *   - NimBLE stack initialization and shutdown
 *   - Advertising (peripheral mode)
 *   - Scanning (central mode)
 *   - Connection management
 *   - Event dispatching to user callback
 */
class BLEManager {
public:
    static BLEManager& instance();
    BLEManager(const BLEManager&) = delete;
    BLEManager& operator=(const BLEManager&) = delete;

    /* ─── Lifecycle ────────────────────────────────────────────────────── */

    /**
     * @brief Initialize the NimBLE BLE stack.
     * 
     * @param device_name  Name shown to scanning devices (max 32 chars)
     * @return ESP_OK on success
     * 
     * @note Call this before any other BLE operations.
     * @note GATT services (via BLEServer) must be registered BEFORE begin().
     */
    esp_err_t begin(const char* device_name = "ESP32-BLE");

    /**
     * @brief Shut down the BLE stack.
     * @return ESP_OK on success
     */
    esp_err_t end();

    bool isReady() const;

    /* ─── Advertising (Peripheral) ─────────────────────────────────────── */

    /**
     * @brief Start BLE advertising.
     * 
     * Makes this device visible to scanning centrals (phones, etc.).
     * 
     * @param config  Advertising parameters (defaults are fine for most use)
     * @return ESP_OK on success
     */
    esp_err_t startAdvertising(const BLEAdvConfig& config = BLEAdvConfig{});

    /**
     * @brief Stop advertising.
     */
    esp_err_t stopAdvertising();

    bool isAdvertising() const;

    /* ─── Scanning (Central) ───────────────────────────────────────────── */

    /**
     * @brief Start scanning for BLE peripherals.
     * 
     * Results are reported via SCAN_RESULT events in the callback.
     * 
     * @param config  Scan parameters
     * @return ESP_OK on success
     */
    esp_err_t startScan(const BLEScanConfig& config = BLEScanConfig{});

    /**
     * @brief Stop scanning.
     */
    esp_err_t stopScan();

    bool isScanning() const;

    /* ─── Connection Management ────────────────────────────────────────── */

    /**
     * @brief Connect to a peripheral (central mode).
     * 
     * @param addr       6-byte address of the peripheral
     * @param addr_type  Address type (BLE_ADDR_PUBLIC or BLE_ADDR_RANDOM)
     * @return ESP_OK if connection attempt started
     */
    esp_err_t connect(const uint8_t* addr, uint8_t addr_type = BLE_ADDR_PUBLIC);

    /**
     * @brief Disconnect from a connected device.
     * @param conn_handle  Connection handle (from CONNECTED event)
     * @return ESP_OK on success
     */
    esp_err_t disconnect(uint16_t conn_handle);

    /**
     * @brief Disconnect all connected devices.
     */
    esp_err_t disconnectAll();

    /**
     * @brief Get the number of active connections.
     */
    int getConnectionCount() const;

    /**
     * @brief Check if a specific connection handle is valid/active.
     */
    bool isConnected(uint16_t conn_handle) const;

    /* ─── Callbacks ────────────────────────────────────────────────────── */

    void setEventCallback(BLEEventCb cb);

    /* ─── Utilities ────────────────────────────────────────────────────── */

    /** @brief Get the device name */
    const char* getDeviceName() const;

    /** @brief Get own BLE address */
    esp_err_t getAddress(uint8_t* addr) const;

    /** @brief Format BLE address to string */
    static void addrToStr(const uint8_t* addr, char* buf);

    /* ─── Internal (used by BLEServer/BLEClient) ───────────────────────── */

    /** @brief Access the GAP event handler (called by NimBLE) */
    static int gapEventHandler(struct ble_gap_event* event, void* arg);

private:
    BLEManager();
    ~BLEManager();

    static void nimbleHostTask(void* arg);
    static void onSync();
    static void onReset(int reason);

    void emitEvent(BLEEvent event, const BLEEventInfo* info = nullptr);
    void extractNameFromAdv(const uint8_t* data, uint8_t len, char* name, size_t name_len);

    bool            _initialized;
    bool            _advertising;
    bool            _scanning;
    char            _device_name[BLE_MAX_DEVICE_NAME + 1];
    uint8_t         _own_addr_type;

    SemaphoreHandle_t _mutex;
    BLEEventCb      _event_cb;

    /* Track active connections */
    struct ConnInfo {
        uint16_t handle;
        bool     active;
        uint8_t  addr[6];
    };
    ConnInfo _connections[BLE_MAX_CONNECTIONS];
};

#endif // BLE_MANAGER_H
