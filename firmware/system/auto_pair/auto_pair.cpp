/*
 * =============================================================================
 * FILE:        auto_pair.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-07-13
 * VERSION:     2.0.0
 * =============================================================================
 *
 * Locking rule used throughout this file:
 *   - mutate internal state while holding _mutex
 *   - copy whatever the callback needs onto the stack
 *   - RELEASE the mutex, THEN fire callbacks / send packets
 * This prevents deadlocks when a callback re-enters AutoPair
 * (e.g. the pair-request callback immediately calling acceptDevice()).
 * =============================================================================
 */

#include "auto_pair.h"
#include "esp_timer.h"
#include "esp_mac.h"

static const char* TAG = "AutoPair";

/* CmdId aliases — pairing rides on the CUSTOM_* range for now.
 * TODO: promote to dedicated PAIR_* entries in message_protocol.h
 * once the protocol header gets its next version bump. */
static const CmdId CMD_PAIR_REQUEST = CmdId::PAIR_REQUEST;
static const CmdId CMD_PAIR_ACCEPT  = CmdId::PAIR_ACCEPT;
static const CmdId CMD_PAIR_REJECT  = CmdId::PAIR_REJECT;

/* NVS keys (max 15 chars) */
static const char* NVS_DEV_PAIRED   = "ap_paired";      /* bool            */
static const char* NVS_DEV_CTRL_MAC = "ap_ctrl_mac";    /* blob, 6 bytes   */
static const char* NVS_CTRL_DEVICES = "ap_devs";        /* blob, N×23 B    */

/* Packed on-flash record for the controller's paired list */
struct __attribute__((packed)) PairedRecord {
    uint8_t mac[6];
    uint8_t role;
    char    name[DEVICE_NAME_LEN];
};
static_assert(sizeof(PairedRecord) == 6 + 1 + DEVICE_NAME_LEN,
              "PairedRecord layout changed — bump NVS key name");

/* =============================================================================
 * SINGLETON / CTOR
 * ========================================================================== */

AutoPair& AutoPair::instance() {
    static AutoPair inst;
    return inst;
}

AutoPair::AutoPair()
    : _is_controller(false)
    , _role(DeviceRole::UNKNOWN)
    , _state(PairState::UNPAIRED)
    , _last_request_us(0)
    , _request_count(0)
    , _pending_count(0)
    , _paired_count(0)
    , _request_cb(nullptr)
    , _result_cb(nullptr)
    , _led_cb(nullptr)
    , _peer_add_cb(nullptr)
    , _peer_remove_cb(nullptr)
{
    memset(_name, 0, sizeof(_name));
    memset(_self_mac, 0, sizeof(_self_mac));
    memset(_controller_mac, 0, sizeof(_controller_mac));
    memset(_pending, 0, sizeof(_pending));
    memset(_paired, 0, sizeof(_paired));
    _mutex = xSemaphoreCreateMutex();
}

AutoPair::~AutoPair() {
    if (_mutex) vSemaphoreDelete(_mutex);
}

void AutoPair::lock() const   { xSemaphoreTake(_mutex, portMAX_DELAY); }
void AutoPair::unlock() const { xSemaphoreGive(_mutex); }

bool AutoPair::handlesCmd(CmdId cmd) {
    return cmd == CmdId::PAIR_REQUEST ||
           cmd == CmdId::PAIR_ACCEPT  ||
           cmd == CmdId::PAIR_REJECT  ||
           cmd == CmdId::PAIR_UNPAIR;   /* reserved, routed but ignored for now */
}

/* =============================================================================
 * DEVICE SIDE — LIFECYCLE
 * ========================================================================== */

