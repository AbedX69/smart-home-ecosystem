/*
 * =============================================================================
 * FILE:        message_protocol.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-18
 * MODIFIED:    2026-07-21
 * VERSION:     2.0.0
 * =============================================================================
 *
 * Implementation notes:
 *   - Reliable sends live in a fixed 8-slot table. An esp_timer ticking every
 *     100ms drives retries/expiry. No extra FreeRTOS task, no heap churn.
 *   - Dedup is a per-peer sliding window of the last 16 sequence numbers.
 *     seq starts at a random value each boot so a reboot never lands inside
 *     a peer's old window.
 *   - Callbacks are ALWAYS invoked with the mutex released — handlers may
 *     call send*() freely without deadlocking.
 * =============================================================================
 */

#include "message_protocol.h"
#include "esp_random.h"

static const char* TAG = "MsgProto";

static const uint8_t BROADCAST[6] = MSG_BROADCAST_MAC;

/* =============================================================================
 * SINGLETON / LIFECYCLE
 * ========================================================================== */

MessageProtocol& MessageProtocol::instance() {
    static MessageProtocol inst;
    return inst;
}

MessageProtocol::MessageProtocol()
    : _initialized(false)
    , _next_seq(0)
    , _mutex(nullptr)
    , _retry_timer(nullptr)
    , _cmd_handler(nullptr)
    , _query_handler(nullptr)
    , _state_handler(nullptr)
    , _ack_handler(nullptr)
    , _delivery_cb(nullptr)
{
    memset(_self_mac, 0, sizeof(_self_mac));
    memset(_pending, 0, sizeof(_pending));
    memset(_peers, 0, sizeof(_peers));
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) ESP_LOGE(TAG, "Failed to create mutex!");
}

MessageProtocol::~MessageProtocol() {
    end();
    if (_mutex) { vSemaphoreDelete(_mutex); _mutex = nullptr; }
}

esp_err_t MessageProtocol::begin() {
    if (_initialized) return ESP_OK;

    esp_read_mac(_self_mac, ESP_MAC_WIFI_STA);

    /* Random seq start: after a reboot our seqs won't collide with the
     * dedup window a peer still holds for our old boot. */
    _next_seq = (uint16_t)esp_random();

    esp_timer_create_args_t targs = {};
    targs.callback = &MessageProtocol::retryTimerCb;
    targs.arg      = this;
    targs.name     = "msg_retry";

    esp_err_t ret = esp_timer_create(&targs, &_retry_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Retry timer create failed: 0x%x", ret);
        return ret;
    }
    ret = esp_timer_start_periodic(_retry_timer,
                                   (uint64_t)MSG_RETRY_TICK_MS * 1000ULL);
    if (ret != ESP_OK) {
        esp_timer_delete(_retry_timer);
        _retry_timer = nullptr;
        ESP_LOGE(TAG, "Retry timer start failed: 0x%x", ret);
        return ret;
    }

    _initialized = true;
    ESP_LOGI(TAG, "Message Protocol v2 ready — 48B packets, %dB payload, "
                  "self=%02X:%02X:%02X:%02X:%02X:%02X seq0=%u",
             MSG_PAYLOAD_MAX,
             _self_mac[0], _self_mac[1], _self_mac[2],
             _self_mac[3], _self_mac[4], _self_mac[5],
             _next_seq);
    return ESP_OK;
}

esp_err_t MessageProtocol::end() {
    if (!_initialized) return ESP_OK;
    _initialized = false;

    if (_retry_timer) {
        esp_timer_stop(_retry_timer);
        esp_timer_delete(_retry_timer);
        _retry_timer = nullptr;
    }
    lock();
    memset(_pending, 0, sizeof(_pending));
    unlock();
    return ESP_OK;
}

/* =============================================================================
 * TRANSPORTS
 * =============================================================================
 * Register everything BEFORE traffic starts — the hot path iterates the
 * vector without the mutex on purpose (send fns may take their own locks,
 * e.g. EspNowManager's, and we avoid any lock-order coupling).
 * ========================================================================== */

