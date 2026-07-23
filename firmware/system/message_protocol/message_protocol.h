/*
 * =============================================================================
 * FILE:        message_protocol.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-18
 * MODIFIED:    2026-07-21
 * VERSION:     2.0.0
 * LICENSE:     MIT
 * PLATFORM:    All ESP32 variants (ESP-IDF v5.x)
 * =============================================================================
 *
 * Message Protocol v2 — one wire format for the whole ecosystem.
 *
 * =============================================================================
 * CHANGES FROM v1.x  (WIRE FORMAT IS **NOT** COMPATIBLE — reflash all nodes)
 * =============================================================================
 *   1. Payload 10 → 24 bytes. Packet grows 32 → 48 bytes.
 *   2. New magic "SMM2" + proto_ver byte. v1 packets are silently dropped,
 *      v2 packets are invisible to v1 nodes. Clean cutover.
 *   3. RELIABLE DELIVERY: sendCommandReliable() keeps retrying with
 *      exponential backoff (200/400/800/1600/3200 ms) until an ACK arrives
 *      or MSG_MAX_ATTEMPTS is reached. Result via setDeliveryCallback().
 *   4. RX DEDUPLICATION: a retried command is executed exactly ONCE.
 *      If the original ACK was the packet that got lost, the receiver
 *      re-sends the cached ACK instead of re-running the command.
 *   5. Dedicated pairing CmdIds (PAIR_REQUEST/ACCEPT/REJECT/UNPAIR,
 *      0x70–0x73). CUSTOM_0..2 are free for experiments again.
 *   6. Broadcast COMMANDs are no longer auto-ACKed (unicast still is).
 *      Prevents N-device ACK collisions on every broadcast.
 *   7. Combined light state (SET_LIGHT_STATE / REPORT_LIGHT_STATE) with
 *      the POC's 5-byte layout: [on, brightness, hue_hi, hue_lo, white].
 *   8. buildHeader() zeroes the whole packet (v1.x memset UB fix kept).
 *
 * =============================================================================
 * RELIABILITY MODEL
 * =============================================================================
 *
 *   sendCommand()          fire-and-forget. One TX, no tracking.
 *   sendCommandReliable()  tracked. Unicast only. Retries until ACK.
 *
 *   Sender                                Receiver
 *   ──────                                ────────
 *   CMD seq=7 ───────X (lost)
 *   CMD seq=7 (retry) ──────────────────→ execute, cache status, ACK seq=7
 *              ←────────────────X (ACK lost)
 *   CMD seq=7 (retry) ──────────────────→ DUPLICATE: skip execute,
 *              ←── ACK seq=7 (cached) ──  re-send cached ACK
 *   slot cleared, delivery cb (true)
 *
 *   RAPID STREAMS (encoder knobs): send the live intermediate values with
 *   sendCommand() (stale values are worthless — never retry them) and send
 *   ONE sendCommandReliable() with the final value when the knob settles.
 *   This also keeps the 8 pending slots free.
 *
 * =============================================================================
 * USAGE
 * =============================================================================
 *
 *     MessageProtocol& msg = MessageProtocol::instance();
 *     msg.begin();
 *     msg.registerTransport(TRANSPORT_ESPNOW, espnow_broadcast, espnow_unicast);
 *     msg.setCommandHandler(onCommand);
 *     msg.setDeliveryCallback(onDelivery);       // optional
 *
 *     // Hub → strip, guaranteed:
 *     msg.sendLightState(strip_mac, true, 200, 120, 40);          // reliable
 *     // Hub → strip, live knob stream:
 *     msg.sendLightState(strip_mac, true, b, h, w, false);        // best-effort
 *
 * NOTE: register all transports BEFORE traffic starts. The transport list
 *       is not re-locked on the hot path.
 * =============================================================================
 */

#ifndef MESSAGE_PROTOCOL_H
#define MESSAGE_PROTOCOL_H

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

/* ─── Constants ──────────────────────────────────────────────────────────── */

#define MSG_MAGIC               0x534D4D32  /* "SMM2" = SmartMsg v2          */
#define MSG_PROTO_VER           2
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

/* ─── Message Types ──────────────────────────────────────────────────────── */

