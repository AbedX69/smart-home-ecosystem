/*
 * =============================================================================
 * FILE:        message_protocol.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-18
 * MODIFIED:    2026-07-27
 * VERSION:     3.0.0
 * LICENSE:     MIT
 * PLATFORM:    All ESP32 variants (ESP-IDF v5.x)
 * =============================================================================
 *
 * Message Protocol v3 — one wire format for the whole ecosystem.
 *
 * =============================================================================
 * CHANGES FROM v2  (WIRE FORMAT IS **NOT** COMPATIBLE — reflash all nodes)
 * =============================================================================
 *   1. MAC IS GONE FROM THE WIRE. src_mac[6]/dst_mac[6] are replaced by
 *      src_uid/dst_uid (4 bytes each, see core_types.h). A MAC is a property
 *      of a radio, not of a device: ESP-NOW has one, LoRa does not. Putting
 *      it in the header welded the protocol to one transport and broke
 *      ecosystem rule #3 (swappable transport).
 *   2. LOGICAL ADDRESSING. The header now carries house + dst_room + dst_node,
 *      so a packet can be aimed at one device, one room, or a whole house.
 *      Two neighbouring installations are mutually invisible.
 *   3. New magic "SMM3" + proto_ver 3. v2 packets are silently dropped.
 *   4. Header fields reordered so every 4-byte value is 4-aligned. v2 had
 *      src_mac starting at offset 10. Same 24-byte header either way.
 *   5. UID→MAC resolution is a single injected function (setUidResolver).
 *      Resolution FAILURE IS NOT AN ERROR — the packet falls back to
 *      broadcast and the far side filters on dst_uid. The resolver is a
 *      bandwidth optimisation, not a correctness requirement, which is
 *      what lets pairing work before any mapping exists.
 *   6. setPeerObservedCallback() reports (uid, mac) on every RX that has a
 *      transport-level MAC, so the address table populates by observation.
 *   7. processMessage() takes the transport's source MAC as a parameter.
 *      Pass nullptr from transports that have no such concept (LoRa).
 *
 * Everything from v2 is retained: reliable send with backoff, RX dedup with
 * cached-ACK re-send, dedicated PAIR_* CmdIds, 24-byte payload.
 *
 * =============================================================================
 * ADDRESSING
 * =============================================================================
 *
 *   dst_uid != UID_NONE   →  unicast to exactly that device.
 *                            dst_room/dst_node are ignored.
 *
 *   dst_uid == UID_NONE   →  group address:
 *                              dst_room = ROOM_ALL  → every room
 *                              dst_node = NODE_ALL  → every node in the room
 *
 *   BROADCAST == UID_NONE + ROOM_ALL + NODE_ALL.
 *
 *   house is checked FIRST and independently. A packet stamped with another
 *   installation's house id is dropped before anything else looks at it.
 *   HOUSE_UNASSIGNED (0) matches everything in both directions, so a
 *   factory-fresh device can still complete a pairing exchange.
 *
 * =============================================================================
 * RELIABILITY MODEL  (unchanged from v2)
 * =============================================================================
 *
 *   sendCommand()          fire-and-forget. One TX, no tracking.
 *   sendCommandReliable()  tracked. Unicast only. Retries until ACK.
 *
 *   Sender                                Receiver
 *   ──────                                ────────
 *   CMD seq=7 ───────X (lost)
 *   CMD seq=7 (retry) ──────────────────→ execute, cache status, ACK seq=7
 *              ←─────────────────X (ACK lost)
 *   CMD seq=7 (retry) ──────────────────→ DUPLICATE: skip execute,
 *              ←── ACK seq=7 (cached) ──  re-send cached ACK
 *   slot cleared, delivery cb (true)
 *
 *   RAPID STREAMS (encoder knobs): send live intermediate values with
 *   sendCommand() (stale values are worthless — never retry them) and send
 *   ONE sendCommandReliable() with the final value when the knob settles.
 *
 * =============================================================================
 * USAGE
 * =============================================================================
 *
 *     DeviceIdentity::instance().begin();      // MUST come first
 *
 *     MessageProtocol& msg = MessageProtocol::instance();
 *     msg.begin();
 *     msg.registerTransport(TRANSPORT_ESPNOW, espnow_broadcast, espnow_unicast);
 *     msg.setUidResolver([](DeviceUid uid, uint8_t mac[6]) {
 *         return AutoPair::instance().resolveUid(uid, mac);
 *     });
 *     msg.setPeerObservedCallback([](DeviceUid uid, const uint8_t mac[6]) {
 *         AutoPair::instance().noteAddress(uid, mac);
 *     });
 *     msg.setCommandHandler(onCommand);
 *
 *     // Hub → strip, guaranteed:
 *     msg.sendLightState(strip_uid, true, 200, 120, 40);
 *     // Hub → strip, live knob stream:
 *     msg.sendLightState(strip_uid, true, b, h, w, false);
 *     // Hub → every light in room 2:
 *     msg.sendCommandToGroup(2, NODE_ALL, CmdId::OFF);
 *
 * NOTE: register all transports BEFORE traffic starts. The transport list
 *       is not re-locked on the hot path.
 *
 * BUILD: this component now REQUIRES device_identity (and therefore
 *        core_types and config_store). Update CMakeLists.txt.
 * =============================================================================
 */

