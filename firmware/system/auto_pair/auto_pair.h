/*
 * =============================================================================
 * FILE:        auto_pair.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-07-13
 * MODIFIED:    2026-07-27
 * VERSION:     3.0.0
 * LICENSE:     MIT
 * PLATFORM:    All ESP32 variants (ESP-IDF v5.x)
 * =============================================================================
 *
 * Auto Pair v3 — Zero-config device onboarding, and the ecosystem's
 * UID → MAC address table.
 *
 * Like opening AirPods near an iPhone. Power on a new device, the
 * controller sees it instantly and offers to pair.
 *
 * =============================================================================
 * WHAT'S NEW IN v3
 * =============================================================================
 *   1. DEVICES ARE IDENTIFIED BY UID, NOT MAC. Everything public now takes a
 *      DeviceUid. MAC survives in exactly one place — the address table —
 *      because that is what the ESP-NOW transport needs to actually transmit.
 *   2. THIS COMPONENT OWNS THE ADDRESS TABLE. resolveUid() is injected into
 *      MessageProtocol via setUidResolver(); noteAddress() is fed by its
 *      setPeerObservedCallback(). The table is seeded from NVS at begin() so
 *      unicast works on the first packet after a reboot, and refreshed by
 *      observation thereafter, so it self-heals if a radio address changes.
 *
 *      This is the ONLY thing device_registry v2 will have to take over.
 *      Two functions. Nothing else in the ecosystem knows a MAC exists.
 *
 *   3. PAIR_ACCEPT CARRIES LOCATION: [house(2), room(1), node(1)]. The device
 *      calls DeviceIdentity::setLocation() on receipt. Without this a paired
 *      device would sit at HOUSE_UNASSIGNED forever, matching every house in
 *      radio range — which defeats the entire point of the house id.
 *   4. beginAsController() SELF-PROVISIONS. A controller with no house that
 *      accepts pairings would stamp house 0 into its children and adopt (and
 *      be adopted by) anything nearby. It mints a house instead.
 *   5. SET_LOCATION (0x74) is handled here, so re-addressing a device over
 *      the air costs no code in any device's main.cpp.
 *   6. NVS record layout changed → new key ("ap_devs2"). Old pairings are
 *      not migrated; re-pair once after flashing v3.
 *
 * Retained from v2: NVS persistence of the paired list, peer add/remove
 * hooks, silent re-acceptance of re-flashed known devices, thread safety,
 * pending-request expiry, live unpair, request rate backoff.
 *
 * =============================================================================
 * HOW PAIRING WORKS
 * =============================================================================
 *
 * STEP 1: New device powers on (nothing in NVS, house = UNASSIGNED)
 *
 *     New Light                    Controller (hub)
 *     ┌──────────┐                ┌────────────────┐
 *     │ "I'm new │── PAIR_REQ ──▶│ callback:      │
 *     │  pair me"│  (broadcast,   │ "New device!"  │
 *     │          │   every 2 s)   │   [Accept]     │ ◀ user taps / serial
 *     │          │  house=0 →     │   [Reject]     │
 *     │          │  matches any   └────────────────┘
 *     └──────────┘
 *
 * STEP 2: Controller accepts, and COMMISSIONS the device
 *
 *     New Light                    Controller
 *     ┌──────────┐                ┌────────────────┐
 *     │ setsHouse│◀─ PAIR_ACCEPT ─│ saves uid+mac  │
 *     │ /room    │  [house,room,  │ to NVS, adds   │
 *     │ /node,   │   node]        │ ESP-NOW peer,  │
 *     │ saves ctl│  (unicast)     │ picks a node # │
 *     └──────────┘                └────────────────┘
 *
 * STEP 3: Both sides reboot-proof. From here the device belongs to exactly
 *         one house and is invisible to every other installation.
 *
 * PAIRING STATES (device side):
 *
 *     UNPAIRED ─▶ REQUESTING ─▶ PAIRED
 *                     ▲  │
 *                     │  ▼
 *                  REJECTED (10 s cooldown, then retry)
 *
 * =============================================================================
 * WIRE FORMAT
 * =============================================================================
 *
 * Rides on MessageProtocol COMMAND packets:
 *
 *   PAIR_REQUEST (0x70)  broadcast, payload [role(1), fw_major(1), name(0-22)]
 *   PAIR_ACCEPT  (0x71)  unicast,   payload [house_lo, house_hi, room, node]
 *   PAIR_REJECT  (0x72)  unicast,   no payload
 *   PAIR_UNPAIR  (0x73)  unicast,   no payload
 *   SET_LOCATION (0x74)  unicast,   payload [house_lo, house_hi, room, node]
 *
 * The sender's identity is src_uid in the header — it is never in a payload.
 *
 * =============================================================================
 * USAGE — DEVICE SIDE (e.g. strip node)
 * =============================================================================
 *
 *     DeviceIdentity::instance().begin();
 *     MessageProtocol& msg = MessageProtocol::instance();
 *     AutoPair&        pair = AutoPair::instance();
 *
 *     // Wire the address table into the protocol layer, BEFORE msg.begin()
 *     msg.setUidResolver([](DeviceUid u, uint8_t mac[6]) {
 *         return AutoPair::instance().resolveUid(u, mac);
 *     });
 *     msg.setPeerObservedCallback([](DeviceUid u, const uint8_t mac[6]) {
 *         AutoPair::instance().noteAddress(u, mac);
 *     });
 *     msg.begin();
 *
 *     // Callbacks BEFORE begin() — begin() replays peer-adds from NVS
 *     pair.setPeerAddCallback([](const uint8_t mac[6]) {
 *         EspNowManager::instance().addPeer(mac);
 *     });
 *     pair.setPairResultCallback([](bool ok, DeviceUid ctrl) {
 *         ESP_LOGI("APP", "Pairing %s", ok ? "ACCEPTED" : "rejected");
 *     });
 *
 *     pair.begin(DeviceRole::LIGHT, "Strip 1");
 *
 *     // In your MessageProtocol command handler:
 *     if (AutoPair::handlesCmd(cmd)) {
 *         pair.processPairMessage(cmd, payload, len, src_uid);
 *         return AckStatus::OK;
 *     }
 *
 *     // In your main loop, every ~500 ms:
 *     pair.update();
 *
 * =============================================================================
 * USAGE — CONTROLLER SIDE (hub)
 * =============================================================================
 *
 *     pair.setPeerAddCallback(...);          // same as above
 *     pair.setPairRequestCallback([](const PairRequestInfo* info) {
 *         // Serial phase: log it. UI phase: GC9A01 popup.
 *         // Then acceptDevice(info->uid) or rejectDevice(info->uid).
 *     });
 *
 *     pair.beginAsController();   // mints a house if needed, loads NVS
 *     pair.update();              // periodically; expires stale requests
 *
 * =============================================================================
 */