esp_err_t AutoPair::begin(DeviceRole role, const char* name) {
    lock();
    _is_controller = false;
    _role = role;
    strncpy(_name, name ? name : "", DEVICE_NAME_LEN - 1);
    _name[DEVICE_NAME_LEN - 1] = '\0';
    esp_read_mac(_self_mac, ESP_MAC_WIFI_STA);

    loadDevicePairing();

    bool paired = (_state == PairState::PAIRED);
    uint8_t ctrl[6];
    memcpy(ctrl, _controller_mac, 6);

    if (!paired) {
        _state = PairState::REQUESTING;
        _last_request_us = 0;       /* first update() fires immediately */
        _request_count = 0;
    }
    unlock();

    if (paired) {
        ESP_LOGI(TAG, "Already paired to %02X:%02X:%02X:%02X:%02X:%02X",
                 ctrl[0], ctrl[1], ctrl[2], ctrl[3], ctrl[4], ctrl[5]);
        if (_peer_add_cb) _peer_add_cb(ctrl);   /* replay peer registration */
        if (_led_cb)      _led_cb(PairLED::OFF);
    } else {
        ESP_LOGI(TAG, "Not paired — broadcasting pair requests as \"%s\"", _name);
        if (_led_cb) _led_cb(PairLED::FAST_BLINK);
    }
    return ESP_OK;
}

bool AutoPair::isPaired() const {
    lock();
    bool p = (_state == PairState::PAIRED);
    unlock();
    return p;
}

PairState AutoPair::getState() const {
    lock();
    PairState s = _state;
    unlock();
    return s;
}

const uint8_t* AutoPair::getControllerMAC() const { return _controller_mac; }

void AutoPair::unpair() {
    lock();
    uint8_t old_ctrl[6];
    memcpy(old_ctrl, _controller_mac, 6);
    bool was_paired = (_state == PairState::PAIRED);

    eraseDevicePairing();
    memset(_controller_mac, 0, 6);
    _state = PairState::REQUESTING;
    _last_request_us = 0;
    _request_count = 0;
    unlock();

    ESP_LOGI(TAG, "Unpaired — searching for a controller again");
    if (was_paired && _peer_remove_cb) _peer_remove_cb(old_ctrl);
    if (_led_cb) _led_cb(PairLED::FAST_BLINK);
}

/* =============================================================================
 * CONTROLLER SIDE — LIFECYCLE
 * ========================================================================== */

esp_err_t AutoPair::beginAsController() {
    lock();
    _is_controller = true;
    _state = PairState::PAIRED;     /* a controller is always "paired" */
    esp_read_mac(_self_mac, ESP_MAC_WIFI_STA);

    loadPairedList();

    /* Snapshot for peer replay outside the lock */
    uint8_t count = _paired_count;
    uint8_t macs[AUTOPAIR_MAX_PAIRED][6];
    for (uint8_t i = 0; i < count; i++) memcpy(macs[i], _paired[i].mac, 6);
    unlock();

    ESP_LOGI(TAG, "Controller mode — %u paired device(s) loaded from NVS", count);
    if (_peer_add_cb) {
        for (uint8_t i = 0; i < count; i++) _peer_add_cb(macs[i]);
    }
    return ESP_OK;
}

esp_err_t AutoPair::acceptDevice(const uint8_t mac[6]) {
    lock();
    if (!_is_controller) { unlock(); return ESP_ERR_INVALID_STATE; }

    /* Drop from pending if present */
    int pi = findPendingLocked(mac);
    PairRequestInfo info = {};
    bool had_info = (pi >= 0);
    if (had_info) {
        info = _pending[pi];
        removePendingLocked(pi);
    }

    /* Already paired? Just re-confirm. */
    if (findPairedLocked(mac) < 0) {
        if (_paired_count >= AUTOPAIR_MAX_PAIRED) {
            unlock();
            ESP_LOGE(TAG, "Paired list full (%d) — rejecting", AUTOPAIR_MAX_PAIRED);
            sendPairResponse(mac, false);
            return ESP_ERR_NO_MEM;
        }
        PairedDevice& d = _paired[_paired_count++];
        memcpy(d.mac, mac, 6);
        d.role = had_info ? info.role : DeviceRole::UNKNOWN;
        memset(d.name, 0, sizeof(d.name));
        if (had_info) strncpy(d.name, info.name, DEVICE_NAME_LEN - 1);
        savePairedList();
    }
    unlock();

    /* Peer BEFORE sending, so the accept goes out as true unicast */
    if (_peer_add_cb) _peer_add_cb(mac);
    sendPairResponse(mac, true);

    ESP_LOGI(TAG, "Accepted %02X:%02X:%02X:%02X:%02X:%02X (\"%s\")",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             had_info ? info.name : "?");
    return ESP_OK;
}

