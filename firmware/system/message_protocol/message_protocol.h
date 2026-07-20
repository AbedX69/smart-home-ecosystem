/*
 * =============================================================================
 * FILE:        message_protocol.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-18
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    All ESP32 variants (ESP-IDF v5.x)
 * =============================================================================
 * 
 * Message Protocol — Transport-agnostic command & state messaging.
 * 
 * Provides:
 *   - Commands:   "Turn on", "Set brightness to 200", "Toggle"
 *   - Queries:    "What's your temperature?", "Are you on?"
 *   - State:      "I'm now ON", "Temperature is 23.5°C"
 *   - Acks:       "Got it, command succeeded/failed"
 *   - Transport-agnostic: same packet over ESP-NOW, LoRa, WiFi, BLE
 *   - Sequence tracking for reliable command delivery
 *   - Debug-friendly text logging of all messages
 * 
 * =============================================================================
 * BEGINNER'S GUIDE
 * =============================================================================
 * 
 * WHY A MESSAGE PROTOCOL?
 * ~~~~~~~~~~~~~~~~~~~~~~~
 * The Device Registry tells you WHO is on the network.
 * The Message Protocol tells them WHAT TO DO.
 * 
 *     Controller                  Light
 *     ┌──────────────┐           ┌──────────────┐
 *     │ "Turn on the │──CMD────→ │ Turns on LED │
 *     │  kitchen     │           │              │
 *     │  light"      │←──ACK─── │ "Done, I'm   │
 *     │              │           │  now ON"     │
 *     └──────────────┘           └──────────────┘
 * 
 *     Controller                  Sensor
 *     ┌──────────────┐           ┌──────────────┐
 *     │ "What's the  │──QRY────→ │              │
 *     │  temperature?"│           │              │
 *     │              │←──STATE── │ "23.5°C"     │
 *     └──────────────┘           └──────────────┘
 * 
 *     Sensor (unsolicited)        Controller
 *     ┌──────────────┐           ┌──────────────┐
 *     │ Temp changed │──STATE──→ │ Updates      │
 *     │ to 24.0°C    │           │ display      │
 *     └──────────────┘           └──────────────┘
 * 
 * 
 * PACKET FORMAT (32 bytes, fixed):
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * 
 *     ┌────────────────────────────────────────┐
 *     │  Byte  │ Size │ Field                  │
 *     ├────────────────────────────────────────┤
 *     │  0-3   │  4   │ Magic (0x534D4D53)     │
 *     │  4     │  1   │ Message type            │
 *     │  5     │  1   │ Command ID              │
 *     │  6-7   │  2   │ Sequence number         │
 *     │  8-13  │  6   │ Source MAC              │
 *     │  14-19 │  6   │ Destination MAC         │
 *     │  20    │  1   │ Payload length (0-10)   │
 *     │  21    │  1   │ Status / flags          │
 *     │  22-31 │  10  │ Payload                 │
 *     └────────────────────────────────────────┘
 * 
 *  Total: 32 bytes (fits ESP-NOW 250B, LoRa 255B, UDP, BLE)
 * 
 * =============================================================================
 * USAGE
 * =============================================================================
 * 
 * SENDING A COMMAND:
 *     MessageProtocol& msg = MessageProtocol::instance();
 *     msg.begin();
 *     msg.registerTransport(TRANSPORT_ESPNOW, espnow_send_fn);
 *     
 *     // Turn on a specific device
 *     msg.sendCommand(light_mac, CmdId::ON);
 *     
 *     // Set brightness on a device
 *     uint8_t level = 200;
 *     msg.sendCommand(light_mac, CmdId::SET_LEVEL, &level, 1);
 * 
 * HANDLING INCOMING:
 *     msg.setCommandHandler(onCommand);
 *     msg.setQueryHandler(onQuery);
 *     msg.setStateHandler(onState);
 * 
 * =============================================================================
 */

#ifndef MESSAGE_PROTOCOL_H
#define MESSAGE_PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* ─── Constants ──────────────────────────────────────────────────────────── */

#define MSG_MAGIC               0x534D4D53  /* "SMMS" = SmartMsg */
#define MSG_PAYLOAD_MAX         10
#define MSG_BROADCAST_MAC       {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define MSG_ACK_TIMEOUT_MS      2000

/* ─── Message Types ──────────────────────────────────────────────────────── */

enum class MsgType : uint8_t {
    COMMAND     = 0x01,     ///< Do something (ON, OFF, SET_LEVEL...)
    QUERY       = 0x02,     ///< Ask for data (GET_TEMP, GET_STATE...)
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

