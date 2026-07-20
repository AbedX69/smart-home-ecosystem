/*
 * =============================================================================
 * FILE:        message_protocol.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-18
 * VERSION:     1.0.0
 * =============================================================================
 */

#include "message_protocol.h"

static const char* TAG = "MsgProto";

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* =============================================================================
 * SINGLETON
 * ========================================================================== */

MessageProtocol& MessageProtocol::instance() {
    static MessageProtocol inst;
    return inst;
}

MessageProtocol::MessageProtocol()
    : _initialized(false)
    , _seq_counter(0)
    , _cmd_handler(nullptr)
    , _query_handler(nullptr)
    , _state_handler(nullptr)
    , _ack_handler(nullptr)
{
    _mutex = xSemaphoreCreateMutex();
    memset(_self_mac, 0, sizeof(_self_mac));
}

MessageProtocol::~MessageProtocol() {
    if (_mutex) vSemaphoreDelete(_mutex);
}

/* =============================================================================
 * SETUP
 * ========================================================================== */

esp_err_t MessageProtocol::begin() {
    if (_initialized) return ESP_OK;

    esp_read_mac(_self_mac, ESP_MAC_WIFI_STA);
    _initialized = true;

    ESP_LOGI(TAG, "Message Protocol ready (MAC=%02X:%02X:%02X:%02X:%02X:%02X)",
             _self_mac[0], _self_mac[1], _self_mac[2],
             _self_mac[3], _self_mac[4], _self_mac[5]);
    return ESP_OK;
}

void MessageProtocol::registerTransport(uint8_t bit, MsgTransportSendFn broadcast_fn,
                                          MsgTransportSendToFn unicast_fn) {
    for (auto& t : _transports) {
        if (!t.active) {
            t.bit = bit;
            t.broadcast = broadcast_fn;
            t.unicast = unicast_fn;
            t.active = true;
            return;
        }
    }
    ESP_LOGW(TAG, "No transport slot available");
}

/* =============================================================================
 * PACKET BUILDING
 * ========================================================================== */

void MessageProtocol::buildHeader(MessagePacket& pkt, MsgType type, CmdId cmd,
                                    const uint8_t dst_mac[6]) {
    memset(&pkt, 0, sizeof(pkt));
    pkt.magic = MSG_MAGIC;
    pkt.msg_type = (uint8_t)type;
    pkt.cmd_id = (uint8_t)cmd;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    pkt.seq = _seq_counter++;
    xSemaphoreGive(_mutex);

    memcpy(pkt.src_mac, _self_mac, 6);
    memcpy(pkt.dst_mac, dst_mac, 6);
}

/* =============================================================================
 * TRANSPORT DISPATCH
 * =============================================================================
 * 
 * Broadcast: sends on all transports using broadcast function.
 * Unicast: tries unicast function first, falls back to broadcast.
 * ========================================================================== */

esp_err_t MessageProtocol::sendPacket(const MessagePacket& pkt) {
    esp_err_t last_err = ESP_ERR_NOT_FOUND;
    for (auto& t : _transports) {
        if (t.active && t.broadcast) {
            esp_err_t ret = t.broadcast((const uint8_t*)&pkt, sizeof(pkt));
            if (ret == ESP_OK) last_err = ESP_OK;
        }
    }
    return last_err;
}

esp_err_t MessageProtocol::sendPacketTo(const uint8_t dst_mac[6],
                                          const MessagePacket& pkt) {
    /* If dst is broadcast, use broadcast */
    if (memcmp(dst_mac, BROADCAST, 6) == 0) {
        return sendPacket(pkt);
    }

    /* Try unicast first */
    for (auto& t : _transports) {
        if (t.active && t.unicast) {
            esp_err_t ret = t.unicast(dst_mac, (const uint8_t*)&pkt, sizeof(pkt));
            if (ret == ESP_OK) return ESP_OK;
        }
    }

    /* Fallback to broadcast */
    return sendPacket(pkt);
}

/* =============================================================================
 * SEND COMMANDS
 * ========================================================================== */

esp_err_t MessageProtocol::sendCommand(const uint8_t dst_mac[6], CmdId cmd,
                                         const uint8_t* payload, uint8_t len) {
    if (!_initialized) return ESP_ERR_INVALID_STATE;
    if (len > MSG_PAYLOAD_MAX) return ESP_ERR_INVALID_SIZE;

    MessagePacket pkt;
    buildHeader(pkt, MsgType::COMMAND, cmd, dst_mac);
    if (payload && len > 0) {
        memcpy(pkt.payload, payload, len);
        pkt.payload_len = len;
    }

    logPacket("TX CMD", &pkt);
    return sendPacketTo(dst_mac, pkt);
}

esp_err_t MessageProtocol::broadcastCommand(CmdId cmd,
                                              const uint8_t* payload, uint8_t len) {
    return sendCommand(BROADCAST, cmd, payload, len);
}

