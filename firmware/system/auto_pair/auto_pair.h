/*
 * =============================================================================
 * FILE:        auto_pair.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-07-13
 * VERSION:     2.0.0
 * LICENSE:     MIT
 * PLATFORM:    All ESP32 variants (ESP-IDF v5.x)
 * =============================================================================
 *
 * Auto Pair v2 — Zero-config device onboarding.
 *
 * Like opening AirPods near an iPhone. Power on a new device, the
 * controller sees it instantly and offers to pair. No serial monitor,
 * no hardcoded MAC addresses.
 *
 * WHAT'S NEW IN v2 (vs v1):
 *   - Controller PERSISTS its paired-device list to NVS (v1 forgot
 *     everything on reboot — the biggest gap)
 *   - Peer-add/remove hooks so the app can register ESP-NOW peers the
 *     moment pairing completes (v1 never touched the peer list, so
 *     unicast silently fell back to broadcast)
 *   - Re-flashed devices re-pair automatically: a PAIR_REQUEST from an
 *     already-known MAC is silently re-accepted, no popup
 *   - Thread-safe: RX task, UI task, and main loop can all touch it
 *   - Pending requests expire (30 s) so stale popups don't linger
 *   - unpair() works live — no reboot required
 *   - Request rate backs off after 2 min (2 s → 30 s interval) to cut
 *     RF chatter while still searching forever
 *
 * =============================================================================
 * HOW PAIRING WORKS
 * =============================================================================
 *
 * STEP 1: New device powers on (nothing in NVS)
 *
 *     New Light                    Controller (hub)
 *     ┌──────────┐                ┌──────────────┐
 *     │ "I'm new │── PAIR_REQ ──▶│ callback:     │
 *     │  pair me"│  (broadcast,   │ "New device!" │
 *     │          │   every 2 s)   │   [Accept]    │ ◀ user taps / serial
 *     │          │                │   [Reject]    │
 *     └──────────┘                └──────────────┘
 *
 * STEP 2: Controller accepts
 *
 *     New Light                    Controller
 *     ┌──────────┐                ┌──────────────┐
 *     │ saves    │◀─ PAIR_ACCEPT ─│ saves device  │
 *     │ ctrl MAC │   (unicast)    │ MAC to NVS,   │
 *     │ to NVS   │                │ adds ESP-NOW  │
 *     │          │                │ peer          │
 *     └──────────┘                └──────────────┘
 *
 * STEP 3: Both sides reboot-proof. Device unicasts state to controller,
 *         controller unicasts commands to device. Forever.
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
 * Rides on MessageProtocol COMMAND packets. CmdId mapping:
 *
 *     CUSTOM_0 (0xF0) = PAIR_REQUEST   broadcast, payload:
 *                       [role(1), fw_major(1), name(0-8 chars)]
 *     CUSTOM_1 (0xF1) = PAIR_ACCEPT    unicast to device, no payload
 *     CUSTOM_2 (0xF2) = PAIR_REJECT    unicast to device, no payload
 *
 * Name is capped at 8 chars on the wire (MSG_PAYLOAD_MAX = 10).
 * Wrong-device protection: MessageProtocol::processMessage() drops
 * packets whose dst_mac isn't us, so even if an ACCEPT goes out via
 * the broadcast fallback, only the intended device acts on it.
 *
 * =============================================================================
 * USAGE — DEVICE SIDE (e.g. strip node)
 * =============================================================================
 *
 *     AutoPair& pair = AutoPair::instance();
 *
 *     // Callbacks BEFORE begin() — begin() replays peer-adds from NVS
 *     pair.setPeerAddCallback([](const uint8_t mac[6]) {
 *         EspNowManager::instance().addPeer(mac);
 *     });
 *     pair.setPairResultCallback([](bool ok, const uint8_t ctrl[6]) {
 *         ESP_LOGI("APP", "Pairing %s", ok ? "ACCEPTED" : "rejected");
 *     });
 *
 *     pair.begin(DeviceRole::LIGHT, "Strip");
 *
 *     // In your MessageProtocol command handler:
 *     if (AutoPair::handlesCmd(cmd)) {
 *         pair.processPairMessage(cmd, payload, len, src_mac);
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
 *     AutoPair& pair = AutoPair::instance();
 *
 *     pair.setPeerAddCallback([](const uint8_t mac[6]) {
 *         EspNowManager::instance().addPeer(mac);
 *     });
 *     pair.setPairRequestCallback([](const PairRequestInfo* info) {
 *         // Serial-accept phase: log it. UI phase: show GC9A01 popup.
 *         // Then call acceptDevice(info->mac) or rejectDevice(info->mac).
 *     });
 *
 *     pair.beginAsController();   // loads paired list, replays peer-adds
 *     pair.update();              // call periodically (expires stale requests)
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
#include "core_types.h"         /* DeviceRole, DEVICE_NAME_LEN */
#include "message_protocol.h"   /* CmdId, MSG_PAYLOAD_MAX */
#include "config_store.h"       /* NVS persistence */

