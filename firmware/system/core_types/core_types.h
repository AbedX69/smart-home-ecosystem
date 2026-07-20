/*
 * =============================================================================
 * FILE:        core_types.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-07-14
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    All ESP32 variants (ESP-IDF v5.x)
 * =============================================================================
 *
 * Core Types — Shared vocabulary of the entire ecosystem.
 *
 * Every system component (auto_pair, message_protocol, device_registry,
 * scene_engine, ...) speaks in these types. They live here, in a
 * header-only component with ZERO dependencies, so that:
 *
 *   - auto_pair can know what a DeviceRole is without dragging in the
 *     whole device_registry (heartbeats, timers, device tables)
 *   - two components never redefine the same enum
 *   - a new component costs one REQUIRES line, not a dependency chain
 *
 * RULE: nothing in this file may include anything except <cstdint>.
 * If a type needs FreeRTOS, NVS, or a driver — it doesn't belong here.
 * =============================================================================
 */

#ifndef CORE_TYPES_H
#define CORE_TYPES_H

#include <cstdint>

/* ─── Identity ───────────────────────────────────────────────────────────── */

#define DEVICE_NAME_LEN         16      ///< Max device name incl. terminator

/* ─── Transport Bitmask ──────────────────────────────────────────────────── */
/* A device advertises which radios it has as an OR of these bits.           */

#define TRANSPORT_NONE      0x00
#define TRANSPORT_ESPNOW    0x01
#define TRANSPORT_WIFI      0x02
#define TRANSPORT_BLE       0x04
#define TRANSPORT_LORA      0x08
#define TRANSPORT_ZIGBEE    0x10

/* ─── Device Roles ───────────────────────────────────────────────────────── */

enum class DeviceRole : uint8_t {
    UNKNOWN     = 0x00,
    CONTROLLER  = 0x01,     ///< Main hub / touch panel
    LIGHT       = 0x02,     ///< On/off, dimmable, color light
    SENSOR      = 0x03,     ///< Temperature, humidity, motion, etc.
    SWITCH      = 0x04,     ///< Physical button / wall switch
    SOCKET      = 0x05,     ///< Smart outlet / relay
    GATEWAY     = 0x06,     ///< Protocol bridge
    CAMERA      = 0x07,     ///< Video / intercom
    AUDIO       = 0x08,     ///< Speaker / microphone
    GARAGE      = 0x09,     ///< Garage door controller
    CUSTOM      = 0xFF
};

/** @brief Human-readable role name (for logs and UI) */
inline const char* deviceRoleName(DeviceRole role) {
    switch (role) {
        case DeviceRole::CONTROLLER: return "Controller";
        case DeviceRole::LIGHT:      return "Light";
        case DeviceRole::SENSOR:     return "Sensor";
        case DeviceRole::SWITCH:     return "Switch";
        case DeviceRole::SOCKET:     return "Socket";
        case DeviceRole::GATEWAY:    return "Gateway";
        case DeviceRole::CAMERA:     return "Camera";
        case DeviceRole::AUDIO:      return "Audio";
        case DeviceRole::GARAGE:     return "Garage";
        case DeviceRole::CUSTOM:     return "Custom";
        default:                     return "Unknown";
    }
}

#endif // CORE_TYPES_H
