/*
 * =============================================================================
 * FILE:        device_registry.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-17
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    All ESP32 variants (ESP-IDF v5.x)
 * =============================================================================
 * 
 * Device Registry — Transport-agnostic device discovery & tracking.
 * 
 * Provides:
 *   - Common device announcement packet format
 *   - Periodic heartbeat broadcasting
 *   - Device table with online/offline tracking
 *   - Transport adapters (ESP-NOW, WiFi UDP, LoRa, BLE)
 *   - Callbacks for device join/leave/update events
 *   - Device lookup by MAC, role, or name
 * 
 * =============================================================================
 * BEGINNER'S GUIDE: DEVICE DISCOVERY
 * =============================================================================
 * 
 * HOW IT WORKS:
 * ~~~~~~~~~~~~~
 * Every device on the network periodically broadcasts a small "heartbeat"
 * packet saying "I'm here, this is who I am." Other devices receive these
 * and maintain a table of known devices.
 * 
 *     Device A (Light)          Device B (Controller)
 *     ┌──────────────┐          ┌──────────────┐
 *     │  Every 5s:    │          │               │
 *     │  "I'm a light │─ ─ ─ ─ ─│  Receives,    │
 *     │   at MAC:xx"  │          │  adds to      │
 *     │               │          │  device table  │
 *     │  Also listens │← ─ ─ ─ ─│  "I'm the     │
 *     │  for others   │          │   controller"  │
 *     └──────────────┘          └──────────────┘
 * 
 * If a device stops sending heartbeats, it's marked "offline" after
 * a configurable timeout (default: 30s).
 * 
 * 
 * TRANSPORT AGNOSTIC:
 * ~~~~~~~~~~~~~~~~~~~
 * The announcement packet is the same bytes regardless of how it travels:
 * 
 *     ┌─────────────────┐
 *     │  Announcement    │──→ ESP-NOW broadcast
 *     │  Packet (32 B)   │──→ WiFi UDP broadcast
 *     │                  │──→ LoRa broadcast
 *     │                  │──→ BLE advertisement
 *     └─────────────────┘
 * 
 * You register whichever transports are available on your board.
 * A device with WiFi+ESP-NOW sends on both. A LoRa-only sensor
 * sends on LoRa. The registry doesn't care how the packet arrived.
 * 
 * 
 * DEVICE ROLES:
 * ~~~~~~~~~~~~~
 * Each device declares what it IS, so others know how to interact:
 * 
 *   CONTROLLER   — Main hub, has display, orchestrates
 *   LIGHT        — Controllable light (on/off, dimmer, color)
 *   SENSOR       — Reports readings (temp, humidity, motion)
 *   SWITCH       — Physical switch/button
 *   SOCKET       — Smart outlet (relay + optional power monitor)
 *   GATEWAY      — Protocol bridge (e.g., Zigbee↔WiFi)
 *   CAMERA       — Video/intercom device
 *   AUDIO        — Speaker/microphone device
 *   CUSTOM       — User-defined
 * 
 * =============================================================================
 * USAGE
 * =============================================================================
 * 
 *     DeviceRegistry& reg = DeviceRegistry::instance();
 * 
 *     // Set this device's identity
 *     DeviceIdentity me;
 *     me.role = DeviceRole::LIGHT;
 *     me.setName("Kitchen Light");
 *     me.transports = TRANSPORT_ESPNOW | TRANSPORT_WIFI;
 *     reg.setSelf(me);
 * 
 *     // Get notified when devices appear/disappear
 *     reg.setEventCallback(onDeviceEvent);
 * 
 *     // Start discovery (pass heartbeat interval)
 *     reg.begin(5000);  // heartbeat every 5s
 * 
 *     // Feed incoming packets from your transport layer
 *     // (ESP-NOW rx callback, LoRa rx callback, etc.)
 *     reg.processAnnouncement(raw_data, len);
 * 
 *     // Query the device table
 *     auto* controller = reg.findByRole(DeviceRole::CONTROLLER);
 *     auto* light = reg.findByName("Kitchen Light");
 *     uint8_t count = reg.getOnlineCount();
 * 
 * =============================================================================
 */