/* ─── Tunables ───────────────────────────────────────────────────────────── */

#define AUTOPAIR_MAX_PAIRED         8       ///< Max devices a controller stores
#define AUTOPAIR_MAX_PENDING        8       ///< Max simultaneous pair requests
#define AUTOPAIR_REQ_INTERVAL_US    (2000000LL)     ///< 2 s between requests
#define AUTOPAIR_REQ_SLOW_AFTER     60              ///< Requests before backoff
#define AUTOPAIR_REQ_SLOW_US        (30000000LL)    ///< Backed-off interval 30 s
#define AUTOPAIR_REJECT_COOLDOWN_US (10000000LL)    ///< Wait after rejection 10 s
#define AUTOPAIR_PENDING_EXPIRY_US  (30000000LL)    ///< Drop stale requests 30 s

/* ─── Pairing State (device side) ────────────────────────────────────────── */

enum class PairState : uint8_t {
    UNPAIRED,       ///< Fresh boot, nothing in NVS
    REQUESTING,     ///< Broadcasting PAIR_REQUEST periodically
    PAIRED,         ///< Controller known (in RAM and NVS)
    REJECTED,       ///< Rejected — cooling down before retrying
};

/* ─── Pair Request Info (controller callback) ────────────────────────────── */

struct PairRequestInfo {
    uint8_t     mac[6];
    DeviceRole  role;
    char        name[DEVICE_NAME_LEN];
    uint8_t     fw_major;
    int64_t     first_seen_us;      ///< esp_timer time of first request
};

/* ─── Paired Device Record (controller side, persisted) ──────────────────── */

struct PairedDevice {
    uint8_t     mac[6];
    DeviceRole  role;
    char        name[DEVICE_NAME_LEN];
};

/* ─── LED feedback patterns (device side) ────────────────────────────────── */

enum class PairLED : uint8_t {
    FAST_BLINK,     ///< Searching / requesting
    SOLID_ON,       ///< Paired!
    TRIPLE_BLINK,   ///< Rejected
    OFF,            ///< Idle / already paired
};

/* ─── Callbacks ──────────────────────────────────────────────────────────── */

/** Controller: a NEW (unknown) device wants to pair. */
using PairRequestCb = std::function<void(const PairRequestInfo* info)>;

/** Device: pairing finished. On accept, ctrl_mac is the controller. */
using PairResultCb  = std::function<void(bool accepted, const uint8_t ctrl_mac[6])>;

/** Device: LED feedback pattern changed. */
using PairLEDCb     = std::function<void(PairLED pattern)>;

/** Both sides: register `mac` with the transport (→ EspNowManager::addPeer).
 *  Fired on pairing completion AND replayed from NVS during begin(). */
using PeerAddCb     = std::function<void(const uint8_t mac[6])>;

/** Both sides: unregister `mac` (→ EspNowManager::removePeer). Optional. */
using PeerRemoveCb  = std::function<void(const uint8_t mac[6])>;

/* ─── Main Class ─────────────────────────────────────────────────────────── */

class AutoPair {
public:
    static AutoPair& instance();
    AutoPair(const AutoPair&) = delete;
    AutoPair& operator=(const AutoPair&) = delete;

    /* ─── Shared setup ─────────────────────────────────────────────── */
    /* Set callbacks BEFORE begin()/beginAsController(): begin replays  */
    /* peer-add callbacks for everything already stored in NVS.        */