esp_err_t MessageProtocol::registerTransport(uint8_t transport_bit,
                                             MsgTransportSendFn broadcast_fn,
                                             MsgTransportSendToFn unicast_fn) {
    lock();
    for (auto& t : _transports) {
        if (t.bit == transport_bit) {
            t.broadcast = broadcast_fn;
            t.unicast   = unicast_fn;
            t.active    = true;
            unlock();
            return ESP_OK;
        }
    }
    _transports.push_back({transport_bit, true, broadcast_fn, unicast_fn});
    unlock();
    ESP_LOGI(TAG, "Transport 0x%02X registered%s", transport_bit,
             unicast_fn ? " (unicast capable)" : "");
    return ESP_OK;
}

esp_err_t MessageProtocol::setTransportActive(uint8_t transport_bit, bool active) {
    lock();
    for (auto& t : _transports) {
        if (t.bit == transport_bit) {
            t.active = active;
            unlock();
            return ESP_OK;
        }
    }
    unlock();
    return ESP_ERR_NOT_FOUND;
}

/* =============================================================================
 * LOW-LEVEL SEND
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
    if (memcmp(dst_mac, BROADCAST, 6) == 0) {
        return sendPacket(pkt);
    }
    /* Unicast first */
    for (auto& t : _transports) {
        if (t.active && t.unicast) {
            esp_err_t ret = t.unicast(dst_mac, (const uint8_t*)&pkt, sizeof(pkt));
            if (ret == ESP_OK) return ESP_OK;
        }
    }
    /* Fallback: broadcast; dst_mac in the header filters on the far side */
    return sendPacket(pkt);
}

/* =============================================================================
 * HEADER / SEQ
 * ========================================================================== */

uint16_t MessageProtocol::nextSeq() {
    lock();
    uint16_t s = _next_seq++;
    unlock();
    return s;
}

void MessageProtocol::buildHeader(MessagePacket& pkt, MsgType type, CmdId cmd,
                                  const uint8_t dst_mac[6]) {
    memset(&pkt, 0, sizeof(pkt));   /* zero EVERYTHING — v1.x UB fix kept */
    pkt.magic     = MSG_MAGIC;
    pkt.proto_ver = MSG_PROTO_VER;
    pkt.msg_type  = (uint8_t)type;
    pkt.cmd_id    = (uint8_t)cmd;
    pkt.seq       = nextSeq();
    memcpy(pkt.src_mac, _self_mac, 6);
    memcpy(pkt.dst_mac, dst_mac, 6);
}

/* =============================================================================
 * SENDING
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

esp_err_t MessageProtocol::sendCommandReliable(const uint8_t dst_mac[6], CmdId cmd,
                                               const uint8_t* payload, uint8_t len,
                                               uint16_t* out_seq) {
    if (!_initialized) return ESP_ERR_INVALID_STATE;
    if (len > MSG_PAYLOAD_MAX) return ESP_ERR_INVALID_SIZE;
    if (!dst_mac || memcmp(dst_mac, BROADCAST, 6) == 0) {
        ESP_LOGW(TAG, "Reliable send requires a unicast destination");
        return ESP_ERR_INVALID_ARG;
    }

    MessagePacket pkt;
    buildHeader(pkt, MsgType::COMMAND, cmd, dst_mac);
    pkt.flags |= MSG_FLAG_ACK_REQ;
    if (payload && len > 0) {
        memcpy(pkt.payload, payload, len);
        pkt.payload_len = len;
    }

    lock();
    PendingTx* slot = nullptr;
    for (int i = 0; i < MSG_PENDING_SLOTS; i++) {
        if (!_pending[i].used) { slot = &_pending[i]; break; }
    }
    if (!slot) {
        unlock();
        ESP_LOGW(TAG, "Reliable send rejected: all %d slots busy",
                 MSG_PENDING_SLOTS);
        return ESP_ERR_NO_MEM;
    }
    slot->used          = true;
    slot->attempts      = 1;
    slot->next_retry_us = esp_timer_get_time()
                        + (int64_t)MSG_RETRY_BASE_MS * 1000LL;
    slot->pkt           = pkt;
    unlock();

    if (out_seq) *out_seq = pkt.seq;
    logPacket("TX CMD*", &pkt);
    return sendPacketTo(dst_mac, pkt);
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
    pkt.seq = reply_seq;    /* match the query's sequence number */
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