#ifndef MESSAGE_PROTOCOL_H
#define MESSAGE_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "core_types.h"
#include "device_identity.h"

/* ─── Constants ───────────────────────────────────────────────────────────── */

#define MSG_MAGIC               0x534D4D33  /* "SMM3" = SmartMsg v3          */
#define MSG_PROTO_VER           3
#define MSG_PAYLOAD_MAX         24
#define MSG_BROADCAST_MAC       {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

/* Reliable-send engine */
#define MSG_MAX_ATTEMPTS        5           /* total sends incl. the first   */
#define MSG_RETRY_BASE_MS       200         /* doubled after every attempt   */
#define MSG_RETRY_TICK_MS       100         /* retry timer granularity       */
#define MSG_PENDING_SLOTS       8           /* max in-flight reliable sends  */

/* RX dedup */
#define MSG_DEDUP_PEERS         8           /* per-peer tracking entries     */
#define MSG_DEDUP_WINDOW        16          /* seq history depth per peer    */

/* Packet flags */
#define MSG_FLAG_ACK_REQ        0x01        /* sender is retrying until ACK  */

/* ─── Message Types ───────────────────────────────────────────────────────── */

enum class MsgType : uint8_t {
    COMMAND     = 0x01,     ///< Do something (ON, OFF, SET_LIGHT_STATE...)
    QUERY       = 0x02,     ///< Ask for data (GET_STATE...)
    STATE       = 0x03,     ///< Report data (unsolicited or reply to query)
    ACK         = 0x04,     ///< Acknowledge a command (success/fail)
};

/* ─── Command IDs ─────────────────────────────────────────────────────────── */

enum class CmdId : uint8_t {
    /* ── Generic ────────── */
    NONE            = 0x00,
    PING            = 0x01,     ///< Ping (expect ACK)
    REBOOT          = 0x02,     ///< Reboot the device
    FACTORY_RESET   = 0x03,     ///< Erase config and reboot
    IDENTIFY        = 0x04,     ///< Flash LED / beep to locate device

    /* ── On/Off ─────────── */
    ON              = 0x10,
    OFF             = 0x11,
    TOGGLE          = 0x12,

    /* ── Level Control ──── */
    SET_LEVEL       = 0x20,     ///< Payload: [uint8_t level 0-255]
    FADE_TO         = 0x21,     ///< Payload: [uint8_t level, uint16_t ms]
    SET_LIGHT_STATE = 0x25,     ///< Payload: [on, brightness, hue_hi, hue_lo, white]

    /* ── Color ──────────── */
    SET_RGB         = 0x30,     ///< Payload: [r, g, b]
    SET_TEMP_K      = 0x31,     ///< Payload: [uint16_t kelvin]

    /* ── Sensor Queries ─── */
    GET_TEMPERATURE = 0x40,
    GET_HUMIDITY    = 0x41,
    GET_STATE       = 0x42,     ///< Generic "what's your state?"
    GET_BATTERY     = 0x43,

