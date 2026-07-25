/*
 * =============================================================================
 * FILE:        device_identity.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-07-25
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    All ESP32 variants (ESP-IDF v5.x)
 * =============================================================================
 *
 * Device Identity — who this device is, and where it lives.
 *
 * Two separate ideas, deliberately kept apart:
 *
 *   UID          Permanent. Burned into the silicon, effectively. Answers
 *                "which physical device is this?" Never changes, never
 *                configured, identical after a full flash erase.
 *
 *   house/room/  Assigned. Answers "where does this device live?" Stored in
 *   node         NVS, changeable at runtime and therefore over the air.
 *
 * =============================================================================
 * WHY THE UID IS DERIVED, NOT GENERATED
 * =============================================================================
 *
 * The obvious approach is esp_random() on first boot, saved to NVS. Don't.
 * During development a full erase happens constantly, and after each one the
 * device would invent a brand new identity — its own hub would see a stranger
 * and every paired relationship would silently break.
 *
 * Instead: UID = CRC32(factory eFuse MAC). The eFuse MAC is burned at the
 * factory and cannot be erased or rewritten, so the UID is:
 *
 *   - deterministic     same chip, same UID, every boot, forever
 *   - storage-free      nothing to save, nothing to lose
 *   - erase-proof       survives esptool erase_flash
 *   - collision-safe    2^32 space; ~1-in-a-million at 100 devices
 *
 * A 2-byte UID was considered and rejected: 65,536 values gives roughly a
 * 7% collision chance across only 100 devices. 4 bytes is free anyway — the
 * packet header lands on exactly 24 bytes either way.
 *
 * =============================================================================
 * ADDRESSING MODEL
 * =============================================================================
 *
 *   dst_uid != UID_NONE   →  unicast to exactly that device
 *   dst_uid == UID_NONE   →  group address via dst_room / dst_node
 *                            ROOM_ALL  = every room in the house
 *                            NODE_ALL  = every node in the room
 *
 * house_id is checked first and independently: a packet stamped with another
 * installation's house_id is dropped before anything else looks at it. Two
 * neighbouring systems are mutually invisible, which is what stops a strip
 * from being adopted by someone else's hub.
 *
 * The one exception is HOUSE_UNASSIGNED (0). A factory-fresh device has no
 * house yet and must still be able to complete a pairing exchange, so 0
 * matches everything in both directions. Once commissioned, it stops.
 *
 * =============================================================================
 * USAGE
 * =============================================================================
 *
 *     DeviceIdentity& id = DeviceIdentity::instance();
 *     id.begin();
 *
 *     ESP_LOGI(TAG, "I am %s in room %u", id.uidString(), id.room());
 *
 *     // Hub, first boot only: mint a house for this installation
 *     if (!id.isProvisioned()) id.provisionAsNewHouse();
 *
 *     // Later, over the air, from an app or main controller:
 *     id.setLocation(house, room, node);      // persisted immediately
 *
 *     // Inbound filtering (message_protocol does this for you)
 *     if (!id.acceptsHouse(pkt.house_id)) return;
 *     if (!id.isForMe(pkt.dst_uid, pkt.dst_room, pkt.dst_node)) return;
 *
 * =============================================================================
 */

#ifndef DEVICE_IDENTITY_H
#define DEVICE_IDENTITY_H

#include <cstdint>

#include "esp_err.h"

#include "core_types.h"

/* NVS keys. Kept short — NVS caps key length at 15 characters. */
#define IDENTITY_KEY_HOUSE      "house_id"
#define IDENTITY_KEY_ROOM       "room_id"
#define IDENTITY_KEY_NODE       "node_id"

class DeviceIdentity {
public:
    static DeviceIdentity& instance();

    DeviceIdentity(const DeviceIdentity&)            = delete;
    DeviceIdentity& operator=(const DeviceIdentity&) = delete;

    /* ─── Lifecycle ────────────────────────────────────────────────────── */

    /**
     * @brief Derive the UID and load house/room/node from NVS.
     *
     * Opens ConfigStore if it is not already open. Safe to call more than
     * once — subsequent calls are no-ops.
     *
     * @return ESP_OK, or an error from the eFuse read / NVS open.
     */
    esp_err_t begin();

    /** @brief True once begin() has succeeded. */
    bool isReady() const { return _ready; }

    /* ─── Who am I ─────────────────────────────────────────────────────── */

    /** @brief This device's permanent UID. Never UID_NONE. */
    DeviceUid uid() const { return _uid; }

    /** @brief UID as 8 hex chars, e.g. "A3F91C04". Valid after begin(). */
    const char* uidString() const { return _uid_str; }

    /** @brief The factory eFuse MAC the UID was derived from. */
    const uint8_t* factoryMac() const { return _mac; }

    /* ─── Where do I live ──────────────────────────────────────────────── */

    HouseId house() const { return _house; }
    RoomId  room()  const { return _room;  }
    NodeId  node()  const { return _node;  }

    /** @brief True once a house has been assigned (i.e. commissioned). */
    bool isProvisioned() const { return _house != HOUSE_UNASSIGNED; }

    /* ─── Assignment (all persist to NVS immediately) ──────────────────── */

    esp_err_t setHouse(HouseId h);
    esp_err_t setRoom(RoomId r);
    esp_err_t setNode(NodeId n);

    /** @brief Set all three in one commit. Preferred over three calls. */
    esp_err_t setLocation(HouseId h, RoomId r, NodeId n);

    /**
     * @brief Mint a random house id for a brand-new installation.
     *
     * Controller-only, first boot only. Pairing then pushes this value out
     * to every node that joins. Never generates HOUSE_UNASSIGNED.
     */
    esp_err_t provisionAsNewHouse();

    /**
     * @brief Clear house/room/node back to unassigned.
     *
     * Does NOT touch the UID — that is not clearable by design.
     */
    esp_err_t clearLocation();

    /* ─── Inbound filtering ────────────────────────────────────────────── */

    /**
     * @brief Should a packet stamped with this house id be processed?
     *
     * HOUSE_UNASSIGNED on either side matches, so an uncommissioned device
     * can still pair. Otherwise the ids must be equal.
     */
    bool acceptsHouse(HouseId incoming) const;

    /**
     * @brief Is this destination address aimed at us?
     *
     * dst_uid wins when set. Otherwise room/node are matched, honouring
     * the ROOM_ALL / NODE_ALL wildcards.
     */
    bool isForMe(DeviceUid dst_uid, RoomId dst_room, NodeId dst_node) const;

    /* ─── Static helpers ───────────────────────────────────────────────── */

    /**
     * @brief Derive a UID from any MAC.
     *
     * Used to build the transport's uid→MAC table from a paired-device list
     * that still stores MACs. Same function the device runs on itself, so
     * the results agree.
     */
    static DeviceUid uidFromMac(const uint8_t mac[6]);

    /** @brief Format a UID as 8 hex chars into a caller-supplied buffer. */
    static void formatUid(DeviceUid uid, char* out, size_t out_len);

private:
    DeviceIdentity();
    ~DeviceIdentity() = default;

    bool        _ready;
    DeviceUid   _uid;
    char        _uid_str[9];        ///< 8 hex chars + terminator
    uint8_t     _mac[6];

    HouseId     _house;
    RoomId      _room;
    NodeId      _node;
};

#endif // DEVICE_IDENTITY_H