esp_err_t MessageProtocol::sendLightState(const uint8_t dst_mac[6], bool on,
                                          uint8_t brightness, uint16_t hue,
                                          uint8_t white, bool reliable) {
    uint8_t p[5];
    msgEncodeLightState(p, on, brightness, hue, white);
    return reliable
        ? sendCommandReliable(dst_mac, CmdId::SET_LIGHT_STATE, p, 5)
        : sendCommand(dst_mac, CmdId::SET_LIGHT_STATE, p, 5);
}

esp_err_t MessageProtocol::reportLightState(bool on, uint8_t brightness,
                                            uint16_t hue, uint8_t white) {
    uint8_t p[5];
    msgEncodeLightState(p, on, brightness, hue, white);
    return sendState(CmdId::REPORT_LIGHT_STATE, p, 5);
}

/* =============================================================================
 * RX DEDUP
 * =============================================================================
 * Returns true if (src, seq) was already seen. Records new seqs.
 * On duplicate, *cached_status gets the last ACK status we sent that peer
 * so the caller can re-ACK without re-executing.
 * ========================================================================== */

bool MessageProtocol::dedupCheck(const uint8_t src_mac[6], uint16_t seq,
                                 uint8_t* cached_status) {
    bool dup = false;
    int64_t now = esp_timer_get_time();

    lock();

    PeerDedup* p = nullptr;
    PeerDedup* free_slot = nullptr;
    PeerDedup* oldest = &_peers[0];

    for (int i = 0; i < MSG_DEDUP_PEERS; i++) {
        PeerDedup& e = _peers[i];
        if (e.used) {
            if (memcmp(e.mac, src_mac, 6) == 0) { p = &e; break; }
            if (!oldest->used || e.last_seen_us < oldest->last_seen_us)
                oldest = &e;
        } else if (!free_slot) {
            free_slot = &e;
        }
    }

    if (!p) {
        /* New peer: take a free slot, or evict the least recently seen */
        p = free_slot ? free_slot : oldest;
        memset(p, 0, sizeof(*p));
        p->used            = true;
        memcpy(p->mac, src_mac, 6);
        p->last_seq        = seq;
        p->window          = 1;
        p->last_ack_status = (uint8_t)AckStatus::OK;
        p->last_seen_us    = now;
        unlock();
        return false;
    }

    p->last_seen_us = now;
    int16_t diff = (int16_t)(seq - p->last_seq);

    if (diff == 0) {
        dup = true;
    } else if (diff > 0) {
        /* Newer than anything seen: slide the window forward */
        if (diff >= MSG_DEDUP_WINDOW) p->window = 0;
        else                          p->window <<= diff;
        p->window  |= 1;
        p->last_seq = seq;
    } else {
        int16_t back = (int16_t)(-diff);
        if (back >= MSG_DEDUP_WINDOW) {
            /* Far outside the window — peer rebooted with a fresh random
             * seq. Resync instead of dropping them forever. */
            p->last_seq = seq;
            p->window   = 1;
        } else {
            uint16_t bit = (uint16_t)(1u << back);
            if (p->window & bit) dup = true;
            else                 p->window |= bit;
        }
    }

    if (dup && cached_status) *cached_status = p->last_ack_status;
    unlock();
    return dup;
}

void MessageProtocol::storeAckStatus(const uint8_t src_mac[6], uint8_t status) {
    lock();
    for (int i = 0; i < MSG_DEDUP_PEERS; i++) {
        if (_peers[i].used && memcmp(_peers[i].mac, src_mac, 6) == 0) {
            _peers[i].last_ack_status = status;
            break;
        }
    }
    unlock();
}

/* =============================================================================
 * RETRY ENGINE
 * ========================================================================== */

void MessageProtocol::retryTimerCb(void* arg) {
    static_cast<MessageProtocol*>(arg)->retryTick();
}