#ifndef AUTO_PAIR_H
#define AUTO_PAIR_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Ecosystem types */
#include "core_types.h"         /* DeviceRole, DeviceUid, DEVICE_NAME_LEN */
#include "device_identity.h"    /* house/room/node assignment            */
#include "message_protocol.h"   /* CmdId, MSG_PAYLOAD_MAX                */
#include "config_store.h"       /* NVS persistence                       */

/* ─── Tunables ────────────────────────────────────────────────────────────── */

#define AUTOPAIR_MAX_PAIRED         8       ///< Max devices a controller stores
#define AUTOPAIR_MAX_PENDING        8       ///< Max simultaneous pair requests
#define AUTOPAIR_ADDR_ENTRIES       12      ///< UID→MAC table size (LRU)
#define AUTOPAIR_REQ_INTERVAL_US    (2000000LL)     ///< 2 s between requests
#define AUTOPAIR_REQ_SLOW_AFTER     60              ///< Requests before backoff
#define AUTOPAIR_REQ_SLOW_US        (30000000LL)    ///< Backed-off interval 30 s
#define AUTOPAIR_REJECT_COOLDOWN_US (10000000LL)    ///< Wait after rejection 10 s
#define AUTOPAIR_PENDING_EXPIRY_US  (30000000LL)    ///< Drop stale requests 30 s

/* First node number handed out to a paired device. The controller itself
 * takes node 1, so children start at 2. */
#define AUTOPAIR_FIRST_NODE         2

/* ─── Pairing State (device side) ─────────────────────────────────────────── */