#ifndef DEVICE_REGISTRY_H
#define DEVICE_REGISTRY_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include "core_types.h"   /* DeviceRole, DEVICE_NAME_LEN, TRANSPORT_* */

/* ─── Constants ──────────────────────────────────────────────────────────── */

#define DEVICE_MAGIC            0x534D4854  /* "SMHT" = SmartHome */
#define MAX_DEVICES             16
#define DEFAULT_HEARTBEAT_MS    5000
#define DEFAULT_OFFLINE_MS      30000

/* ─── Transport Bitmask ──────────────────────────────────────────────────── */


/* ─── Device Roles ───────────────────────────────────────────────────────── */


/* ─── Device State Flags ─────────────────────────────────────────────────── */

#define STATE_FLAG_ONLINE       0x0001
#define STATE_FLAG_CONFIGURED   0x0002
#define STATE_FLAG_OTA_READY    0x0004
#define STATE_FLAG_LOW_BATTERY  0x0008
#define STATE_FLAG_ERROR        0x0010

/* ─── Announcement Packet (32 bytes, fixed-size) ─────────────────────────── */
/*
 *     Offset  Size  Field
 *     ──────────────────────────
 *       0      4    magic (0x534D4854)
 *       4      6    MAC address
 *      10      1    device role
 *      11      1    transport bitmask
 *      12      2    firmware version (major.minor packed)
 *      14      2    state flags
 *      16     16    device name (null-terminated, padded)
 *     ──────────────────────────
 *      Total: 32 bytes
 */

struct __attribute__((packed)) AnnouncementPacket {
    uint32_t    magic;
    uint8_t     mac[6];
    uint8_t     role;
    uint8_t     transports;
    uint16_t    fw_version;     ///< (major << 8) | minor
    uint16_t    state_flags;
    char        name[DEVICE_NAME_LEN];
};

static_assert(sizeof(AnnouncementPacket) == 32, "Packet must be 32 bytes");

/* ─── Device Identity (this device's info) ───────────────────────────────── */

struct DeviceIdentity {
    DeviceRole  role        = DeviceRole::UNKNOWN;
    uint8_t     transports  = TRANSPORT_NONE;
    uint8_t     fw_major    = 1;
    uint8_t     fw_minor    = 0;
    uint16_t    state_flags = STATE_FLAG_ONLINE;
    char        name[DEVICE_NAME_LEN] = {};

    void setName(const char* n) {
        strncpy(name, n, DEVICE_NAME_LEN - 1);
        name[DEVICE_NAME_LEN - 1] = '\0';
    }
};

/* ─── Device Table Entry ─────────────────────────────────────────────────── */

struct DeviceEntry {
    bool        active;         ///< Slot in use
    bool        online;         ///< Recently seen
    uint8_t     mac[6];
    DeviceRole  role;
    uint8_t     transports;
    uint16_t    fw_version;
    uint16_t    state_flags;
    char        name[DEVICE_NAME_LEN];
    int64_t     last_seen_us;   ///< esp_timer_get_time() of last heartbeat
    uint32_t    heartbeat_count;
};

/* ─── Events ─────────────────────────────────────────────────────────────── */

enum class RegistryEvent {
    DEVICE_JOINED,      ///< New device discovered
    DEVICE_LEFT,        ///< Device went offline (timeout)
    DEVICE_UPDATED,     ///< Known device sent updated info
};

struct RegistryEventInfo {
    const DeviceEntry*  device;
    RegistryEvent       event;
};

using RegistryEventCb = std::function<void(const RegistryEventInfo* info)>;

/* ─── Transport Send Function ────────────────────────────────────────────── */
/*
 * You register a send function for each transport you have.
 * The registry calls it when it's time to broadcast a heartbeat.
 * 
 * Example for ESP-NOW:
 *   reg.registerTransport(TRANSPORT_ESPNOW, [](const uint8_t* data, uint8_t len) {
 *       uint8_t broadcast[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
 *       esp_now_send(broadcast, data, len);
 *       return ESP_OK;
 *   });
 */
using TransportSendFn = std::function<esp_err_t(const uint8_t* data, uint8_t len)>;