    void setPeerAddCallback(PeerAddCb cb);
    void setPeerRemoveCallback(PeerRemoveCb cb);

    /** true if this CmdId belongs to the pairing protocol */
    static bool handlesCmd(CmdId cmd);

    /**
     * @brief Feed pairing messages in from your MessageProtocol
     *        command handler. Safe to call from the RX task.
     */
    void processPairMessage(CmdId cmd, const uint8_t* payload,
                            uint8_t len, const uint8_t src_mac[6]);

    /**
     * @brief Drive timeouts. Call every ~500 ms from the main loop.
     *        Device: sends requests, ends cooldowns.
     *        Controller: expires stale pending requests.
     */
    void update();

    /* ─── Device side ──────────────────────────────────────────────── */

    /**
     * @brief Start as a device seeking a controller.
     *        If NVS has a pairing → PAIRED immediately (peer-add replayed).
     *        Otherwise → REQUESTING.
     */
    esp_err_t begin(DeviceRole role, const char* name);

    bool            isPaired() const;
    PairState       getState() const;
    const uint8_t*  getControllerMAC() const;   ///< valid only when paired

    /** @brief Forget pairing (erase NVS), resume searching. No reboot. */
    void unpair();

    void setPairResultCallback(PairResultCb cb);
    void setLEDCallback(PairLEDCb cb);

    /* ─── Controller side ──────────────────────────────────────────── */

    /**
     * @brief Start as the controller. Loads the paired-device list
     *        from NVS and replays peer-add for each entry.
     */
    esp_err_t beginAsController();

    esp_err_t acceptDevice(const uint8_t mac[6]);
    esp_err_t rejectDevice(const uint8_t mac[6]);

    void setPairRequestCallback(PairRequestCb cb);

    /* Pending requests (for popup UI / serial listing) */
    uint8_t                 getPendingCount() const;
    const PairRequestInfo*  getPending(uint8_t index) const;

    /* Persisted paired devices */
    uint8_t                 getPairedCount() const;
    const PairedDevice*     getPairedDevice(uint8_t index) const;
    bool                    isDevicePaired(const uint8_t mac[6]) const;

    /** @brief Remove one device (NVS + peer-remove callback). */
    esp_err_t forgetDevice(const uint8_t mac[6]);

    /** @brief Remove all paired devices. */
    void forgetAll();

private:
    AutoPair();
    ~AutoPair();

    /* NVS */
    void loadDevicePairing();
    void saveDevicePairing();
    void eraseDevicePairing();
    void loadPairedList();
    void savePairedList();

    /* TX */
    void sendPairRequest();
    void sendPairResponse(const uint8_t dst_mac[6], bool accept);

    /* RX handlers (split for clarity) */
    void onPairRequest(const uint8_t* payload, uint8_t len,
                       const uint8_t src_mac[6]);
    void onPairAccept(const uint8_t src_mac[6]);
    void onPairReject(const uint8_t src_mac[6]);

    /* Pending list helpers — caller must hold _mutex */
    int  findPendingLocked(const uint8_t mac[6]) const;
    void removePendingLocked(int index);
    int  findPairedLocked(const uint8_t mac[6]) const;

    void lock() const;
    void unlock() const;

    /* Identity */
    bool            _is_controller;
    DeviceRole      _role;
    char            _name[DEVICE_NAME_LEN];
    uint8_t         _self_mac[6];

    /* Device-side state */
    PairState       _state;
    uint8_t         _controller_mac[6];
    int64_t         _last_request_us;
    uint32_t        _request_count;

    /* Controller-side state */
    PairRequestInfo _pending[AUTOPAIR_MAX_PENDING];
    uint8_t         _pending_count;
    PairedDevice    _paired[AUTOPAIR_MAX_PAIRED];
    uint8_t         _paired_count;

    /* Callbacks */
    PairRequestCb   _request_cb;
    PairResultCb    _result_cb;
    PairLEDCb       _led_cb;
    PeerAddCb       _peer_add_cb;
    PeerRemoveCb    _peer_remove_cb;

    mutable SemaphoreHandle_t _mutex;
};

#endif // AUTO_PAIR_H