enum class MsgType : uint8_t {
    COMMAND     = 0x01,     ///< Do something (ON, OFF, SET_LIGHT_STATE...)
    QUERY       = 0x02,     ///< Ask for data (GET_STATE...)
    STATE       = 0x03,     ///< Report data (unsolicited or reply to query)
    ACK         = 0x04,     ///< Acknowledge a command (success/fail)
};

/* ─── Command IDs ────────────────────────────────────────────────────────── */

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
    PAIR_ACCEPT     = 0x71,     ///< Unicast controller → device
    PAIR_REJECT     = 0x72,     ///< Unicast controller → device
    PAIR_UNPAIR     = 0x73,     ///< Live unpair, either direction

    /* 0x80–0x8F reserved for OTA control (step 3) */

    /* ── Custom ─────────── */
    CUSTOM_0        = 0xF0,
    CUSTOM_1        = 0xF1,
    CUSTOM_2        = 0xF2,
};

/* ─── ACK Status ─────────────────────────────────────────────────────────── */

enum class AckStatus : uint8_t {
    OK              = 0x00,
    FAIL            = 0x01,
    UNKNOWN_CMD     = 0x02,
    BUSY            = 0x03,
    NOT_SUPPORTED   = 0x04,
};

/* ─── Message Packet (48 bytes) ──────────────────────────────────────────── */

struct __attribute__((packed)) MessagePacket {
    uint32_t    magic;          ///< MSG_MAGIC ("SMM2")
    uint8_t     proto_ver;      ///< MSG_PROTO_VER (2)
    uint8_t     msg_type;       ///< MsgType
    uint8_t     cmd_id;         ///< CmdId
    uint8_t     flags;          ///< MSG_FLAG_*
    uint16_t    seq;            ///< Sequence number (random start per boot)
    uint8_t     src_mac[6];
    uint8_t     dst_mac[6];
    uint8_t     payload_len;    ///< 0-24
    uint8_t     status;         ///< AckStatus for ACK, 0 otherwise
    uint8_t     payload[MSG_PAYLOAD_MAX];
};

static_assert(sizeof(MessagePacket) == 48, "MessagePacket must be 48 bytes");

/* ─── Light State Payload (5 bytes, shared by hub and strip) ─────────────── */

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

/* ─── Callbacks ──────────────────────────────────────────────────────────── */

/**
 * Called when a command arrives. Return an AckStatus.
 * The protocol layer sends the ACK back automatically (unicast only).
 */
using MsgCommandHandler = std::function<AckStatus(
    CmdId cmd, const uint8_t* payload, uint8_t len,
    const uint8_t src_mac[6])>;

/** Called when a query arrives. Handler should call sendStateTo() in response. */
using MsgQueryHandler = std::function<void(
    CmdId query, const uint8_t src_mac[6], uint16_t seq)>;

/** Called when a state report arrives (solicited or unsolicited). */
using MsgStateHandler = std::function<void(
    CmdId state_id, const uint8_t* payload, uint8_t len,
    const uint8_t src_mac[6])>;

/** Called when an ACK arrives for a command we sent. */
using MsgAckHandler = std::function<void(
    CmdId cmd, AckStatus status, uint16_t seq,
    const uint8_t src_mac[6])>;

/**
 * Called when a RELIABLE send resolves:
 *   delivered = true  → ACK received, status = remote AckStatus
 *   delivered = false → gave up after MSG_MAX_ATTEMPTS, status = FAIL
 */
using MsgDeliveryCb = std::function<void(
    uint16_t seq, CmdId cmd, bool delivered, AckStatus status,
    const uint8_t dst_mac[6])>;

/* ─── Transport Send Functions ───────────────────────────────────────────── */

using MsgTransportSendFn   = std::function<esp_err_t(const uint8_t* data, uint8_t len)>;
using MsgTransportSendToFn = std::function<esp_err_t(
    const uint8_t dst_mac[6], const uint8_t* data, uint8_t len)>;

/* ─── Main Class ─────────────────────────────────────────────────────────── */

class MessageProtocol {
public:
    static MessageProtocol& instance();
    MessageProtocol(const MessageProtocol&) = delete;
    MessageProtocol& operator=(const MessageProtocol&) = delete;

    /* ─── Setup ────────────────────────────────────────────────────── */

    esp_err_t begin();
    esp_err_t end();