/* ─── Main Class ─────────────────────────────────────────────────────────── */

class DeviceRegistry {
public:
    static DeviceRegistry& instance();
    DeviceRegistry(const DeviceRegistry&) = delete;
    DeviceRegistry& operator=(const DeviceRegistry&) = delete;

    /* ─── Setup ────────────────────────────────────────────────────────── */

    /**
     * @brief Set this device's identity (must call before begin).
     */
    void setSelf(const DeviceIdentity& identity);

    /**
     * @brief Register a transport for sending heartbeats.
     * 
     * Multiple transports can be registered. Heartbeats are sent on all.
     * 
     * @param transport_bit  TRANSPORT_ESPNOW, TRANSPORT_WIFI, etc.
     * @param send_fn        Function that broadcasts raw bytes
     */
    void registerTransport(uint8_t transport_bit, TransportSendFn send_fn);

    /**
     * @brief Start heartbeat broadcasting and stale device checking.
     * 
     * @param heartbeat_ms  How often to broadcast (default 5000ms)
     * @param offline_ms    Time before marking device offline (default 30000ms)
     */
    esp_err_t begin(uint32_t heartbeat_ms = DEFAULT_HEARTBEAT_MS,
                    uint32_t offline_ms = DEFAULT_OFFLINE_MS);

    void stop();

    /* ─── Incoming Data ────────────────────────────────────────────────── */

    /**
     * @brief Process a raw announcement packet from any transport.
     * 
     * Call this from your ESP-NOW rx callback, LoRa rx callback,
     * WiFi UDP receive, BLE scan, etc.
     * 
     * @param data  Raw bytes (should be 32 bytes)
     * @param len   Length
     * @return ESP_OK if valid announcement processed
     */
    esp_err_t processAnnouncement(const uint8_t* data, uint8_t len);

    /* ─── Query ────────────────────────────────────────────────────────── */

    /** @brief Find first device matching a role (or nullptr) */
    const DeviceEntry* findByRole(DeviceRole role) const;

    /** @brief Find device by MAC address (or nullptr) */
    const DeviceEntry* findByMAC(const uint8_t mac[6]) const;

    /** @brief Find device by name (substring match, or nullptr) */
    const DeviceEntry* findByName(const char* name) const;

    /** @brief Get all active devices (fills array, returns count) */
    uint8_t getDevices(const DeviceEntry** out, uint8_t max_count) const;

    /** @brief Count of online devices (excluding self) */
    uint8_t getOnlineCount() const;

    /** @brief Total known devices (online + offline) */
    uint8_t getTotalCount() const;

    /** @brief Get this device's MAC */
    const uint8_t* getSelfMAC() const;

    /* ─── Callbacks ────────────────────────────────────────────────────── */

    void setEventCallback(RegistryEventCb cb);

    /* ─── Manual Control ───────────────────────────────────────────────── */

    /** @brief Force send a heartbeat now */
    void sendHeartbeat();

    /** @brief Update own state flags */
    void updateStateFlags(uint16_t flags);

    /** @brief Clear device table */
    void clearDevices();

private:
    DeviceRegistry();
    ~DeviceRegistry();

    /* Timer callbacks */
    static void heartbeatTimerCb(TimerHandle_t timer);
    static void staleCheckTimerCb(TimerHandle_t timer);

    /* Internal */
    DeviceEntry* findSlotByMAC(const uint8_t mac[6]);
    DeviceEntry* findEmptySlot();
    void buildPacket(AnnouncementPacket& pkt);
    void markStaleDevices();

    /* State */
    bool                _running;
    DeviceIdentity      _self;
    uint8_t             _self_mac[6];
    uint32_t            _offline_timeout_us;

    DeviceEntry         _devices[MAX_DEVICES];
    SemaphoreHandle_t   _mutex;

    TimerHandle_t       _heartbeat_timer;
    TimerHandle_t       _stale_timer;

    RegistryEventCb     _event_cb;

    /* Transport send functions (up to 5 transports) */
    struct TransportEntry {
        uint8_t         bit;
        TransportSendFn fn;
        bool            active;
    };
    TransportEntry      _transports[5];
};

#endif // DEVICE_REGISTRY_H