    /* ── State Reports ──── */
    REPORT_TEMP     = 0x50,     ///< Payload: [int16_t temp_x100]
    REPORT_HUMIDITY = 0x51,     ///< Payload: [uint16_t hum_x100]
    REPORT_ON_OFF   = 0x52,     ///< Payload: [uint8_t 0=off 1=on]
    REPORT_LEVEL    = 0x53,     ///< Payload: [uint8_t level]
    REPORT_BATTERY  = 0x54,     ///< Payload: [uint8_t percent]
    REPORT_LIGHT_STATE = 0x55,  ///< Payload: [on, brightness, hue_hi, hue_lo, white]

    /* ── Garage ─────────── */
    GARAGE_OPEN     = 0x60,
    GARAGE_CLOSE    = 0x61,
    GARAGE_STOP     = 0x62,

    /* ── Pairing (auto_pair rides on these) ── */
    PAIR_REQUEST    = 0x70,     ///< Broadcast: [role, fw_major, name...]
    PAIR_ACCEPT     = 0x71,     ///< Unicast controller → device.
                                ///< v3 payload: [house_lo, house_hi, room, node]
    PAIR_REJECT     = 0x72,     ///< Unicast controller → device
    PAIR_UNPAIR     = 0x73,     ///< Live unpair, either direction

    /* ── Commissioning ──── */
    SET_LOCATION    = 0x74,     ///< Re-address a device over the air.
                                ///< Payload: [house_lo, house_hi, room, node]
    SET_CHANNEL     = 0x75,     ///< Move a node to a new WiFi channel.
                                ///< Payload: [channel, delay_lo, delay_hi]
                                ///< The hub adopts the routers channel when
                                ///< it joins WiFi; nodes must follow or go
                                ///< deaf. delay_ms lets every node switch at
                                ///< the same moment rather than as ACKs land.

    /* 0x80–0x8F assigned below; 0x85-0x8F still free */

    /* -- OTA control (ota_bulk rides on these) -- */
    OTA_OFFER       = 0x80,     ///< Hub -> device. Payload: OtaOfferPayload.
                                ///< Its ACK is the accept/reject.
    OTA_PASS_END    = 0x81,     ///< Hub -> device. Payload: OtaPassEndPayload
    OTA_GAP_REPORT  = 0x82,     ///< Device -> hub. Payload: OtaGapReportPayload
    OTA_COMPLETE    = 0x83,     ///< Device -> hub. Payload: OtaCompletePayload
    OTA_ABORT       = 0x84,     ///< Either way.    Payload: OtaAbortPayload

    /* ── Custom ─────────── */
    CUSTOM_0        = 0xF0,
    CUSTOM_1        = 0xF1,
    CUSTOM_2        = 0xF2,
};

/* ─── ACK Status ──────────────────────────────────────────────────────────── */

enum class AckStatus : uint8_t {
    OK              = 0x00,
    FAIL            = 0x01,
    UNKNOWN_CMD     = 0x02,
    BUSY            = 0x03,
    NOT_SUPPORTED   = 0x04,
};

/* ─── Message Packet (48 bytes) ───────────────────────────────────────────────
 *
 *   off  size  field
 *   ───  ────  ─────────────────────────────────────────────────────────────
 *     0     4  magic          "SMM3"
 *     4     4  src_uid        who sent it
 *     8     4  dst_uid        UID_NONE → group address via dst_room/dst_node
 *    12     2  house          installation id; checked before anything else
 *    14     2  seq            random start per boot
 *    16     1  proto_ver      3
 *    17     1  msg_type       MsgType
 *    18     1  cmd_id         CmdId
 *    19     1  flags          MSG_FLAG_*
 *    20     1  dst_room       ROOM_ALL = every room
 *    21     1  dst_node       NODE_ALL = every node in the room
 *    22     1  payload_len    0-24
 *    23     1  status         AckStatus for ACK, 0 otherwise
 *    24    24  payload
 *   ───  ────
 *    48        total
 *
 * Field order is chosen so every multi-byte value sits on its natural
 * alignment despite the packed attribute. Do not reorder casually.
 * ────────────────────────────────────────────────────────────────────────── */

