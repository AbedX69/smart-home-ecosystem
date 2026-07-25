/*
 * =============================================================================
 * FILE:        core_types.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-07-14
 * MODIFIED:    2026-07-25
 * VERSION:     1.1.0
 * LICENSE:     MIT
 * PLATFORM:    All ESP32 variants (ESP-IDF v5.x)
 * =============================================================================
 *
 * Core Types — Shared vocabulary of the entire ecosystem.
 *
 * Every system component (auto_pair, message_protocol, device_registry,
 * device_identity, scene_engine, ...) speaks in these types. They live here,
 * in a header-only component with ZERO dependencies, so that:
 *
 *   - auto_pair can know what a DeviceRole is without dragging in the
 *     whole device_registry (heartbeats, timers, device tables)
 *   - two components never redefine the same enum
 *   - a new component costs one REQUIRES line, not a dependency chain
 *
 * RULE: nothing in this file may include anything except <cstdint>.
 * If a type needs FreeRTOS, NVS, or a driver — it doesn't belong here.
 *
 * =============================================================================
 * CHANGES IN 1.1.0
 * =============================================================================
 *   1. DeviceUid — permanent, transport-independent device identity.
 *      Replaces MAC as the ecosystem's notion of "who". See device_identity.
 *   2. HouseId / RoomId / NodeId — logical addressing, so a message can be
 *      aimed at one device, one room, or a whole installation.
 *   3. Wildcard + unassigned sentinels for the above.
 *
 * WHY UID AND NOT MAC:
 *   MAC is a property of a radio, not of a device. ESP-NOW has one, LoRa
 *   does not. Putting MAC in the wire format welds the protocol to one
 *   transport and breaks ecosystem rule #3 (swappable transport). The UID
 *   is derived from the factory eFuse MAC, so it is stable across reflash
 *   and full erase, but it means "this device" rather than "this radio".
 *   The MAC lives one layer down, in the transport's address table.
 * =============================================================================
 */

#ifndef CORE_TYPES_H
#define CORE_TYPES_H

#include <cstdint>

/* ─── Identity ───────────────────────────────────────────────────────────── */

#define DEVICE_NAME_LEN         16      ///< Max device name incl. terminator

/**
 * @brief Permanent device identity.
 *
 * Derived once per boot from the factory eFuse MAC (CRC32). Deterministic:
 * the same chip always produces the same UID, with no storage involved, so
 * it survives a full flash erase. See DeviceIdentity::uidFromMac().
 */
using DeviceUid = uint32_t;

#define UID_NONE            0x00000000u ///< Sentinel: address by room+node instead

/* ─── Logical Addressing ─────────────────────────────────────────────────── */
/*
 * house  — which installation. Yours vs the neighbour's. Prevents two
 *          separate systems from ever seeing each other's traffic.
 * room   — which room within the installation.
 * node   — which device within the room.
 *
 * All three are stored in NVS and are changeable at runtime, so a device can
 * be re-assigned over the air without a reflash. NEVER make these compile-time
 * constants: a device on a ceiling must be re-addressable without a ladder.
 */

using HouseId = uint16_t;
using RoomId  = uint8_t;
using NodeId  = uint8_t;

#define HOUSE_UNASSIGNED    0x0000      ///< Not commissioned yet — matches any house
#define ROOM_UNASSIGNED     0x00
#define NODE_UNASSIGNED     0x00

#define ROOM_ALL            0xFF        ///< Wildcard: every room in the house
#define NODE_ALL            0xFF        ///< Wildcard: every node in the room

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