    esp_err_t registerTransport(uint8_t transport_bit,
                                MsgTransportSendFn broadcast_fn,
                                MsgTransportSendToFn unicast_fn = nullptr);
    esp_err_t setTransportActive(uint8_t transport_bit, bool active);

    /* ─── Sending ──────────────────────────────────────────────────── */

    /** Fire-and-forget command. */
    esp_err_t sendCommand(const uint8_t dst_mac[6], CmdId cmd,
                          const uint8_t* payload = nullptr, uint8_t len = 0);

    esp_err_t broadcastCommand(CmdId cmd,
                               const uint8_t* payload = nullptr, uint8_t len = 0);

    /**
     * Reliable command: retried until ACK or MSG_MAX_ATTEMPTS.
     * Unicast only (broadcast returns ESP_ERR_INVALID_ARG).
     * Returns ESP_ERR_NO_MEM if all pending slots are busy.
     */
    esp_err_t sendCommandReliable(const uint8_t dst_mac[6], CmdId cmd,
                                  const uint8_t* payload = nullptr,
                                  uint8_t len = 0,
                                  uint16_t* out_seq = nullptr);

    esp_err_t sendQuery(const uint8_t dst_mac[6], CmdId query);

    esp_err_t sendState(CmdId state_id,
                        const uint8_t* payload = nullptr, uint8_t len = 0);

    esp_err_t sendStateTo(const uint8_t dst_mac[6], CmdId state_id,
                          uint16_t reply_seq,
                          const uint8_t* payload = nullptr, uint8_t len = 0);

   
   
   
   
   
                          /* ─── Convenience Senders ──────────────────────────────────────── */

    esp_err_t reportTemperature(float celsius);
    esp_err_t reportOnOff(bool on);
    esp_err_t reportLevel(uint8_t level);

    /** Hub → strip. reliable=false for live knob streams. */
    esp_err_t sendLightState(const uint8_t dst_mac[6], bool on,
                             uint8_t brightness, uint16_t hue, uint8_t white,
                             bool reliable = true);

    /** Strip → hub (broadcast state report). */
    esp_err_t reportLightState(bool on, uint8_t brightness,
                               uint16_t hue, uint8_t white);

    /* ─── Receiving ────────────────────────────────────────────────── */

    /** Feed raw bytes from any transport RX callback. */
    esp_err_t processMessage(const uint8_t* data, uint8_t len);

    /* ─── Handlers ─────────────────────────────────────────────────── */

    void setCommandHandler(MsgCommandHandler handler);
    void setQueryHandler(MsgQueryHandler handler);
    void setStateHandler(MsgStateHandler handler);
    void setAckHandler(MsgAckHandler handler);
    void setDeliveryCallback(MsgDeliveryCb cb);



       /* Debug */
    static const char* typeName(MsgType type);
    static const char* cmdName(CmdId cmd);
    void logPacket(const char* prefix, const MessagePacket* pkt);

    /* ─── Status ───────────────────────────────────────────────────── */

    uint8_t       pendingCount();
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
        bool     used;
        uint8_t  mac[6];
        uint16_t last_seq;
        uint16_t window;            ///< bitmask of recently seen seqs
        uint8_t  last_ack_status;   ///< re-sent on duplicate commands
        int64_t  last_seen_us;      ///< for LRU eviction
    };

    /* Helpers */
    uint16_t  nextSeq();
    void      buildHeader(MessagePacket& pkt, MsgType type, CmdId cmd,
                          const uint8_t dst_mac[6]);
    esp_err_t sendPacket(const MessagePacket& pkt);
    esp_err_t sendPacketTo(const uint8_t dst_mac[6], const MessagePacket& pkt);

    bool      dedupCheck(const uint8_t src_mac[6], uint16_t seq,
                         uint8_t* cached_status);
    void      storeAckStatus(const uint8_t src_mac[6], uint8_t status);

    static void retryTimerCb(void* arg);
    void        retryTick();

    void inline lock()   { xSemaphoreTake(_mutex, portMAX_DELAY); }
    void inline unlock() { xSemaphoreGive(_mutex); }

 
    /* State */
    bool                    _initialized;
    uint8_t                 _self_mac[6];
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
};

#endif // MESSAGE_PROTOCOL_H