struct __attribute__((packed)) MessagePacket {
    uint32_t    magic;
    DeviceUid   src_uid;
    DeviceUid   dst_uid;
    HouseId     house;
    uint16_t    seq;
    uint8_t     proto_ver;
    uint8_t     msg_type;
    uint8_t     cmd_id;
    uint8_t     flags;
    RoomId      dst_room;
    NodeId      dst_node;
    uint8_t     payload_len;
    uint8_t     status;
    uint8_t     payload[MSG_PAYLOAD_MAX];
};

static_assert(sizeof(MessagePacket) == 48, "MessagePacket must be 48 bytes");
static_assert(offsetof(MessagePacket, src_uid) % 4 == 0, "src_uid misaligned");
static_assert(offsetof(MessagePacket, dst_uid) % 4 == 0, "dst_uid misaligned");
static_assert(offsetof(MessagePacket, payload) == 24,    "payload must be at 24");

/* ─── Light State Payload (5 bytes, shared by hub and strip) ──────────────── */

struct LightStatePayload {
    bool        on;
    uint8_t     brightness;     ///< 0-255
    uint16_t    hue;            ///< 0-359
    uint8_t     white;          ///< 0-255
};

inline void msgEncodeLightState(uint8_t out[5], bool on, uint8_t brightness,
                                 uint16_t hue, uint8_t white) {
    out[0] = on ? 1 : 0;
    out[1] = brightness;
    out[2] = (uint8_t)(hue >> 8);
    out[3] = (uint8_t)(hue & 0xFF);
    out[4] = white;
}

inline bool msgDecodeLightState(const uint8_t* payload, uint8_t len,
                                 LightStatePayload& out) {
    if (!payload || len < 5) return false;
    out.on         = payload[0] != 0;
    out.brightness = payload[1];
    out.hue        = (uint16_t)((payload[2] << 8) | payload[3]);
    out.white      = payload[4];
    if (out.hue > 359) out.hue %= 360;
    return true;
}

/* ─── Location Payload (4 bytes: PAIR_ACCEPT and SET_LOCATION) ────────────── */

inline void msgEncodeLocation(uint8_t out[4], HouseId h, RoomId r, NodeId n) {
    out[0] = (uint8_t)(h & 0xFF);
    out[1] = (uint8_t)(h >> 8);
    out[2] = r;
    out[3] = n;
}

inline bool msgDecodeLocation(const uint8_t* payload, uint8_t len,
                               HouseId& h, RoomId& r, NodeId& n) {
    if (!payload || len < 4) return false;
    h = (HouseId)(payload[0] | ((uint16_t)payload[1] << 8));
    r = payload[2];
    n = payload[3];
    return true;
}

/* ─── Callbacks ───────────────────────────────────────────────────────────── */

/**
 * Called when a command arrives. Return an AckStatus.
 * The protocol layer sends the ACK back automatically (unicast only).
 */
using MsgCommandHandler = std::function<AckStatus(
    CmdId cmd, const uint8_t* payload, uint8_t len,
    DeviceUid src_uid)>;

/** Called when a query arrives. Handler should call sendStateTo() in response. */
using MsgQueryHandler = std::function<void(
    CmdId query, DeviceUid src_uid, uint16_t seq)>;

/** Called when a state report arrives (solicited or unsolicited). */
using MsgStateHandler = std::function<void(
    CmdId state_id, const uint8_t* payload, uint8_t len,
    DeviceUid src_uid)>;

/** Called when an ACK arrives for a command we sent. */
using MsgAckHandler = std::function<void(
    CmdId cmd, AckStatus status, uint16_t seq,
    DeviceUid src_uid)>;

/**
 * Called when a RELIABLE send resolves:
 *   delivered = true  → ACK received, status = remote AckStatus
 *   delivered = false → gave up after MSG_MAX_ATTEMPTS, status = FAIL
 */
using MsgDeliveryCb = std::function<void(
    uint16_t seq, CmdId cmd, bool delivered, AckStatus status,
    DeviceUid dst_uid)>;

/**
 * UID → MAC lookup, supplied by whoever owns the address table
 * (auto_pair today, device_registry v2 later).
 *
 * Return true and fill out_mac if known. Returning false is NOT an error:
 * the packet falls back to broadcast and the far side filters on dst_uid.
 * This is what allows pairing to work before any mapping exists.
 */
using MsgUidResolver = std::function<bool(DeviceUid uid, uint8_t out_mac[6])>;

