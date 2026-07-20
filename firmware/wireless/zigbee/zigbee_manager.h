/*
 * =============================================================================
 * FILE:        zigbee_manager.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-17
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    ESP32-C6 / ESP32-H2 (ESP-IDF v5.x + ESP Zigbee SDK)
 * =============================================================================
 * 
 * Zigbee Manager - C++ wrapper for ESP Zigbee SDK.
 * 
 * Provides:
 *   - Coordinator / Router / End Device roles
 *   - HA On/Off Light device
 *   - HA Dimmable Light (level control)
 *   - HA Temperature Sensor (periodic reporting)
 *   - Event callbacks for network and attribute changes
 *   - Factory reset helper
 * 
 * =============================================================================
 * BEGINNER'S GUIDE: ZIGBEE
 * =============================================================================
 * 
 * WHAT IS ZIGBEE?
 * ~~~~~~~~~~~~~~~
 * Zigbee is a mesh networking protocol on the 802.15.4 radio (2.4 GHz).
 * Unlike WiFi/BLE, Zigbee devices form a self-healing mesh — each device
 * can relay messages for others, extending range.
 * 
 *     ┌──────────────────────────────────────────────────┐
 *     │              │  Zigbee      │  BLE Mesh    │     │
 *     ├──────────────────────────────────────────────────┤
 *     │  Range/hop   │  10-100m     │  10-50m      │     │
 *     │  Max hops    │  ~30         │  ~127        │     │
 *     │  Max nodes   │  ~65000      │  ~32000      │     │
 *     │  Power       │  Very low    │  Very low    │     │
 *     │  Ecosystem   │  Huge (Hue,  │  Growing     │     │
 *     │              │  IKEA, etc.) │              │     │
 *     └──────────────────────────────────────────────────┘
 * 
 * 
 * DEVICE ROLES:
 * ~~~~~~~~~~~~~
 * 
 *     Coordinator (ZC)         Router (ZR)          End Device (ZED)
 *     ┌─────────────┐         ┌──────────┐         ┌──────────┐
 *     │  Forms the   │─────── │  Relays    │─────── │  Leaf     │
 *     │  network.    │        │  messages. │        │  node.    │
 *     │  Only ONE    │        │  Always    │        │  Can      │
 *     │  per network.│        │  awake.    │        │  sleep.   │
 *     └─────────────┘         └──────────┘         └──────────┘
 * 
 *   Coordinator: Creates the network. Hub/gateway role.
 *   Router: Joins the network, relays for others. Smart plugs, lights.
 *   End Device: Joins, doesn't relay. Sensors, remotes (battery).
 * 
 * 
 * DATA MODEL (simplified):
 * ~~~~~~~~~~~~~~~~~~~~~~~~
 * 
 *     Device
 *       └── Endpoint 1 (e.g., On/Off Light)
 *             ├── Cluster: On/Off (0x0006)
 *             │     └── Attribute: on_off (bool)
 *             ├── Cluster: Level Control (0x0008)
 *             │     └── Attribute: current_level (uint8)
 *             └── Cluster: Basic (0x0000)
 *                   ├── Attribute: manufacturer_name
 *                   └── Attribute: model_identifier
 * 
 * 
 * HARDWARE REQUIREMENT:
 * ~~~~~~~~~~~~~~~~~~~~~
 * Zigbee requires 802.15.4 radio. Only these ESP32 variants have it:
 *   - ESP32-C6 ✓   (your boards)
 *   - ESP32-H2 ✓
 *   - ESP32-C5 ✓
 *   - ESP32-D  ✗   (no 802.15.4)
 *   - ESP32-S3 ✗   (no 802.15.4)
 * 
 * 
 * ESP ZIGBEE SDK DEPENDENCY:
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Unlike other components, Zigbee requires external libraries from the
 * ESP Component Registry. These are declared in idf_component.yml and
 * downloaded automatically at build time:
 *   - espressif/esp-zboss-lib
 *   - espressif/esp-zigbee-lib
 * 
 * 
 * =============================================================================
 * USAGE EXAMPLES
 * =============================================================================
 * 
 * ON/OFF LIGHT (End Device):
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~
 *     ZigbeeManager& zb = ZigbeeManager::instance();
 *     zb.setOnOffCallback(myLedControl);
 *     zb.begin(ZBRole::END_DEVICE, ZBDeviceType::ON_OFF_LIGHT);
 *     // → Light appears in Zigbee2MQTT / ZHA
 * 
 * DIMMABLE LIGHT (Router):
 * ~~~~~~~~~~~~~~~~~~~~~~~~
 *     zb.setOnOffCallback(myOnOff);
 *     zb.setLevelCallback(myDimmer);
 *     zb.begin(ZBRole::ROUTER, ZBDeviceType::DIMMABLE_LIGHT);
 * 
 * TEMPERATURE SENSOR (End Device):
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *     zb.begin(ZBRole::END_DEVICE, ZBDeviceType::TEMPERATURE_SENSOR);
 *     // Periodically:
 *     zb.reportTemperature(23.5f);
 * 
 * COORDINATOR (forms network):
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *     zb.setNetworkCallback(onNetworkEvent);
 *     zb.begin(ZBRole::COORDINATOR, ZBDeviceType::ON_OFF_LIGHT);
 *     // Network forms, other devices can join
 * 
 * =============================================================================
 */

#ifndef ZIGBEE_MANAGER_H
#define ZIGBEE_MANAGER_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ESP Zigbee SDK headers (provided by esp-zigbee-lib component) */
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"