esp_err_t MessageProtocol::sendQuery(const uint8_t dst_mac[6], CmdId query) {
    if (!_initialized) return ESP_ERR_INVALID_STATE;

    MessagePacket pkt;
    buildHeader(pkt, MsgType::QUERY, query, dst_mac);

    logPacket("TX QRY", &pkt);
    return sendPacketTo(dst_mac, pkt);
}

esp_err_t MessageProtocol::sendState(CmdId state_id,
                                       const uint8_t* payload, uint8_t len) {
    if (!_initialized) return ESP_ERR_INVALID_STATE;
    if (len > MSG_PAYLOAD_MAX) return ESP_ERR_INVALID_SIZE;

    MessagePacket pkt;
    buildHeader(pkt, MsgType::STATE, state_id, BROADCAST);
    if (payload && len > 0) {
        memcpy(pkt.payload, payload, len);
        pkt.payload_len = len;
    }

    logPacket("TX STATE", &pkt);
    return sendPacket(pkt);
}

esp_err_t MessageProtocol::sendStateTo(const uint8_t dst_mac[6], CmdId state_id,
                                         uint16_t reply_seq,
                                         const uint8_t* payload, uint8_t len) {
    if (!_initialized) return ESP_ERR_INVALID_STATE;
    if (len > MSG_PAYLOAD_MAX) return ESP_ERR_INVALID_SIZE;

    MessagePacket pkt;
    buildHeader(pkt, MsgType::STATE, state_id, dst_mac);
    pkt.seq = reply_seq;  // Match the query's sequence number
    if (payload && len > 0) {
        memcpy(pkt.payload, payload, len);
        pkt.payload_len = len;
    }

    logPacket("TX STATE→", &pkt);
    return sendPacketTo(dst_mac, pkt);
}

/* =============================================================================
 * CONVENIENCE SENDERS
 * ========================================================================== */

esp_err_t MessageProtocol::reportTemperature(float celsius) {
    int16_t temp_x100 = (int16_t)(celsius * 100);
    uint8_t payload[2];
    payload[0] = (temp_x100 >> 8) & 0xFF;
    payload[1] = temp_x100 & 0xFF;
    return sendState(CmdId::REPORT_TEMP, payload, 2);
}

esp_err_t MessageProtocol::reportOnOff(bool on) {
    uint8_t val = on ? 1 : 0;
    return sendState(CmdId::REPORT_ON_OFF, &val, 1);
}

esp_err_t MessageProtocol::reportLevel(uint8_t level) {
    return sendState(CmdId::REPORT_LEVEL, &level, 1);
}

/* =============================================================================
 * PROCESS INCOMING
 * =============================================================================
 * 
 * Called from any transport's receive callback. Validates, ignores own
 * messages, checks if addressed to us (or broadcast), then dispatches
 * to the appropriate handler.
 * 
 * For COMMAND messages: handler returns AckStatus, we auto-send ACK back.
 * For QUERY messages: handler is responsible for calling sendStateTo().
 * ========================================================================== */