enum class PairState : uint8_t {
    UNPAIRED,       ///< Fresh boot, nothing in NVS
    REQUESTING,     ///< Broadcasting PAIR_REQUEST periodically
    PAIRED,         ///< Controller known (in RAM and NVS)
    REJECTED,       ///< Rejected — cooling down before retrying
};

/* ─── Pair Request Info (controller callback) ─────────────────────────────── */

struct PairRequestInfo {
    DeviceUid   uid;
    DeviceRole  role;
    char        name[DEVICE_NAME_LEN];
    uint8_t     fw_major;
    int64_t     first_seen_us;      ///< esp_timer time of first request
};

/* ─── Paired Device Record (controller side, persisted) ───────────────────── */

struct PairedDevice {
    DeviceUid   uid;
    uint8_t     mac[6];             ///< transport hint; may go stale, self-heals
    DeviceRole  role;
    RoomId      room;               ///< location we assigned it
    NodeId      node;
    char        name[DEVICE_NAME_LEN];
};

/* ─── LED feedback patterns (device side) ─────────────────────────────────── */

enum class PairLED : uint8_t {
    FAST_BLINK,     ///< Searching / requesting
    SOLID_ON,       ///< Paired!
    TRIPLE_BLINK,   ///< Rejected
    OFF,            ///< Idle / already paired
};

/* ─── Callbacks ───────────────────────────────────────────────────────────── */

/** Controller: a NEW (unknown) device wants to pair. */
using PairRequestCb = std::function<void(const PairRequestInfo* info)>;

/** Device: pairing finished. On accept, ctrl_uid is the controller. */
using PairResultCb  = std::function<void(bool accepted, DeviceUid ctrl_uid)>;

/** Device: LED feedback pattern changed. */
using PairLEDCb     = std::function<void(PairLED pattern)>;

/** Both sides: register `mac` with the transport (→ EspNowManager::addPeer).
 *  Still MAC-based on purpose — this is the transport boundary.
 *  Fired on pairing completion AND replayed from NVS during begin(). */
using PeerAddCb     = std::function<void(const uint8_t mac[6])>;

/** Both sides: unregister `mac` (→ EspNowManager::removePeer). Optional. */
using PeerRemoveCb  = std::function<void(const uint8_t mac[6])>;

/* ─── Main Class ──────────────────────────────────────────────────────────── */

class AutoPair {
public:
    static AutoPair& instance();
    AutoPair(const AutoPair&) = delete;
    AutoPair& operator=(const AutoPair&) = delete;

    /* ─── Address table (the device_registry v2 seam) ─────────────────────
     *
     * These two functions are the entire interface between "who a device is"
     * and "where to transmit". When device_registry v2 arrives, it implements
     * these and MessageProtocol is re-pointed at it. Nothing else changes.
     * ─────────────────────────────────────────────────────────────────── */

    /** @brief UID → MAC. Returns false if unknown (caller broadcasts instead). */
    bool resolveUid(DeviceUid uid, uint8_t out_mac[6]) const;

    /** @brief Record/refresh a UID's radio address. Feed from every RX. */
    void noteAddress(DeviceUid uid, const uint8_t mac[6]);

    /* ─── Shared setup ────────────────────────────────────────────────────
     * Set callbacks BEFORE begin()/beginAsController(): begin replays
     * peer-add callbacks for everything already stored in NVS.
     * ─────────────────────────────────────────────────────────────────── */

    void setPeerAddCallback(PeerAddCb cb);
    void setPeerRemoveCallback(PeerRemoveCb cb);

    /** true if this CmdId belongs to the pairing/commissioning protocol */
    static bool handlesCmd(CmdId cmd);

    /**
     * @brief Feed pairing messages in from your MessageProtocol
     *        command handler. Safe to call from the RX task.
     */
    void processPairMessage(CmdId cmd, const uint8_t* payload,
                            uint8_t len, DeviceUid src_uid);

    /**
     * @brief Drive timeouts. Call every ~500 ms from the main loop.
     *        Device: sends requests, ends cooldowns.
     *        Controller: expires stale pending requests.
     */
    void update();

    /* ─── Device side ─────────────────────────────────────────────────── */