/* ─── Constants ──────────────────────────────────────────────────────────── */
#define ZB_DEFAULT_ENDPOINT     10
#define ZB_MAX_MANUFACTURER_LEN 32
#define ZB_MAX_MODEL_LEN        32

/* ─── Enums ──────────────────────────────────────────────────────────────── */

enum class ZBRole {
    COORDINATOR,    ///< Forms network (one per network)
    ROUTER,         ///< Joins + relays (always powered)
    END_DEVICE      ///< Joins, leaf node (can sleep)
};

enum class ZBDeviceType {
    ON_OFF_LIGHT,       ///< Simple on/off light
    DIMMABLE_LIGHT,     ///< On/off + level control (0-255)
    TEMPERATURE_SENSOR  ///< Reports temperature
};

/* ─── Events ─────────────────────────────────────────────────────────────── */

enum class ZBEvent {
    STACK_READY,        ///< Zigbee stack initialized
    NETWORK_FORMED,     ///< Coordinator formed a network
    NETWORK_JOINED,     ///< Joined an existing network
    NETWORK_STEERING,   ///< Network steering in progress
    NETWORK_LEFT,       ///< Left the network
    NETWORK_FAILED,     ///< Failed to join/form
    DEVICE_JOINED,      ///< A new device joined (coordinator only)
    PERMIT_JOIN,        ///< Network open/closed for joining
};

struct ZBEventInfo {
    uint16_t    pan_id;
    uint8_t     channel;
    uint16_t    short_addr;
    uint8_t     ext_pan_id[8];
    bool        permit_join_open;
    uint8_t     permit_duration;
};

/* ─── Callbacks ──────────────────────────────────────────────────────────── */

/** Called when On/Off attribute changes (light on/off) */
using ZBOnOffCb = std::function<void(bool on)>;

/** Called when Level attribute changes (dimmer 0-255) */
using ZBLevelCb = std::function<void(uint8_t level)>;

/** Called on network events */
using ZBNetworkCb = std::function<void(ZBEvent event, const ZBEventInfo* info)>;

/* ─── Configuration ──────────────────────────────────────────────────────── */

struct ZBConfig {
    uint8_t     endpoint        = ZB_DEFAULT_ENDPOINT;
    uint32_t    channel_mask    = ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK;
    bool        erase_nvram     = false;    ///< Factory reset on start
    char        manufacturer[ZB_MAX_MANUFACTURER_LEN] = "SmartHome";
    char        model[ZB_MAX_MODEL_LEN] = "ESP32-C6";
};

/* ─── Main Class ─────────────────────────────────────────────────────────── */

class ZigbeeManager {
public:
    static ZigbeeManager& instance();
    ZigbeeManager(const ZigbeeManager&) = delete;
    ZigbeeManager& operator=(const ZigbeeManager&) = delete;

    /* ─── Lifecycle ────────────────────────────────────────────────────── */

    /**
     * @brief Initialize and start the Zigbee stack.
     * 
     * Creates the Zigbee task, initializes the stack, creates the
     * endpoint/cluster hierarchy, and begins commissioning.
     * 
     * @param role        Coordinator, Router, or End Device
     * @param device_type Which HA device profile to create
     * @param config      Optional configuration overrides
     * @return ESP_OK on success
     */
    esp_err_t begin(ZBRole role, ZBDeviceType device_type,
                    const ZBConfig& config = ZBConfig{});

    /**
     * @brief Factory reset: erase all Zigbee NVRAM and reboot.
     */
    void factoryReset();

    /**
     * @brief Open network for joining (coordinator/router only).
     * @param duration_s  Seconds to allow joining (0 = close, 255 = permanent)
     */
    esp_err_t permitJoin(uint8_t duration_s = 180);

    bool isJoined() const;

    /* ─── Temperature Sensor ───────────────────────────────────────────── */

    /**
     * @brief Report a temperature reading.
     * 
     * Updates the temperature measurement attribute and sends
     * a ZCL report to bound devices.
     * 
     * @param temp_celsius  Temperature in degrees C (e.g., 23.5)
     */
    esp_err_t reportTemperature(float temp_celsius);

    /* ─── Callbacks ────────────────────────────────────────────────────── */

    void setOnOffCallback(ZBOnOffCb cb);
    void setLevelCallback(ZBLevelCb cb);
    void setNetworkCallback(ZBNetworkCb cb);

    /* ─── Getters ──────────────────────────────────────────────────────── */

    uint16_t getShortAddr() const;
    uint8_t  getChannel() const;
    uint16_t getPanId() const;

private:
    ZigbeeManager();
    ~ZigbeeManager();

    /* ─── Static callbacks (bridge to C API) ───────────────────────────── */
    static void zbTask(void* pvParameters);
    static void appSignalHandler(esp_zb_app_signal_t* signal);
    static esp_err_t actionHandler(esp_zb_core_action_callback_id_t callback_id,
                                    const void* message);

    /* Attribute handling */
    static esp_err_t handleSetAttrValue(const esp_zb_zcl_set_attr_value_message_t* msg);

    /* Endpoint creation helpers */
    void createOnOffLightEndpoint();
    void createDimmableLightEndpoint();
    void createTemperatureSensorEndpoint();

    /* Commission retry */
    static void bdbCommissionCb(uint8_t mode_mask);

    /* State */
    bool            _initialized;
    ZBRole          _role;
    ZBDeviceType    _device_type;
    ZBConfig        _config;
    bool            _joined;

    ZBOnOffCb       _onoff_cb;
    ZBLevelCb       _level_cb;
    ZBNetworkCb     _network_cb;
};

#endif // ZIGBEE_MANAGER_H