    /* ── Garage ─────────── */
    GARAGE_OPEN     = 0x60,
    GARAGE_CLOSE    = 0x61,
    GARAGE_STOP     = 0x62,

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

/* ─── Message Packet (32 bytes) ──────────────────────────────────────────── */

struct __attribute__((packed)) MessagePacket {
    uint32_t    magic;
    uint8_t     msg_type;       ///< MsgType
    uint8_t     cmd_id;         ///< CmdId
    uint16_t    seq;            ///< Sequence number
    uint8_t     src_mac[6];
    uint8_t     dst_mac[6];
    uint8_t     payload_len;    ///< 0-10
    uint8_t     status;         ///< AckStatus for ACK, 0 otherwise
    uint8_t     payload[MSG_PAYLOAD_MAX];
};

static_assert(sizeof(MessagePacket) == 32, "MessagePacket must be 32 bytes");

/* ─── Callbacks ──────────────────────────────────────────────────────────── */

/**
 * Called when a command arrives. Return an AckStatus.
 * The protocol layer will automatically send an ACK back.
 */
using MsgCommandHandler = std::function<AckStatus(
    CmdId cmd, const uint8_t* payload, uint8_t len,
    const uint8_t src_mac[6])>;

/** Called when a query arrives. Handler should call sendState() in response. */
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

/* ─── Transport Send Function ────────────────────────────────────────────── */
/* Same type as device_registry — can reuse the same send functions */
using MsgTransportSendFn = std::function<esp_err_t(const uint8_t* data, uint8_t len)>;

/* Targeted send: sends to a specific MAC (for transports that support it) */
using MsgTransportSendToFn = std::function<esp_err_t(
    const uint8_t dst_mac[6], const uint8_t* data, uint8_t len)>;

/* ─── Main Class ─────────────────────────────────────────────────────────── */

class MessageProtocol {
public:
    static MessageProtocol& instance();
    MessageProtocol(const MessageProtocol&) = delete;
    MessageProtocol& operator=(const MessageProtocol&) = delete;

    /* ─── Setup ────────────────────────────────────────────────────────── */

    esp_err_t begin();

    /**
     * @brief Register a transport.
     * @param bit           TRANSPORT_ESPNOW, etc.
     * @param broadcast_fn  Sends to all devices
     * @param unicast_fn    Sends to specific MAC (optional, nullptr = broadcast only)
     */
    void registerTransport(uint8_t bit, MsgTransportSendFn broadcast_fn,
                            MsgTransportSendToFn unicast_fn = nullptr);

    /* ─── Send ─────────────────────────────────────────────────────────── */

    /** Send a command to a specific device */
    esp_err_t sendCommand(const uint8_t dst_mac[6], CmdId cmd,
                           const uint8_t* payload = nullptr, uint8_t len = 0);

    /** Broadcast a command to all devices */
    esp_err_t broadcastCommand(CmdId cmd,
                                const uint8_t* payload = nullptr, uint8_t len = 0);

    /** Send a query to a specific device */
    esp_err_t sendQuery(const uint8_t dst_mac[6], CmdId query);

    /** Send a state report (broadcast) */
    esp_err_t sendState(CmdId state_id,
                         const uint8_t* payload = nullptr, uint8_t len = 0);

    /** Send a state report to a specific device (reply to query) */
    esp_err_t sendStateTo(const uint8_t dst_mac[6], CmdId state_id,
                           uint16_t reply_seq,
                           const uint8_t* payload = nullptr, uint8_t len = 0);

    /** Convenience: report temperature (broadcasts) */
    esp_err_t reportTemperature(float celsius);

    /** Convenience: report on/off state (broadcasts) */
    esp_err_t reportOnOff(bool on);

    /** Convenience: report level (broadcasts) */
    esp_err_t reportLevel(uint8_t level);

    /* ─── Receive ──────────────────────────────────────────────────────── */

    /**
     * @brief Process a raw incoming message from any transport.
     * 
     * Call this from your ESP-NOW/LoRa/WiFi/BLE receive callback.
     */
    esp_err_t processMessage(const uint8_t* data, uint8_t len);

    /* ─── Handlers ─────────────────────────────────────────────────────── */

    void setCommandHandler(MsgCommandHandler handler);
    void setQueryHandler(MsgQueryHandler handler);
    void setStateHandler(MsgStateHandler handler);
    void setAckHandler(MsgAckHandler handler);

    /* ─── Debug ────────────────────────────────────────────────────────── */

    /** @brief Get human-readable name for a CmdId */
    static const char* cmdName(CmdId cmd);

    /** @brief Get human-readable name for a MsgType */
    static const char* typeName(MsgType type);

    /** @brief Log a packet in human-readable form */
    static void logPacket(const char* prefix, const MessagePacket* pkt);

private:
    MessageProtocol();
    ~MessageProtocol();

    /* Internal send */
    esp_err_t sendPacket(const MessagePacket& pkt);
    esp_err_t sendPacketTo(const uint8_t dst_mac[6], const MessagePacket& pkt);
    void buildHeader(MessagePacket& pkt, MsgType type, CmdId cmd,
                      const uint8_t dst_mac[6]);

    /* State */
    bool                _initialized;
    uint8_t             _self_mac[6];
    uint16_t            _seq_counter;
    SemaphoreHandle_t   _mutex;

    MsgCommandHandler   _cmd_handler;
    MsgQueryHandler     _query_handler;
    MsgStateHandler     _state_handler;
    MsgAckHandler       _ack_handler;

    /* Transports */
    struct MsgTransport {
        uint8_t                 bit       = 0;
        MsgTransportSendFn      broadcast = nullptr;
        MsgTransportSendToFn    unicast   = nullptr;
        bool                    active    = false;
    };
    MsgTransport _transports[5];
};

#endif // MESSAGE_PROTOCOL_H