void MessageProtocol::retryTick() {
    if (!_initialized) return;

    MessagePacket to_resend[MSG_PENDING_SLOTS];
    int n_resend = 0;

    struct Failed { uint16_t seq; CmdId cmd; uint8_t dst[6]; };
    Failed failed[MSG_PENDING_SLOTS];
    int n_failed = 0;

    int64_t now = esp_timer_get_time();

    lock();
    for (int i = 0; i < MSG_PENDING_SLOTS; i++) {
        PendingTx& s = _pending[i];
        if (!s.used || now < s.next_retry_us) continue;

        if (s.attempts >= MSG_MAX_ATTEMPTS) {
            failed[n_failed].seq = s.pkt.seq;
            failed[n_failed].cmd = (CmdId)s.pkt.cmd_id;
            memcpy(failed[n_failed].dst, s.pkt.dst_mac, 6);
            n_failed++;
            s.used = false;
        } else {
            s.attempts++;
            int64_t backoff_ms = (int64_t)MSG_RETRY_BASE_MS << (s.attempts - 1);
            s.next_retry_us = now + backoff_ms * 1000LL;
            to_resend[n_resend++] = s.pkt;
        }
    }
    unlock();

    for (int i = 0; i < n_resend; i++) {
        logPacket("TX RTRY", &to_resend[i]);
        sendPacketTo(to_resend[i].dst_mac, to_resend[i]);
    }
    for (int i = 0; i < n_failed; i++) {
        ESP_LOGW(TAG, "Delivery FAILED: %s seq=%u → %02X:%02X:%02X:%02X:%02X:%02X "
                      "after %d attempts",
                 cmdName(failed[i].cmd), failed[i].seq,
                 failed[i].dst[0], failed[i].dst[1], failed[i].dst[2],
                 failed[i].dst[3], failed[i].dst[4], failed[i].dst[5],
                 MSG_MAX_ATTEMPTS);
        if (_delivery_cb) {
            _delivery_cb(failed[i].seq, failed[i].cmd, false,
                         AckStatus::FAIL, failed[i].dst);
        }
    }
}

/* =============================================================================
 * PROCESS INCOMING
 * ========================================================================== */