/**
 * Reports the transport-level source MAC of every inbound packet, paired
 * with the src_uid that was inside it. Lets the address table build itself
 * by observation instead of only at pairing time — and self-heal if a
 * device's radio address ever changes.
 *
 * Not called for transports that pass nullptr to processMessage().
 */
using MsgPeerObservedCb = std::function<void(DeviceUid uid, const uint8_t mac[6])>;

/* ─── Transport Send Functions ────────────────────────────────────────────────
 * These still speak MAC. That is correct and deliberate: the transport layer
 * is exactly where a radio address belongs. Nothing above it sees one.
 * ────────────────────────────────────────────────────────────────────────── */

using MsgTransportSendFn   = std::function<esp_err_t(const uint8_t* data, uint8_t len)>;
using MsgTransportSendToFn = std::function<esp_err_t(
    const uint8_t dst_mac[6], const uint8_t* data, uint8_t len)>;

/* ─── Main Class ──────────────────────────────────────────────────────────── */

class MessageProtocol {
public:
    static MessageProtocol& instance();
    MessageProtocol(const MessageProtocol&) = delete;
    MessageProtocol& operator=(const MessageProtocol&) = delete;

    /* ─── Setup ───────────────────────────────────────────────────────── */

    /**
     * @brief Start the protocol layer.
     *
     * DeviceIdentity::begin() MUST have succeeded first — every outbound
     * header is stamped from it, and every inbound packet is filtered
     * against it. Returns ESP_ERR_INVALID_STATE otherwise.
     */
    esp_err_t begin();
    esp_err_t end();

    esp_err_t registerTransport(uint8_t transport_bit,
                                MsgTransportSendFn broadcast_fn,
                                MsgTransportSendToFn unicast_fn = nullptr);
    esp_err_t setTransportActive(uint8_t transport_bit, bool active);

    /** Install the UID→MAC lookup. Optional; without it everything broadcasts. */
    void setUidResolver(MsgUidResolver fn);

    /** Install the (uid, mac) observation hook. Optional. */
    void setPeerObservedCallback(MsgPeerObservedCb cb);

    /* ─── Sending ─────────────────────────────────────────────────────── */

    /** Fire-and-forget command to one device. */
    esp_err_t sendCommand(DeviceUid dst_uid, CmdId cmd,
                          const uint8_t* payload = nullptr, uint8_t len = 0);

    /** Fire-and-forget command to a room/node group. Never ACKed. */
    esp_err_t sendCommandToGroup(RoomId room, NodeId node, CmdId cmd,
                                 const uint8_t* payload = nullptr, uint8_t len = 0);

    /** Every device in this house. Shorthand for ROOM_ALL / NODE_ALL. */
    esp_err_t broadcastCommand(CmdId cmd,
                               const uint8_t* payload = nullptr, uint8_t len = 0);

    /**
     * Reliable command: retried until ACK or MSG_MAX_ATTEMPTS.
     * Unicast only (UID_NONE returns ESP_ERR_INVALID_ARG).
     * Returns ESP_ERR_NO_MEM if all pending slots are busy.
     */
    esp_err_t sendCommandReliable(DeviceUid dst_uid, CmdId cmd,
                                  const uint8_t* payload = nullptr,
                                  uint8_t len = 0,
                                  uint16_t* out_seq = nullptr);

    esp_err_t sendQuery(DeviceUid dst_uid, CmdId query);

    /** Unsolicited state report to the whole house. */
    esp_err_t sendState(CmdId state_id,
                        const uint8_t* payload = nullptr, uint8_t len = 0);

    /** State report aimed at one device, echoing a query's seq. */
    esp_err_t sendStateTo(DeviceUid dst_uid, CmdId state_id,
                          uint16_t reply_seq,
                          const uint8_t* payload = nullptr, uint8_t len = 0);

    /* ─── Convenience Senders ─────────────────────────────────────────── */

    esp_err_t reportTemperature(float celsius);
    esp_err_t reportOnOff(bool on);
    esp_err_t reportLevel(uint8_t level);

    /** Hub → strip. reliable=false for live knob streams. */
    esp_err_t sendLightState(DeviceUid dst_uid, bool on,
                             uint8_t brightness, uint16_t hue, uint8_t white,
                             bool reliable = true);