esp_err_t MessageProtocol::processMessage(const uint8_t* data, uint8_t len) {
    if (!data || len < sizeof(MessagePacket)) return ESP_ERR_INVALID_SIZE;

    const MessagePacket* pkt = (const MessagePacket*)data;

    /* Validate magic */
    if (pkt->magic != MSG_MAGIC) return ESP_ERR_INVALID_ARG;

    /* Ignore own messages */
    if (memcmp(pkt->src_mac, _self_mac, 6) == 0) return ESP_OK;

    /* Check if addressed to us or broadcast */
    bool is_for_us = (memcmp(pkt->dst_mac, _self_mac, 6) == 0) ||
                     (memcmp(pkt->dst_mac, BROADCAST, 6) == 0);
    if (!is_for_us) return ESP_OK;

    MsgType type = (MsgType)pkt->msg_type;
    CmdId cmd = (CmdId)pkt->cmd_id;

    logPacket("RX", pkt);

    switch (type) {

        case MsgType::COMMAND: {
            AckStatus ack_status = AckStatus::UNKNOWN_CMD;

            if (_cmd_handler) {
                ack_status = _cmd_handler(cmd, pkt->payload, pkt->payload_len,
                                           pkt->src_mac);
            }

            /* Auto-send ACK */
            MessagePacket ack;
            buildHeader(ack, MsgType::ACK, cmd, pkt->src_mac);
            ack.seq = pkt->seq;  // Echo the command's sequence
            ack.status = (uint8_t)ack_status;

            logPacket("TX ACK", &ack);
            sendPacketTo(pkt->src_mac, ack);
            break;
        }

        case MsgType::QUERY: {
            if (_query_handler) {
                _query_handler(cmd, pkt->src_mac, pkt->seq);
            }
            break;
        }

        case MsgType::STATE: {
            if (_state_handler) {
                _state_handler(cmd, pkt->payload, pkt->payload_len,
                                pkt->src_mac);
            }
            break;
        }

        case MsgType::ACK: {
            if (_ack_handler) {
                _ack_handler(cmd, (AckStatus)pkt->status, pkt->seq,
                              pkt->src_mac);
            }
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown message type: 0x%02X", pkt->msg_type);
            break;
    }

    return ESP_OK;
}

/* =============================================================================
 * HANDLERS
 * ========================================================================== */

void MessageProtocol::setCommandHandler(MsgCommandHandler handler) { _cmd_handler = handler; }
void MessageProtocol::setQueryHandler(MsgQueryHandler handler) { _query_handler = handler; }
void MessageProtocol::setStateHandler(MsgStateHandler handler) { _state_handler = handler; }
void MessageProtocol::setAckHandler(MsgAckHandler handler) { _ack_handler = handler; }

/* =============================================================================
 * DEBUG LOGGING
 * =============================================================================
 * 
 * Logs every packet in a human-readable format so you can follow
 * the conversation in the serial monitor:
 * 
 *   TX CMD  → ON       seq=42  dst=AA:BB:CC:DD:EE:FF
 *   RX ACK  ← ON [OK]  seq=42  src=AA:BB:CC:DD:EE:FF
 * ========================================================================== */

const char* MessageProtocol::typeName(MsgType type) {
    switch (type) {
        case MsgType::COMMAND: return "CMD";
        case MsgType::QUERY:   return "QRY";
        case MsgType::STATE:   return "STATE";
        case MsgType::ACK:     return "ACK";
        default:               return "???";
    }
}

const char* MessageProtocol::cmdName(CmdId cmd) {
    switch (cmd) {
        case CmdId::NONE:            return "NONE";
        case CmdId::PING:            return "PING";
        case CmdId::REBOOT:          return "REBOOT";
        case CmdId::FACTORY_RESET:   return "FACTORY_RST";
        case CmdId::IDENTIFY:        return "IDENTIFY";
        case CmdId::ON:              return "ON";
        case CmdId::OFF:             return "OFF";
        case CmdId::TOGGLE:          return "TOGGLE";
        case CmdId::SET_LEVEL:       return "SET_LEVEL";
        case CmdId::FADE_TO:         return "FADE_TO";
        case CmdId::SET_RGB:         return "SET_RGB";
        case CmdId::SET_TEMP_K:      return "SET_TEMP_K";
        case CmdId::GET_TEMPERATURE: return "GET_TEMP";
        case CmdId::GET_HUMIDITY:    return "GET_HUM";
        case CmdId::GET_STATE:       return "GET_STATE";
        case CmdId::GET_BATTERY:     return "GET_BAT";
        case CmdId::REPORT_TEMP:     return "RPT_TEMP";
        case CmdId::REPORT_HUMIDITY: return "RPT_HUM";
        case CmdId::REPORT_ON_OFF:   return "RPT_ONOFF";
        case CmdId::REPORT_LEVEL:    return "RPT_LEVEL";
        case CmdId::REPORT_BATTERY:  return "RPT_BAT";
        case CmdId::GARAGE_OPEN:     return "GARAGE_OPEN";
        case CmdId::GARAGE_CLOSE:    return "GARAGE_CLOSE";
        case CmdId::GARAGE_STOP:     return "GARAGE_STOP";
        default:                     return "CUSTOM";
    }
}

void MessageProtocol::logPacket(const char* prefix, const MessagePacket* pkt) {
    MsgType type = (MsgType)pkt->msg_type;
    CmdId cmd = (CmdId)pkt->cmd_id;

    char extra[32] = {};
    if (type == MsgType::ACK) {
        const char* status_str = "?";
        switch ((AckStatus)pkt->status) {
            case AckStatus::OK:             status_str = "OK"; break;
            case AckStatus::FAIL:           status_str = "FAIL"; break;
            case AckStatus::UNKNOWN_CMD:    status_str = "UNKNOWN"; break;
            case AckStatus::BUSY:           status_str = "BUSY"; break;
            case AckStatus::NOT_SUPPORTED:  status_str = "UNSUPPORTED"; break;
        }
        snprintf(extra, sizeof(extra), " [%s]", status_str);
    }

    /* Show payload bytes if present */
    char payload_str[40] = {};
    if (pkt->payload_len > 0) {
        int pos = 0;
        for (uint8_t i = 0; i < pkt->payload_len && i < 6; i++) {
            pos += snprintf(payload_str + pos, sizeof(payload_str) - pos,
                            "%02X ", pkt->payload[i]);
        }
    }

    ESP_LOGI(TAG, "%s %-5s %-12s%s seq=%d  %02X:%02X→%02X:%02X %s",
             prefix,
             typeName(type),
             cmdName(cmd),
             extra,
             pkt->seq,
             pkt->src_mac[4], pkt->src_mac[5],
             pkt->dst_mac[4], pkt->dst_mac[5],
             payload_str);
}