esp_err_t MessageProtocol::processMessage(const uint8_t* data, uint8_t len) {
    if (!data || len < sizeof(MessagePacket)) return ESP_ERR_INVALID_SIZE;

    const MessagePacket* pkt = (const MessagePacket*)data;

    if (pkt->magic != MSG_MAGIC)              return ESP_ERR_INVALID_ARG;
    if (pkt->proto_ver != MSG_PROTO_VER)      return ESP_ERR_INVALID_ARG;
    if (pkt->payload_len > MSG_PAYLOAD_MAX)   return ESP_ERR_INVALID_SIZE;

    /* Ignore own messages */
    if (memcmp(pkt->src_mac, _self_mac, 6) == 0) return ESP_OK;

    /* Addressed to us or broadcast? */
    bool is_broadcast = (memcmp(pkt->dst_mac, BROADCAST, 6) == 0);
    bool is_for_us    = is_broadcast ||
                        (memcmp(pkt->dst_mac, _self_mac, 6) == 0);
    if (!is_for_us) return ESP_OK;

    MsgType type = (MsgType)pkt->msg_type;
    CmdId   cmd  = (CmdId)pkt->cmd_id;

    logPacket("RX", pkt);

    switch (type) {

        case MsgType::COMMAND: {
            if (!is_broadcast) {
                uint8_t cached = (uint8_t)AckStatus::OK;
                if (dedupCheck(pkt->src_mac, pkt->seq, &cached)) {
                    /* Duplicate: the original ACK probably got lost.
                     * Re-ACK from cache, do NOT re-execute. */
                    MessagePacket ack;
                    buildHeader(ack, MsgType::ACK, cmd, pkt->src_mac);
                    ack.seq    = pkt->seq;
                    ack.status = cached;
                    logPacket("TX ACK~", &ack);
                    sendPacketTo(pkt->src_mac, ack);
                    break;
                }
            }

            AckStatus ack_status = AckStatus::UNKNOWN_CMD;
            if (_cmd_handler) {
                ack_status = _cmd_handler(cmd, pkt->payload, pkt->payload_len,
                                          pkt->src_mac);
            }

            if (!is_broadcast) {
                storeAckStatus(pkt->src_mac, (uint8_t)ack_status);

                MessagePacket ack;
                buildHeader(ack, MsgType::ACK, cmd, pkt->src_mac);
                ack.seq    = pkt->seq;      /* echo the command's seq */
                ack.status = (uint8_t)ack_status;
                logPacket("TX ACK", &ack);
                sendPacketTo(pkt->src_mac, ack);
            }
            /* Broadcast commands: no ACK (prevents N-device ACK storms) */
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
            /* Resolve a pending reliable send, if any */
            bool   matched = false;
            CmdId  pending_cmd = CmdId::NONE;
            uint8_t pending_dst[6] = {};

            lock();
            for (int i = 0; i < MSG_PENDING_SLOTS; i++) {
                PendingTx& s = _pending[i];
                if (s.used && s.pkt.seq == pkt->seq &&
                    memcmp(s.pkt.dst_mac, pkt->src_mac, 6) == 0) {
                    matched     = true;
                    pending_cmd = (CmdId)s.pkt.cmd_id;
                    memcpy(pending_dst, s.pkt.dst_mac, 6);
                    s.used = false;
                    break;
                }
            }
            unlock();

            if (matched && _delivery_cb) {
                _delivery_cb(pkt->seq, pending_cmd, true,
                             (AckStatus)pkt->status, pending_dst);
            }
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
 * HANDLERS / STATUS
 * ========================================================================== */

void MessageProtocol::setCommandHandler(MsgCommandHandler handler)  { _cmd_handler   = handler; }
void MessageProtocol::setQueryHandler(MsgQueryHandler handler)      { _query_handler = handler; }
void MessageProtocol::setStateHandler(MsgStateHandler handler)      { _state_handler = handler; }
void MessageProtocol::setAckHandler(MsgAckHandler handler)          { _ack_handler   = handler; }
void MessageProtocol::setDeliveryCallback(MsgDeliveryCb cb)         { _delivery_cb   = cb; }

uint8_t MessageProtocol::pendingCount() {
    uint8_t n = 0;
    lock();
    for (int i = 0; i < MSG_PENDING_SLOTS; i++) {
        if (_pending[i].used) n++;
    }
    unlock();
    return n;
}

/* =============================================================================
 * DEBUG LOGGING
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
        case CmdId::SET_LIGHT_STATE: return "SET_LIGHT";
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
        case CmdId::REPORT_LIGHT_STATE: return "RPT_LIGHT";
        case CmdId::GARAGE_OPEN:     return "GARAGE_OPEN";
        case CmdId::GARAGE_CLOSE:    return "GARAGE_CLOSE";
        case CmdId::GARAGE_STOP:     return "GARAGE_STOP";
        case CmdId::PAIR_REQUEST:    return "PAIR_REQ";
        case CmdId::PAIR_ACCEPT:     return "PAIR_ACCEPT";
        case CmdId::PAIR_REJECT:     return "PAIR_REJECT";
        case CmdId::PAIR_UNPAIR:     return "PAIR_UNPAIR";
        default:                     return "CUSTOM";
    }
}

void MessageProtocol::logPacket(const char* prefix, const MessagePacket* pkt) {
    MsgType type = (MsgType)pkt->msg_type;
    CmdId   cmd  = (CmdId)pkt->cmd_id;

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

    char payload_str[40] = {};
    if (pkt->payload_len > 0) {
        int pos = 0;
        for (uint8_t i = 0; i < pkt->payload_len && i < 8; i++) {
            pos += snprintf(payload_str + pos, sizeof(payload_str) - pos,
                            "%02X ", pkt->payload[i]);
        }
    }

    ESP_LOGI(TAG, "%s %-5s %-12s%s seq=%u  %02X:%02X→%02X:%02X %s",
             prefix,
             typeName(type),
             cmdName(cmd),
             extra,
             pkt->seq,
             pkt->src_mac[4], pkt->src_mac[5],
             pkt->dst_mac[4], pkt->dst_mac[5],
             payload_str);
}