    /** Strip → hub (broadcast state report). */
    esp_err_t reportLightState(bool on, uint8_t brightness,
                               uint16_t hue, uint8_t white);

    /** Re-address a device over the air. Reliable. */
    esp_err_t sendLocation(DeviceUid dst_uid, HouseId h, RoomId r, NodeId n);

    /* ─── Receiving ───────────────────────────────────────────────────── */

    /**
     * @brief Feed raw bytes in from any transport RX callback.
     *
     * @param data      the 48-byte packet
     * @param len       must be >= sizeof(MessagePacket)
     * @param src_mac   transport-level source address, or nullptr for
     *                  transports that have no such concept (LoRa).
     *                  Used only to feed the peer-observed callback.
     */
    esp_err_t processMessage(const uint8_t* data, uint8_t len,
                             const uint8_t* src_mac = nullptr);

    /* ─── Handlers ────────────────────────────────────────────────────── */

    void setCommandHandler(MsgCommandHandler handler);
    void setQueryHandler(MsgQueryHandler handler);
    void setStateHandler(MsgStateHandler handler);
    void setAckHandler(MsgAckHandler handler);
    void setDeliveryCallback(MsgDeliveryCb cb);

    /* ─── Debug ───────────────────────────────────────────────────────── */

    static const char* typeName(MsgType type);
    static const char* cmdName(CmdId cmd);
    void logPacket(const char* prefix, const MessagePacket* pkt);

    /* ─── Status ──────────────────────────────────────────────────────── */

    uint8_t        pendingCount();
    DeviceUid      selfUid() const { return _self_uid; }
    const uint8_t* selfMac() const { return _self_mac; }

private:
    MessageProtocol();
    ~MessageProtocol();

    /* One registered transport */
    struct Transport {
        uint8_t              bit;
        bool                 active;
        MsgTransportSendFn   broadcast;
        MsgTransportSendToFn unicast;
    };

    /* One in-flight reliable send */
    struct PendingTx {
        bool          used;
        uint8_t       attempts;         ///< sends performed so far
        int64_t       next_retry_us;
        MessagePacket pkt;
    };

    /* One RX-dedup peer entry */
    struct PeerDedup {
        bool      used;
        DeviceUid uid;
        uint16_t  last_seq;
        uint16_t  window;           ///< bitmask of recently seen seqs
        uint8_t   last_ack_status;  ///< re-sent on duplicate commands
        int64_t   last_seen_us;     ///< for LRU eviction
    };

    /* Helpers */
    uint16_t  nextSeq();
    void      buildHeader(MessagePacket& pkt, MsgType type, CmdId cmd,
                          DeviceUid dst_uid, RoomId dst_room, NodeId dst_node);
    esp_err_t sendPacket(const MessagePacket& pkt);
    esp_err_t sendPacketTo(DeviceUid dst_uid, const MessagePacket& pkt);

    bool      dedupCheck(DeviceUid src_uid, uint16_t seq,
                         uint8_t* cached_status);
    void      storeAckStatus(DeviceUid src_uid, uint8_t status);

    static void retryTimerCb(void* arg);
    void        retryTick();

    void inline lock()   { xSemaphoreTake(_mutex, portMAX_DELAY); }
    void inline unlock() { xSemaphoreGive(_mutex); }

    /* State */
    bool                    _initialized;
    DeviceUid               _self_uid;
    uint8_t                 _self_mac[6];   ///< logging / transport convenience only
    uint16_t                _next_seq;
    SemaphoreHandle_t       _mutex;
    esp_timer_handle_t      _retry_timer;

    std::vector<Transport>  _transports;
    PendingTx               _pending[MSG_PENDING_SLOTS];
    PeerDedup               _peers[MSG_DEDUP_PEERS];

    MsgCommandHandler       _cmd_handler;
    MsgQueryHandler         _query_handler;
    MsgStateHandler         _state_handler;
    MsgAckHandler           _ack_handler;
    MsgDeliveryCb           _delivery_cb;
    MsgUidResolver          _uid_resolver;
    MsgPeerObservedCb       _peer_observed_cb;
};

#endif // MESSAGE_PROTOCOL_H