    /**
     * @brief Start as a device seeking a controller.
     *        If NVS has a pairing → PAIRED immediately (peer-add replayed).
     *        Otherwise → REQUESTING.
     */
    esp_err_t begin(DeviceRole role, const char* name);

    bool            isPaired() const;
    PairState       getState() const;
    DeviceUid       getControllerUid() const;   ///< UID_NONE when unpaired
    const uint8_t*  getControllerMAC() const;   ///< valid only when paired

    /**
     * @brief Forget pairing (erase NVS), resume searching. No reboot.
     * @param clear_location  also reset house/room/node to unassigned.
     *                        Default true — a device that has left its
     *                        house should not keep answering to it.
     */
    void unpair(bool clear_location = true);

    void setPairResultCallback(PairResultCb cb);
    void setLEDCallback(PairLEDCb cb);

    /* ─── Controller side ─────────────────────────────────────────────── */

    /**
     * @brief Start as the controller. Mints a house id if this device has
     *        none, then loads the paired list from NVS and replays peer-add.
     */
    esp_err_t beginAsController();

    esp_err_t acceptDevice(DeviceUid uid);
    esp_err_t rejectDevice(DeviceUid uid);

    void setPairRequestCallback(PairRequestCb cb);

    /* Pending requests (for popup UI / serial listing) */
    uint8_t                 getPendingCount() const;
    const PairRequestInfo*  getPending(uint8_t index) const;

    /* Persisted paired devices */
    uint8_t                 getPairedCount() const;
    const PairedDevice*     getPairedDevice(uint8_t index) const;
    bool                    isDevicePaired(DeviceUid uid) const;

    /** @brief Remove one device (NVS + peer-remove callback). */
    esp_err_t forgetDevice(DeviceUid uid);

    /** @brief Remove all paired devices. */
    void forgetAll();

private:
    AutoPair();
    ~AutoPair();

    /* One UID→MAC mapping */
    struct AddressEntry {
        bool      used;
        DeviceUid uid;
        uint8_t   mac[6];
        int64_t   last_seen_us;     ///< LRU eviction
    };

    /* NVS */
    void loadDevicePairing();
    void saveDevicePairing();
    void eraseDevicePairing();
    void loadPairedList();
    void savePairedList();

    /* TX */
    void sendPairRequest();
    void sendPairAccept(DeviceUid uid, RoomId room, NodeId node);
    void sendPairReject(DeviceUid uid);

    /* RX handlers (split for clarity) */
    void onPairRequest(const uint8_t* payload, uint8_t len, DeviceUid src_uid);
    void onPairAccept(const uint8_t* payload, uint8_t len, DeviceUid src_uid);
    void onPairReject(DeviceUid src_uid);
    void onSetLocation(const uint8_t* payload, uint8_t len, DeviceUid src_uid);

    /* Locked helpers — caller must hold _mutex */
    int    findPendingLocked(DeviceUid uid) const;
    void   removePendingLocked(int index);
    int    findPairedLocked(DeviceUid uid) const;
    int    findAddrLocked(DeviceUid uid) const;
    void   noteAddressLocked(DeviceUid uid, const uint8_t mac[6]);
    NodeId allocateNodeLocked(RoomId room) const;

    void lock() const;
    void unlock() const;

    /* Identity */
    bool            _is_controller;
    DeviceRole      _role;
    char            _name[DEVICE_NAME_LEN];
    DeviceUid       _self_uid;

    /* Device-side state */
    PairState       _state;
    DeviceUid       _controller_uid;
    uint8_t         _controller_mac[6];
    int64_t         _last_request_us;
    uint32_t        _request_count;

    /* Controller-side state */
    PairRequestInfo _pending[AUTOPAIR_MAX_PENDING];
    uint8_t         _pending_count;
    PairedDevice    _paired[AUTOPAIR_MAX_PAIRED];
    uint8_t         _paired_count;

    /* Address table (both roles) */
    AddressEntry    _addr[AUTOPAIR_ADDR_ENTRIES];

    /* Callbacks */
    PairRequestCb   _request_cb;
    PairResultCb    _result_cb;
    PairLEDCb       _led_cb;
    PeerAddCb       _peer_add_cb;
    PeerRemoveCb    _peer_remove_cb;

    mutable SemaphoreHandle_t _mutex;
};

#endif // AUTO_PAIR_H