esp_err_t AutoPair::rejectDevice(const uint8_t mac[6]) {
    lock();
    if (!_is_controller) { unlock(); return ESP_ERR_INVALID_STATE; }
    int pi = findPendingLocked(mac);
    if (pi >= 0) removePendingLocked(pi);
    unlock();

    /* No peer needed — MessageProtocol falls back to broadcast,
     * and dst-MAC filtering makes sure only the target reacts. */
    sendPairResponse(mac, false);
    ESP_LOGI(TAG, "Rejected %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ESP_OK;
}

/* ─── Pending accessors ──────────────────────────────────────────────────── */

uint8_t AutoPair::getPendingCount() const {
    lock();
    uint8_t n = _pending_count;
    unlock();
    return n;
}

const PairRequestInfo* AutoPair::getPending(uint8_t index) const {
    return (index < _pending_count) ? &_pending[index] : nullptr;
}

/* ─── Paired-list accessors ──────────────────────────────────────────────── */

uint8_t AutoPair::getPairedCount() const {
    lock();
    uint8_t n = _paired_count;
    unlock();
    return n;
}

const PairedDevice* AutoPair::getPairedDevice(uint8_t index) const {
    return (index < _paired_count) ? &_paired[index] : nullptr;
}

bool AutoPair::isDevicePaired(const uint8_t mac[6]) const {
    lock();
    bool found = (findPairedLocked(mac) >= 0);
    unlock();
    return found;
}

esp_err_t AutoPair::forgetDevice(const uint8_t mac[6]) {
    lock();
    int i = findPairedLocked(mac);
    if (i < 0) { unlock(); return ESP_ERR_NOT_FOUND; }

    uint8_t gone[6];
    memcpy(gone, _paired[i].mac, 6);
    for (uint8_t j = i; j + 1 < _paired_count; j++) _paired[j] = _paired[j + 1];
    _paired_count--;
    savePairedList();
    unlock();

    if (_peer_remove_cb) _peer_remove_cb(gone);
    ESP_LOGI(TAG, "Forgot device %02X:%02X:%02X:%02X:%02X:%02X",
             gone[0], gone[1], gone[2], gone[3], gone[4], gone[5]);
    return ESP_OK;
}

void AutoPair::forgetAll() {
    lock();
    uint8_t count = _paired_count;
    uint8_t macs[AUTOPAIR_MAX_PAIRED][6];
    for (uint8_t i = 0; i < count; i++) memcpy(macs[i], _paired[i].mac, 6);
    _paired_count = 0;
    savePairedList();
    unlock();

    if (_peer_remove_cb) {
        for (uint8_t i = 0; i < count; i++) _peer_remove_cb(macs[i]);
    }
    ESP_LOGI(TAG, "Forgot all %u paired device(s)", count);
}

/* =============================================================================
 * UPDATE LOOP
 * ========================================================================== */

void AutoPair::update() {
    int64_t now = esp_timer_get_time();

    lock();
    if (_is_controller) {
        /* Expire stale pending requests (device powered off / gave up) */
        for (int i = (int)_pending_count - 1; i >= 0; i--) {
            if (now - _pending[i].first_seen_us > AUTOPAIR_PENDING_EXPIRY_US) {
                ESP_LOGI(TAG, "Pending request from \"%s\" expired",
                         _pending[i].name);
                removePendingLocked(i);
            }
        }
        unlock();
        return;
    }

    /* Device side */
    bool do_request = false;

    if (_state == PairState::REJECTED) {
        if (now - _last_request_us > AUTOPAIR_REJECT_COOLDOWN_US) {
            _state = PairState::REQUESTING;
            _request_count = 0;
            _last_request_us = 0;
            ESP_LOGI(TAG, "Rejection cooldown over — requesting again");
        }
    }

    if (_state == PairState::REQUESTING) {
        int64_t interval = (_request_count < AUTOPAIR_REQ_SLOW_AFTER)
                               ? AUTOPAIR_REQ_INTERVAL_US
                               : AUTOPAIR_REQ_SLOW_US;
        if (now - _last_request_us > interval) {
            _last_request_us = now;
            _request_count++;
            do_request = true;
            if (_request_count == AUTOPAIR_REQ_SLOW_AFTER) {
                ESP_LOGW(TAG, "No controller after %d tries — slowing to 30 s",
                         AUTOPAIR_REQ_SLOW_AFTER);
            }
        }
    }
    unlock();

    if (do_request) sendPairRequest();
}

/* =============================================================================
 * MESSAGE HANDLING (entry point from MessageProtocol command handler)
 * ========================================================================== */

void AutoPair::processPairMessage(CmdId cmd, const uint8_t* payload,
                                  uint8_t len, const uint8_t src_mac[6]) {
    if (cmd == CMD_PAIR_REQUEST && _is_controller) {
        onPairRequest(payload, len, src_mac);
    } else if (cmd == CMD_PAIR_ACCEPT && !_is_controller) {
        onPairAccept(src_mac);
    } else if (cmd == CMD_PAIR_REJECT && !_is_controller) {
        onPairReject(src_mac);
    }
    /* Anything else (e.g. a controller receiving an ACCEPT) is ignored. */
}

/* ─── Controller: incoming PAIR_REQUEST ──────────────────────────────────── */

void AutoPair::onPairRequest(const uint8_t* payload, uint8_t len,
                             const uint8_t src_mac[6]) {
    if (len < 2) return;    /* need at least role + fw_major */

    lock();

    /* Known device re-requesting (re-flashed / lost its NVS)?
     * Re-accept silently — no popup, no user interaction. */
    int paired_idx = findPairedLocked(src_mac);
    if (paired_idx >= 0) {
        /* Refresh stored identity in case the firmware changed it */
        _paired[paired_idx].role = (DeviceRole)payload[0];
        if (len > 2) {
            uint8_t nl = len - 2;
            if (nl > DEVICE_NAME_LEN - 1) nl = DEVICE_NAME_LEN - 1;
            memset(_paired[paired_idx].name, 0, DEVICE_NAME_LEN);
            memcpy(_paired[paired_idx].name, payload + 2, nl);
            savePairedList();
        }
        unlock();

        ESP_LOGI(TAG, "Known device re-requesting — auto re-accepting");
        if (_peer_add_cb) _peer_add_cb(src_mac);
        sendPairResponse(src_mac, true);
        return;
    }

    /* Already pending? Refresh its timestamp, don't fire a second popup. */
    int pi = findPendingLocked(src_mac);
    if (pi >= 0) {
        _pending[pi].first_seen_us = esp_timer_get_time();
        unlock();
        return;
    }

    if (_pending_count >= AUTOPAIR_MAX_PENDING) {
        unlock();
        ESP_LOGW(TAG, "Pending queue full — ignoring request");
        return;
    }

    /* New device — record it */
    PairRequestInfo& slot = _pending[_pending_count++];
    memset(&slot, 0, sizeof(slot));
    memcpy(slot.mac, src_mac, 6);
    slot.role     = (DeviceRole)payload[0];
    slot.fw_major = payload[1];
    slot.first_seen_us = esp_timer_get_time();
    if (len > 2) {
        uint8_t nl = len - 2;
        if (nl > DEVICE_NAME_LEN - 1) nl = DEVICE_NAME_LEN - 1;
        memcpy(slot.name, payload + 2, nl);
    }
    PairRequestInfo copy = slot;    /* stack copy for the callback */
    unlock();

    ESP_LOGI(TAG, "╔═ PAIR REQUEST ═══════════════════════════════╗");
    ESP_LOGI(TAG, "║  \"%s\" (%02X:%02X:%02X:%02X:%02X:%02X) role=%u",
             copy.name, copy.mac[0], copy.mac[1], copy.mac[2],
             copy.mac[3], copy.mac[4], copy.mac[5], (unsigned)copy.role);
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════╝");

    if (_request_cb) _request_cb(&copy);
}

/* ─── Device: incoming PAIR_ACCEPT ───────────────────────────────────────── */

void AutoPair::onPairAccept(const uint8_t src_mac[6]) {
    lock();
    if (_state == PairState::PAIRED) {
        bool same = (memcmp(_controller_mac, src_mac, 6) == 0);
        unlock();
        if (!same) {
            ESP_LOGW(TAG, "ACCEPT from a second controller — ignoring. "
                          "unpair() first to switch controllers.");
        }
        return;     /* duplicate accept from own controller: silently drop */
    }

    memcpy(_controller_mac, src_mac, 6);
    _state = PairState::PAIRED;
    saveDevicePairing();
    uint8_t ctrl[6];
    memcpy(ctrl, _controller_mac, 6);
    unlock();

    ESP_LOGI(TAG, "╔═ PAIRED ═════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Controller: %02X:%02X:%02X:%02X:%02X:%02X",
             ctrl[0], ctrl[1], ctrl[2], ctrl[3], ctrl[4], ctrl[5]);
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════╝");

    if (_peer_add_cb) _peer_add_cb(ctrl);
    if (_led_cb)      _led_cb(PairLED::SOLID_ON);
    if (_result_cb)   _result_cb(true, ctrl);
}

/* ─── Device: incoming PAIR_REJECT ───────────────────────────────────────── */

void AutoPair::onPairReject(const uint8_t src_mac[6]) {
    lock();
    if (_state == PairState::PAIRED) { unlock(); return; }
    _state = PairState::REJECTED;
    _last_request_us = esp_timer_get_time();
    unlock();

    ESP_LOGW(TAG, "Pair request rejected — retrying in %lld s",
             AUTOPAIR_REJECT_COOLDOWN_US / 1000000LL);
    if (_led_cb)    _led_cb(PairLED::TRIPLE_BLINK);
    if (_result_cb) _result_cb(false, src_mac);
}

/* =============================================================================
 * TX
 * ========================================================================== */

void AutoPair::sendPairRequest() {
    /* Payload: [role(1), fw_major(1), name(0-8)] — MSG_PAYLOAD_MAX is 10 */
    uint8_t payload[MSG_PAYLOAD_MAX];
    payload[0] = (uint8_t)_role;
    payload[1] = ConfigStore::instance().getU8(ConfigKeys::FW_MAJOR, 1);

    uint8_t name_len = strlen(_name);
    if (name_len > MSG_PAYLOAD_MAX - 2) name_len = MSG_PAYLOAD_MAX - 2;
    memcpy(payload + 2, _name, name_len);

    MessageProtocol::instance().broadcastCommand(CMD_PAIR_REQUEST,
                                                 payload, 2 + name_len);
    ESP_LOGD(TAG, "Pair request #%lu sent", (unsigned long)_request_count);
}

void AutoPair::sendPairResponse(const uint8_t dst_mac[6], bool accept) {
    MessageProtocol::instance().sendCommand(
        dst_mac, accept ? CMD_PAIR_ACCEPT : CMD_PAIR_REJECT);
}

/* =============================================================================
 * NVS PERSISTENCE
 * ========================================================================== */

void AutoPair::loadDevicePairing() {
    ConfigStore& cfg = ConfigStore::instance();
    if (cfg.getBool(NVS_DEV_PAIRED, false)) {
        size_t len = 6;
        if (cfg.getBlob(NVS_DEV_CTRL_MAC, _controller_mac, &len) == ESP_OK
            && len == 6) {
            _state = PairState::PAIRED;
            return;
        }
        ESP_LOGW(TAG, "Pairing flag set but MAC blob invalid — resetting");
        eraseDevicePairing();
    }
    _state = PairState::UNPAIRED;
}

void AutoPair::saveDevicePairing() {
    ConfigStore& cfg = ConfigStore::instance();
    cfg.setBlob(NVS_DEV_CTRL_MAC, _controller_mac, 6);
    cfg.setBool(NVS_DEV_PAIRED, true);
}

void AutoPair::eraseDevicePairing() {
    ConfigStore& cfg = ConfigStore::instance();
    cfg.eraseKey(NVS_DEV_PAIRED);
    cfg.eraseKey(NVS_DEV_CTRL_MAC);
}

void AutoPair::loadPairedList() {
    ConfigStore& cfg = ConfigStore::instance();
    PairedRecord recs[AUTOPAIR_MAX_PAIRED];
    size_t len = sizeof(recs);

    _paired_count = 0;
    if (cfg.getBlob(NVS_CTRL_DEVICES, recs, &len) != ESP_OK) return;

    uint8_t count = len / sizeof(PairedRecord);
    if (count > AUTOPAIR_MAX_PAIRED) count = AUTOPAIR_MAX_PAIRED;

    for (uint8_t i = 0; i < count; i++) {
        memcpy(_paired[i].mac, recs[i].mac, 6);
        _paired[i].role = (DeviceRole)recs[i].role;
        memcpy(_paired[i].name, recs[i].name, DEVICE_NAME_LEN);
        _paired[i].name[DEVICE_NAME_LEN - 1] = '\0';
    }
    _paired_count = count;
}

void AutoPair::savePairedList() {
    ConfigStore& cfg = ConfigStore::instance();

    if (_paired_count == 0) {
        cfg.eraseKey(NVS_CTRL_DEVICES);
        return;
    }

    PairedRecord recs[AUTOPAIR_MAX_PAIRED] = {};
    for (uint8_t i = 0; i < _paired_count; i++) {
        memcpy(recs[i].mac, _paired[i].mac, 6);
        recs[i].role = (uint8_t)_paired[i].role;
        memcpy(recs[i].name, _paired[i].name, DEVICE_NAME_LEN);
    }
    cfg.setBlob(NVS_CTRL_DEVICES, recs,
                _paired_count * sizeof(PairedRecord));
}

/* =============================================================================
 * LOCKED HELPERS (caller holds _mutex)
 * ========================================================================== */

int AutoPair::findPendingLocked(const uint8_t mac[6]) const {
    for (uint8_t i = 0; i < _pending_count; i++) {
        if (memcmp(_pending[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

void AutoPair::removePendingLocked(int index) {
    if (index < 0 || index >= _pending_count) return;
    for (uint8_t j = index; j + 1 < _pending_count; j++) {
        _pending[j] = _pending[j + 1];
    }
    _pending_count--;
}

int AutoPair::findPairedLocked(const uint8_t mac[6]) const {
    for (uint8_t i = 0; i < _paired_count; i++) {
        if (memcmp(_paired[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

/* =============================================================================
 * CALLBACK SETTERS
 * ========================================================================== */

void AutoPair::setPairRequestCallback(PairRequestCb cb) { _request_cb = cb; }
void AutoPair::setPairResultCallback(PairResultCb cb)   { _result_cb  = cb; }
void AutoPair::setLEDCallback(PairLEDCb cb)             { _led_cb     = cb; }
void AutoPair::setPeerAddCallback(PeerAddCb cb)         { _peer_add_cb = cb; }
void AutoPair::setPeerRemoveCallback(PeerRemoveCb cb)   { _peer_remove_cb = cb; }
