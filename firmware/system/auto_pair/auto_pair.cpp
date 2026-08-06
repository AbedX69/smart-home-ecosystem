/*
 * =============================================================================
 * FILE:        auto_pair.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-07-13
 * MODIFIED:    2026-07-27
 * VERSION:     3.0.0
 * =============================================================================
 *
 * Locking rule used throughout this file:
 *   - mutate internal state while holding _mutex
 *   - copy whatever the callback needs onto the stack
 *   - RELEASE the mutex, THEN fire callbacks / send packets
 *
 * In v3 this rule is load-bearing rather than merely tidy: MessageProtocol
 * calls back into resolveUid() from inside every unicast send, and
 * resolveUid() takes _mutex. Sending a packet while holding _mutex would
 * deadlock instantly. Every send site in this file unlocks first.
 * =============================================================================
 */

#include "auto_pair.h"
#include "esp_timer.h"

static const char* TAG = "AutoPair";

/* NVS keys (max 15 chars) */
static const char* NVS_DEV_PAIRED   = "ap_paired";      /* bool            */
static const char* NVS_DEV_CTRL_MAC = "ap_ctrl_mac";    /* blob, 6 bytes   */
static const char* NVS_DEV_CTRL_UID = "ap_ctrl_uid";    /* u32             */
static const char* NVS_CTRL_DEVICES = "ap_devs2";       /* blob, N×29 B    */

/* Packed on-flash record for the controller's paired list.
 * Layout changed in v3 → new key name, old "ap_devs" is simply orphaned. */
struct __attribute__((packed)) PairedRecord {
    uint32_t uid;
    uint8_t  mac[6];
    uint8_t  role;
    uint8_t  room;
    uint8_t  node;
    char     name[DEVICE_NAME_LEN];
};
static_assert(sizeof(PairedRecord) == 4 + 6 + 1 + 1 + 1 + DEVICE_NAME_LEN,
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
    , _self_uid(UID_NONE)
    , _state(PairState::UNPAIRED)
    , _controller_uid(UID_NONE)
    , _last_request_us(0)
    , _request_count(0)
    , _pending_count(0)
    , _paired_count(0)
    , _request_cb(nullptr)
    , _result_cb(nullptr)
    , _led_cb(nullptr)
    , _channel_cb(nullptr)
    , _channel(0)
    , _last_contact_us(0)
    , _sweeping(false)
    , _sweep_ch(AUTOPAIR_SWEEP_MIN_CH)
    , _sweep_last_us(0)
    , _pending_channel(0)
    , _pending_apply_us(0)
    , _peer_add_cb(nullptr)
    , _peer_remove_cb(nullptr)
{
    memset(_name, 0, sizeof(_name));
    memset(_controller_mac, 0, sizeof(_controller_mac));
    memset(_pending, 0, sizeof(_pending));
    memset(_paired, 0, sizeof(_paired));
    memset(_addr, 0, sizeof(_addr));
    memset(_reloc, 0, sizeof(_reloc));
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
           cmd == CmdId::PAIR_UNPAIR  ||
           cmd == CmdId::SET_LOCATION ||
           cmd == CmdId::SET_CHANNEL;
}

/* =============================================================================
 * ADDRESS TABLE
 * =============================================================================
 * The one place in the ecosystem that still knows MACs exist. Seeded from
 * NVS at begin() so unicast works immediately after a reboot, then kept
 * fresh by observation of inbound traffic.
 *
 * A miss is not a failure — MessageProtocol falls back to broadcast and the
 * receiver filters on dst_uid. That is what lets pairing happen before any
 * mapping exists, and what makes a stale entry self-correct instead of
 * bricking communication.
 * ========================================================================== */

int AutoPair::findAddrLocked(DeviceUid uid) const {
    for (int i = 0; i < AUTOPAIR_ADDR_ENTRIES; i++) {
        if (_addr[i].used && _addr[i].uid == uid) return i;
    }
    return -1;
}

void AutoPair::noteAddressLocked(DeviceUid uid, const uint8_t mac[6]) {
    if (uid == UID_NONE || !mac) return;
    int64_t now = esp_timer_get_time();

    int i = findAddrLocked(uid);
    if (i >= 0) {
        if (memcmp(_addr[i].mac, mac, 6) != 0) {
            ESP_LOGW(TAG, "UID %08X moved radio address", (unsigned)uid);
            memcpy(_addr[i].mac, mac, 6);
        }
        _addr[i].last_seen_us = now;
        return;
    }

    /* Free slot, else evict least recently seen */
    int slot = -1;
    int oldest = 0;
    for (int j = 0; j < AUTOPAIR_ADDR_ENTRIES; j++) {
        if (!_addr[j].used) { slot = j; break; }
        if (_addr[j].last_seen_us < _addr[oldest].last_seen_us) oldest = j;
    }
    if (slot < 0) slot = oldest;

    _addr[slot].used         = true;
    _addr[slot].uid          = uid;
    memcpy(_addr[slot].mac, mac, 6);
    _addr[slot].last_seen_us = now;
}

void AutoPair::noteAddress(DeviceUid uid, const uint8_t mac[6]) {
    bool    found  = false;
    uint8_t on_ch  = 0;

    lock();
    noteAddressLocked(uid, mac);

    /* Any packet from our controller is proof of contact. This fires on
     * every received message, so the sweep costs no extra traffic. */
    if (!_is_controller && uid != UID_NONE && uid == _controller_uid) {
        _last_contact_us = esp_timer_get_time();
        if (_sweeping) {
            _sweeping = false;
            found     = true;
            on_ch     = _channel;
        }
    }
    unlock();

    if (found) {
        ESP_LOGI(TAG, "Controller found on channel %u - persisting",
                 (unsigned)on_ch);
        ConfigStore::instance().setU8(ConfigKeys::WIFI_CHANNEL, on_ch);
    }
}

bool AutoPair::resolveUid(DeviceUid uid, uint8_t out_mac[6]) const {
    if (uid == UID_NONE || !out_mac) return false;
    lock();
    int i = findAddrLocked(uid);
    if (i >= 0) {
        memcpy(out_mac, _addr[i].mac, 6);
        unlock();
        return true;
    }
    unlock();
    return false;
}

/* =============================================================================
 * DEVICE SIDE — LIFECYCLE
 * ========================================================================== */

esp_err_t AutoPair::begin(DeviceRole role, const char* name) {
    DeviceIdentity& id = DeviceIdentity::instance();
    if (!id.isReady()) {
        ESP_LOGE(TAG, "DeviceIdentity::begin() must succeed first");
        return ESP_ERR_INVALID_STATE;
    }

    lock();
    _is_controller = false;
    _role = role;
    strncpy(_name, name ? name : "", DEVICE_NAME_LEN - 1);
    _name[DEVICE_NAME_LEN - 1] = '\0';
    _self_uid = id.uid();

    loadDevicePairing();

    bool paired = (_state == PairState::PAIRED);
    DeviceUid ctrl_uid = _controller_uid;
    uint8_t   ctrl_mac[6];
    memcpy(ctrl_mac, _controller_mac, 6);

    if (paired) {
        /* Seed the address table so the first unicast doesn't have to
         * broadcast while waiting to hear from the controller. */
        noteAddressLocked(ctrl_uid, ctrl_mac);
    } else {
        _state = PairState::REQUESTING;
        _last_request_us = 0;       /* first update() fires immediately */
        _request_count = 0;
    }
    unlock();

    if (paired) {
        ESP_LOGI(TAG, "Already paired to controller %08X (house 0x%04X "
                      "room %u node %u)",
                 (unsigned)ctrl_uid, (unsigned)id.house(),
                 (unsigned)id.room(), (unsigned)id.node());
        if (_peer_add_cb) _peer_add_cb(ctrl_mac);   /* replay peer registration */
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

DeviceUid AutoPair::getControllerUid() const {
    lock();
    DeviceUid u = (_state == PairState::PAIRED) ? _controller_uid : UID_NONE;
    unlock();
    return u;
}

const uint8_t* AutoPair::getControllerMAC() const { return _controller_mac; }

void AutoPair::unpair(bool clear_location) {
    lock();
    uint8_t old_ctrl[6];
    memcpy(old_ctrl, _controller_mac, 6);
    bool was_paired = (_state == PairState::PAIRED);

    int ai = findAddrLocked(_controller_uid);
    if (ai >= 0) memset(&_addr[ai], 0, sizeof(_addr[ai]));

    eraseDevicePairing();
    memset(_controller_mac, 0, 6);
    _controller_uid  = UID_NONE;
    _state           = PairState::REQUESTING;
    _last_request_us = 0;
    _request_count   = 0;
    unlock();

    /* A device that has left its house must stop answering to it, or it
     * will keep accepting group commands from its old hub. */
    if (clear_location) DeviceIdentity::instance().clearLocation();

    ESP_LOGI(TAG, "Unpaired — searching for a controller again");
    if (was_paired && _peer_remove_cb) _peer_remove_cb(old_ctrl);
    if (_led_cb) _led_cb(PairLED::FAST_BLINK);
}

/* =============================================================================
 * CONTROLLER SIDE — LIFECYCLE
 * ========================================================================== */

esp_err_t AutoPair::beginAsController() {
    DeviceIdentity& id = DeviceIdentity::instance();
    if (!id.isReady()) {
        ESP_LOGE(TAG, "DeviceIdentity::begin() must succeed first");
        return ESP_ERR_INVALID_STATE;
    }

    /* A controller with no house that starts accepting pairings would stamp
     * house 0 into every child, and house 0 matches everything — the whole
     * installation would be adoptable by any hub in radio range. Mint one. */
    if (!id.isProvisioned()) {
        esp_err_t ret = id.provisionAsNewHouse();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Could not mint a house id: 0x%x", ret);
            return ret;
        }
        ESP_LOGI(TAG, "New installation provisioned — house 0x%04X",
                 (unsigned)id.house());
    }
    /* The controller occupies node 1 of its own room. */
    if (id.room() == ROOM_UNASSIGNED || id.node() == NODE_UNASSIGNED) {
        id.setLocation(id.house(),
                       id.room() == ROOM_UNASSIGNED ? 1 : id.room(),
                       1);
    }

    lock();
    _is_controller = true;
    _state         = PairState::PAIRED;     /* a controller is always "paired" */
    _self_uid      = id.uid();

    loadPairedList();

    /* Seed the address table from NVS, and snapshot for peer replay */
    uint8_t count = _paired_count;
    uint8_t macs[AUTOPAIR_MAX_PAIRED][6];
    for (uint8_t i = 0; i < count; i++) {
        memcpy(macs[i], _paired[i].mac, 6);
        noteAddressLocked(_paired[i].uid, _paired[i].mac);
    }
    unlock();

    ESP_LOGI(TAG, "Controller mode — house 0x%04X room %u node %u, "
                  "%u paired device(s) loaded from NVS",
             (unsigned)id.house(), (unsigned)id.room(),
             (unsigned)id.node(), count);
    if (_peer_add_cb) {
        for (uint8_t i = 0; i < count; i++) _peer_add_cb(macs[i]);
    }
    return ESP_OK;
}

/* Lowest unused node number in `room`, starting at AUTOPAIR_FIRST_NODE.
 * Caller holds _mutex. */
NodeId AutoPair::allocateNodeLocked(RoomId room) const {
    for (NodeId n = AUTOPAIR_FIRST_NODE; n < NODE_ALL; n++) {
        bool taken = false;
        for (uint8_t i = 0; i < _paired_count; i++) {
            if (_paired[i].room == room && _paired[i].node == n) {
                taken = true;
                break;
            }
        }
        if (!taken) return n;
    }
    return NODE_ALL - 1;    /* room full; overwrite the last slot */
}

/* Is (room, node) free? `except` is skipped -- that is the device being
 * moved. Checks committed records AND moves still awaiting confirmation,
 * so two relocations cannot be aimed at the same slot.
 * Caller holds _mutex. */
bool AutoPair::nodeFreeLocked(RoomId room, NodeId node, DeviceUid except) const {
    for (uint8_t i = 0; i < _paired_count; i++) {
        if (_paired[i].uid == except) continue;
        if (_paired[i].room == room && _paired[i].node == node) return false;
    }
    for (uint8_t i = 0; i < AUTOPAIR_MAX_RELOCATES; i++) {
        if (!_reloc[i].used || _reloc[i].uid == except) continue;
        if (_reloc[i].room == room && _reloc[i].node == node) return false;
    }
    return true;
}

/* Move a paired device, keeping our record in sync with the device.
 *
 * The record is NOT updated here. It is updated in noteDeliveryResult()
 * when the device ACKs. If the move never lands, both sides stay at the
 * old address -- a stale hub record is worse than a failed move, because
 * every subsequent sendCommandToGroup() silently misses the device. */
esp_err_t AutoPair::relocateDevice(DeviceUid uid, RoomId room, NodeId node) {
    DeviceIdentity& id = DeviceIdentity::instance();

    /* Read identity BEFORE locking -- nothing in this file calls out while
     * holding _mutex, and that rule is what keeps resolveUid() safe. */
    HouseId house     = id.house();
    RoomId  self_room = id.room();
    NodeId  self_node = id.node();

    if (room == ROOM_UNASSIGNED || room == ROOM_ALL) {
        ESP_LOGE(TAG, "relocate: room %u is not an address", (unsigned)room);
        return ESP_ERR_INVALID_ARG;
    }
    if (node == NODE_ALL) {
        ESP_LOGE(TAG, "relocate: node %u is not an address", (unsigned)node);
        return ESP_ERR_INVALID_ARG;
    }
    if (node != NODE_UNASSIGNED && room == self_room && node == self_node) {
        ESP_LOGE(TAG, "relocate: r%u/n%u is this controller",
                 (unsigned)room, (unsigned)node);
        return ESP_ERR_INVALID_ARG;
    }

    lock();
    if (!_is_controller) { unlock(); return ESP_ERR_INVALID_STATE; }

    int i = findPairedLocked(uid);
    if (i < 0) { unlock(); return ESP_ERR_NOT_FOUND; }

    for (uint8_t j = 0; j < AUTOPAIR_MAX_RELOCATES; j++) {
        if (_reloc[j].used && _reloc[j].uid == uid) {
            unlock();
            ESP_LOGW(TAG, "relocate: %08X already has a move in flight",
                     (unsigned)uid);
            return ESP_ERR_INVALID_STATE;
        }
    }

    NodeId target = node;
    if (node == NODE_UNASSIGNED) {
        /* Already in that room -> keep the number it has. */
        target = (_paired[i].room == room) ? _paired[i].node
                                           : allocateNodeLocked(room);
        /* allocateNodeLocked() returns NODE_ALL-1 on a full room, which
         * would collide silently. Verify rather than trust. */
        if (!nodeFreeLocked(room, target, uid)) {
            unlock();
            ESP_LOGE(TAG, "relocate: room %u is full", (unsigned)room);
            return ESP_ERR_NO_MEM;
        }
    } else if (!nodeFreeLocked(room, target, uid)) {
        unlock();
        ESP_LOGE(TAG, "relocate: r%u/n%u occupied -- refusing. Two devices "
                      "at one address makes group sends hit both.",
                 (unsigned)room, (unsigned)target);
        return ESP_ERR_INVALID_ARG;
    }

    int slot = -1;
    for (uint8_t j = 0; j < AUTOPAIR_MAX_RELOCATES; j++) {
        if (!_reloc[j].used) { slot = j; break; }
    }
    if (slot < 0) {
        unlock();
        ESP_LOGE(TAG, "relocate: %d moves already in flight",
                 AUTOPAIR_MAX_RELOCATES);
        return ESP_ERR_NO_MEM;
    }
    _reloc[slot].used = true;
    _reloc[slot].uid  = uid;
    _reloc[slot].room = room;
    _reloc[slot].node = target;

    RoomId old_room = _paired[i].room;
    NodeId old_node = _paired[i].node;
    unlock();

    /* Reliable on purpose. Do NOT call MessageProtocol::sendLocation() from
     * controller code -- it moves the device and leaves our record stale. */
    uint8_t p[4];
    msgEncodeLocation(p, house, room, target);
    esp_err_t ret = MessageProtocol::instance().sendCommandReliable(
        uid, CmdId::SET_LOCATION, p, 4);

    if (ret != ESP_OK) {
        lock();
        memset(&_reloc[slot], 0, sizeof(_reloc[slot]));
        unlock();
        ESP_LOGE(TAG, "relocate: send failed (0x%x)", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Moving %08X  r%u/n%u -> r%u/n%u  (awaiting confirmation)",
             (unsigned)uid, (unsigned)old_room, (unsigned)old_node,
             (unsigned)room, (unsigned)target);
    return ESP_OK;
}

/* Fed from the app's MessageProtocol delivery callback.
 *
 * Correlates on (cmd, dst_uid) rather than seq: the ACK can arrive before
 * sendCommandReliable() has even returned, so a seq stored afterwards would
 * race. One move per device at a time makes the uid a sufficient key. */
void AutoPair::noteDeliveryResult(CmdId cmd, DeviceUid dst_uid, bool delivered) {
    if (cmd != CmdId::SET_LOCATION) return;

    lock();
    int r = -1;
    for (uint8_t j = 0; j < AUTOPAIR_MAX_RELOCATES; j++) {
        if (_reloc[j].used && _reloc[j].uid == dst_uid) { r = (int)j; break; }
    }
    if (r < 0) { unlock(); return; }

    RoomId room = _reloc[r].room;
    NodeId node = _reloc[r].node;
    memset(&_reloc[r], 0, sizeof(_reloc[r]));

    if (!delivered) {
        unlock();
        ESP_LOGW(TAG, "Move of %08X to r%u/n%u NOT confirmed -- record "
                      "left unchanged", (unsigned)dst_uid,
                 (unsigned)room, (unsigned)node);
        return;
    }

    int i = findPairedLocked(dst_uid);
    if (i >= 0) {
        _paired[i].room = room;
        _paired[i].node = node;
        savePairedList();
    }
    unlock();

    ESP_LOGI(TAG, "Move of %08X confirmed -- now r%u/n%u",
             (unsigned)dst_uid, (unsigned)room, (unsigned)node);
}

esp_err_t AutoPair::acceptDevice(DeviceUid uid) {
    DeviceIdentity& id = DeviceIdentity::instance();

    lock();
    if (!_is_controller) { unlock(); return ESP_ERR_INVALID_STATE; }

    /* Drop from pending if present */
    int pi = findPendingLocked(uid);
    PairRequestInfo info = {};
    bool had_info = (pi >= 0);
    if (had_info) {
        info = _pending[pi];
        removePendingLocked(pi);
    }

    RoomId assign_room;
    NodeId assign_node;

    int already = findPairedLocked(uid);
    if (already >= 0) {
        /* Known device re-pairing — keep its existing address. */
        assign_room = _paired[already].room;
        assign_node = _paired[already].node;
    } else {
        if (_paired_count >= AUTOPAIR_MAX_PAIRED) {
            unlock();
            ESP_LOGE(TAG, "Paired list full (%d) — rejecting", AUTOPAIR_MAX_PAIRED);
            sendPairReject(uid);
            return ESP_ERR_NO_MEM;
        }
        assign_room = id.room();
        assign_node = allocateNodeLocked(assign_room);

        PairedDevice& d = _paired[_paired_count++];
        memset(&d, 0, sizeof(d));
        d.uid  = uid;
        d.role = had_info ? info.role : DeviceRole::UNKNOWN;
        d.room = assign_room;
        d.node = assign_node;
        if (had_info) strncpy(d.name, info.name, DEVICE_NAME_LEN - 1);

        int ai = findAddrLocked(uid);
        if (ai >= 0) memcpy(d.mac, _addr[ai].mac, 6);

        savePairedList();
    }

    uint8_t mac[6] = {};
    bool have_mac = false;
    int ai = findAddrLocked(uid);
    if (ai >= 0) { memcpy(mac, _addr[ai].mac, 6); have_mac = true; }

    char nm[DEVICE_NAME_LEN];
    strncpy(nm, had_info ? info.name : "?", DEVICE_NAME_LEN - 1);
    nm[DEVICE_NAME_LEN - 1] = '\0';
    unlock();

    /* Peer BEFORE sending, so the accept goes out as true unicast */
    if (have_mac && _peer_add_cb) _peer_add_cb(mac);
    sendPairAccept(uid, assign_room, assign_node);

    ESP_LOGI(TAG, "Accepted %08X (\"%s\") → house 0x%04X room %u node %u",
             (unsigned)uid, nm, (unsigned)id.house(),
             (unsigned)assign_room, (unsigned)assign_node);
    return ESP_OK;
}

esp_err_t AutoPair::rejectDevice(DeviceUid uid) {
    lock();
    if (!_is_controller) { unlock(); return ESP_ERR_INVALID_STATE; }
    int pi = findPendingLocked(uid);
    if (pi >= 0) removePendingLocked(pi);
    unlock();

    /* No peer needed — MessageProtocol falls back to broadcast, and
     * dst_uid filtering makes sure only the target reacts. */
    sendPairReject(uid);
    ESP_LOGI(TAG, "Rejected %08X", (unsigned)uid);
    return ESP_OK;
}

/* ─── Pending accessors ───────────────────────────────────────────────────── */

uint8_t AutoPair::getPendingCount() const {
    lock();
    uint8_t n = _pending_count;
    unlock();
    return n;
}

const PairRequestInfo* AutoPair::getPending(uint8_t index) const {
    return (index < _pending_count) ? &_pending[index] : nullptr;
}

/* ─── Paired-list accessors ───────────────────────────────────────────────── */

uint8_t AutoPair::getPairedCount() const {
    lock();
    uint8_t n = _paired_count;
    unlock();
    return n;
}

const PairedDevice* AutoPair::getPairedDevice(uint8_t index) const {
    return (index < _paired_count) ? &_paired[index] : nullptr;
}

bool AutoPair::isDevicePaired(DeviceUid uid) const {
    lock();
    bool found = (findPairedLocked(uid) >= 0);
    unlock();
    return found;
}

esp_err_t AutoPair::forgetDevice(DeviceUid uid) {
    lock();
    int i = findPairedLocked(uid);
    if (i < 0) { unlock(); return ESP_ERR_NOT_FOUND; }

    uint8_t gone[6];
    memcpy(gone, _paired[i].mac, 6);
    for (uint8_t j = 0; j < AUTOPAIR_MAX_RELOCATES; j++) {
        if (_reloc[j].used && _reloc[j].uid == uid) memset(&_reloc[j], 0, sizeof(_reloc[j]));
    }
    for (uint8_t j = i; j + 1 < _paired_count; j++) _paired[j] = _paired[j + 1];
    _paired_count--;
    savePairedList();

    int ai = findAddrLocked(uid);
    if (ai >= 0) memset(&_addr[ai], 0, sizeof(_addr[ai]));
    unlock();

    if (_peer_remove_cb) _peer_remove_cb(gone);
    ESP_LOGI(TAG, "Forgot device %08X", (unsigned)uid);
    return ESP_OK;
}

void AutoPair::forgetAll() {
    lock();
    uint8_t count = _paired_count;
    uint8_t macs[AUTOPAIR_MAX_PAIRED][6];
    for (uint8_t i = 0; i < count; i++) memcpy(macs[i], _paired[i].mac, 6);
    memset(_reloc, 0, sizeof(_reloc));
    _paired_count = 0;
    savePairedList();
    memset(_addr, 0, sizeof(_addr));
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
    /* A SET_CHANNEL that asked us to wait. Applied here rather than in the
     * handler so every node flips at the same moment instead of as their
     * ACKs trickle back to the hub. */
    uint8_t apply_now = 0;
    if (_pending_channel && now >= _pending_apply_us) {
        apply_now        = _pending_channel;
        _pending_channel = 0;
    }
    unlock();

    if (apply_now) applyChannel(apply_now, true);
    if (do_request) sendPairRequest();

    sweepTick(now);
}


/* =============================================================================
 * CHANNEL
 * ========================================================================== */

void AutoPair::applyChannel(uint8_t channel, bool persist) {
    if (channel < AUTOPAIR_SWEEP_MIN_CH || channel > AUTOPAIR_SWEEP_MAX_CH) {
        ESP_LOGW(TAG, "Ignoring bad channel %u", (unsigned)channel);
        return;
    }

    lock();
    _channel = channel;
    ChannelSetCb cb = _channel_cb;
    unlock();

    if (cb) cb(channel);        /* never call out under our own mutex */
    if (persist) ConfigStore::instance().setU8(ConfigKeys::WIFI_CHANNEL, channel);
    ESP_LOGI(TAG, "Channel %u applied%s", (unsigned)channel,
             persist ? " and saved" : "");
}

void AutoPair::onSetChannel(const uint8_t* payload, uint8_t len,
                            DeviceUid src_uid) {
    if (_is_controller || !payload || len < 3) return;

    /* Same authorisation rule as SET_LOCATION: only our own controller
     * may move us. Otherwise anything in radio range can strand a node. */
    lock();
    bool ok = (_state == PairState::PAIRED && src_uid == _controller_uid);
    unlock();
    if (!ok) {
        ESP_LOGW(TAG, "SET_CHANNEL from %08X ignored - not our controller",
                 (unsigned)src_uid);
        return;
    }

    uint8_t  ch    = payload[0];
    uint16_t delay = (uint16_t)(payload[1] | (payload[2] << 8));

    if (delay == 0) { applyChannel(ch, true); return; }

    lock();
    _pending_channel  = ch;
    _pending_apply_us = esp_timer_get_time() + (int64_t)delay * 1000LL;
    unlock();
    ESP_LOGI(TAG, "Channel %u scheduled in %u ms",
             (unsigned)ch, (unsigned)delay);
}

uint8_t AutoPair::announceChannel(uint8_t channel, uint16_t delay_ms) {
    if (!_is_controller) return 0;
    if (channel < AUTOPAIR_SWEEP_MIN_CH || channel > AUTOPAIR_SWEEP_MAX_CH) {
        return 0;
    }

    uint8_t pl[3] = { channel,
                      (uint8_t)(delay_ms & 0xFF),
                      (uint8_t)(delay_ms >> 8) };

    DeviceUid targets[AUTOPAIR_MAX_PAIRED];
    uint8_t   n = 0;
    lock();
    for (uint8_t i = 0; i < _paired_count && n < AUTOPAIR_MAX_PAIRED; i++) {
        targets[n++] = _paired[i].uid;
    }
    unlock();

    for (uint8_t i = 0; i < n; i++) {
        MessageProtocol::instance().sendCommandReliable(
            targets[i], CmdId::SET_CHANNEL, pl, sizeof(pl), nullptr);
    }
    ESP_LOGI(TAG, "Announced channel %u (in %u ms) to %u device(s)",
             (unsigned)channel, (unsigned)delay_ms, (unsigned)n);
    return n;
}

void AutoPair::sweepTick(int64_t now) {
    /* Runs with the lock NOT held: it invokes the channel callback and
     * sends a probe, and AutoPair never calls out under its own mutex. */
    uint8_t   probe_ch = 0;
    DeviceUid ctrl     = UID_NONE;

    lock();
    do {
        if (_is_controller || _state != PairState::PAIRED || !_channel_cb) break;

        /* First tick after boot - start the clock, do not sweep. */
        if (_last_contact_us == 0) { _last_contact_us = now; break; }

        if (!_sweeping) {
            if (now - _last_contact_us < AUTOPAIR_CONTACT_TIMEOUT_US) break;
            _sweeping      = true;
            _sweep_ch      = AUTOPAIR_SWEEP_MIN_CH;
            _sweep_last_us = 0;
            ESP_LOGW(TAG, "No controller contact in %d s - sweeping",
                     (int)(AUTOPAIR_CONTACT_TIMEOUT_US / 1000000LL));
        }

        if (now - _sweep_last_us < AUTOPAIR_SWEEP_DWELL_US) break;
        _sweep_last_us = now;

        probe_ch  = _sweep_ch;
        ctrl      = _controller_uid;
        _channel  = probe_ch;

        _sweep_ch = (_sweep_ch >= AUTOPAIR_SWEEP_MAX_CH)
                    ? AUTOPAIR_SWEEP_MIN_CH : (uint8_t)(_sweep_ch + 1);
    } while (0);
    unlock();

    if (probe_ch == 0) return;

    if (_channel_cb) _channel_cb(probe_ch);

    /* Fire-and-forget PING. If the controller is on this channel it ACKs,
     * and that inbound packet stamps _last_contact_us via noteAddress(),
     * which ends the sweep and saves the channel. The strip stays lit
     * throughout - losing the hub should not turn the light off. */
    MessageProtocol::instance().sendCommand(ctrl, CmdId::PING);
}

/* =============================================================================
 * MESSAGE HANDLING (entry point from MessageProtocol command handler)
 * =============================================================================
 * No MAC parameter in v3. By the time this runs, MessageProtocol has already
 * fired setPeerObservedCallback() → noteAddress(), so src_uid is resolvable.
 * ========================================================================== */

void AutoPair::processPairMessage(CmdId cmd, const uint8_t* payload,
                                  uint8_t len, DeviceUid src_uid) {
    if (src_uid == UID_NONE) return;

    if (cmd == CmdId::PAIR_REQUEST && _is_controller) {
        onPairRequest(payload, len, src_uid);
    } else if (cmd == CmdId::PAIR_ACCEPT && !_is_controller) {
        onPairAccept(payload, len, src_uid);
    } else if (cmd == CmdId::PAIR_REJECT && !_is_controller) {
        onPairReject(src_uid);
    } else if (cmd == CmdId::SET_LOCATION) {
        onSetLocation(payload, len, src_uid);
    } else if (cmd == CmdId::SET_CHANNEL) {
        onSetChannel(payload, len, src_uid);
    }
    /* Anything else (e.g. a controller receiving an ACCEPT) is ignored. */
}

/* ─── Controller: incoming PAIR_REQUEST ───────────────────────────────────── */

void AutoPair::onPairRequest(const uint8_t* payload, uint8_t len,
                             DeviceUid src_uid) {
    if (len < 2) return;    /* need at least role + fw_major */

    lock();

    /* Known device re-requesting (re-flashed / lost its NVS)?
     * Re-accept silently — no popup, no user interaction. */
    int paired_idx = findPairedLocked(src_uid);
    if (paired_idx >= 0) {
        _paired[paired_idx].role = (DeviceRole)payload[0];
        if (len > 2) {
            uint8_t nl = len - 2;
            if (nl > DEVICE_NAME_LEN - 1) nl = DEVICE_NAME_LEN - 1;
            memset(_paired[paired_idx].name, 0, DEVICE_NAME_LEN);
            memcpy(_paired[paired_idx].name, payload + 2, nl);
        }
        /* Refresh the stored MAC from what we just observed */
        int ai = findAddrLocked(src_uid);
        if (ai >= 0) memcpy(_paired[paired_idx].mac, _addr[ai].mac, 6);
        savePairedList();

        RoomId r = _paired[paired_idx].room;
        NodeId n = _paired[paired_idx].node;
        uint8_t mac[6];
        bool have_mac = (ai >= 0);
        if (have_mac) memcpy(mac, _addr[ai].mac, 6);
        unlock();

        ESP_LOGI(TAG, "Known device %08X re-requesting — auto re-accepting",
                 (unsigned)src_uid);
        if (have_mac && _peer_add_cb) _peer_add_cb(mac);
        sendPairAccept(src_uid, r, n);
        return;
    }

    /* Already pending? Refresh its timestamp, don't fire a second popup. */
    int pi = findPendingLocked(src_uid);
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
    slot.uid           = src_uid;
    slot.role          = (DeviceRole)payload[0];
    slot.fw_major      = payload[1];
    slot.first_seen_us = esp_timer_get_time();
    if (len > 2) {
        uint8_t nl = len - 2;
        if (nl > DEVICE_NAME_LEN - 1) nl = DEVICE_NAME_LEN - 1;
        memcpy(slot.name, payload + 2, nl);
    }
    PairRequestInfo copy = slot;    /* stack copy for the callback */
    unlock();

    ESP_LOGI(TAG, "╔═ PAIR REQUEST ═══════════════════════════╗");
    ESP_LOGI(TAG, "║  \"%s\"  uid %08X  role=%s",
             copy.name, (unsigned)copy.uid, deviceRoleName(copy.role));
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    if (_request_cb) _request_cb(&copy);
}

/* ─── Device: incoming PAIR_ACCEPT ────────────────────────────────────────── */

void AutoPair::onPairAccept(const uint8_t* payload, uint8_t len,
                            DeviceUid src_uid) {
    HouseId h = HOUSE_UNASSIGNED;
    RoomId  r = ROOM_UNASSIGNED;
    NodeId  n = NODE_UNASSIGNED;
    bool has_location = msgDecodeLocation(payload, len, h, r, n);

    lock();
    if (_state == PairState::PAIRED) {
        bool same = (_controller_uid == src_uid);
        unlock();
        if (!same) {
            ESP_LOGW(TAG, "ACCEPT from a second controller (%08X) — ignoring. "
                          "unpair() first to switch controllers.",
                     (unsigned)src_uid);
        }
        return;     /* duplicate accept from own controller: silently drop */
    }

    _controller_uid = src_uid;
    int ai = findAddrLocked(src_uid);
    if (ai >= 0) memcpy(_controller_mac, _addr[ai].mac, 6);
    _state = PairState::PAIRED;
    saveDevicePairing();

    uint8_t ctrl_mac[6];
    memcpy(ctrl_mac, _controller_mac, 6);
    unlock();

    /* Commissioning. Do this OUTSIDE the lock — setLocation writes NVS. */
    if (has_location && h != HOUSE_UNASSIGNED) {
        DeviceIdentity::instance().setLocation(h, r, n);
    } else {
        ESP_LOGW(TAG, "PAIR_ACCEPT carried no location — device stays "
                      "uncommissioned and will match any house");
    }

    DeviceIdentity& id = DeviceIdentity::instance();
    ESP_LOGI(TAG, "╔═ PAIRED ═════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Controller %08X", (unsigned)src_uid);
    ESP_LOGI(TAG, "║  House 0x%04X  room %u  node %u",
             (unsigned)id.house(), (unsigned)id.room(), (unsigned)id.node());
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    if (_peer_add_cb) _peer_add_cb(ctrl_mac);
    if (_led_cb)      _led_cb(PairLED::SOLID_ON);
    if (_result_cb)   _result_cb(true, src_uid);
}

/* ─── Device: incoming PAIR_REJECT ────────────────────────────────────────── */

void AutoPair::onPairReject(DeviceUid src_uid) {
    lock();
    if (_state == PairState::PAIRED) { unlock(); return; }
    _state = PairState::REJECTED;
    _last_request_us = esp_timer_get_time();
    unlock();

    ESP_LOGW(TAG, "Pair request rejected by %08X — retrying in %lld s",
             (unsigned)src_uid, AUTOPAIR_REJECT_COOLDOWN_US / 1000000LL);
    if (_led_cb)    _led_cb(PairLED::TRIPLE_BLINK);
    if (_result_cb) _result_cb(false, src_uid);
}

/* ─── Either side: SET_LOCATION (re-address over the air) ─────────────────── */

void AutoPair::onSetLocation(const uint8_t* payload, uint8_t len,
                             DeviceUid src_uid) {
    HouseId h; RoomId r; NodeId n;
    if (!msgDecodeLocation(payload, len, h, r, n)) {
        ESP_LOGW(TAG, "SET_LOCATION with bad payload (len=%u)", len);
        return;
    }

    /* Only our own controller may move us. Without this check any device in
     * radio range could re-address the whole installation. */
    lock();
    bool authorised = _is_controller ||
                      (_state == PairState::PAIRED && _controller_uid == src_uid);
    unlock();

    if (!authorised) {
        ESP_LOGW(TAG, "SET_LOCATION from %08X ignored — not our controller",
                 (unsigned)src_uid);
        return;
    }

    DeviceIdentity::instance().setLocation(h, r, n);
    ESP_LOGI(TAG, "Re-addressed: house 0x%04X room %u node %u",
             (unsigned)h, (unsigned)r, (unsigned)n);
}

/* =============================================================================
 * TX
 * ========================================================================== */

void AutoPair::sendPairRequest() {
    /* Payload: [role(1), fw_major(1), name(0-22)] */
    uint8_t payload[MSG_PAYLOAD_MAX];
    payload[0] = (uint8_t)_role;
    payload[1] = ConfigStore::instance().getU8(ConfigKeys::FW_MAJOR, 1);

    uint8_t name_len = strlen(_name);
    if (name_len > MSG_PAYLOAD_MAX - 2) name_len = MSG_PAYLOAD_MAX - 2;
    memcpy(payload + 2, _name, name_len);

    MessageProtocol::instance().broadcastCommand(CmdId::PAIR_REQUEST,
                                                 payload, 2 + name_len);
    ESP_LOGD(TAG, "Pair request #%lu sent", (unsigned long)_request_count);
}

void AutoPair::sendPairAccept(DeviceUid uid, RoomId room, NodeId node) {
    uint8_t p[4];
    msgEncodeLocation(p, DeviceIdentity::instance().house(), room, node);
    MessageProtocol::instance().sendCommand(uid, CmdId::PAIR_ACCEPT, p, 4);
}

void AutoPair::sendPairReject(DeviceUid uid) {
    MessageProtocol::instance().sendCommand(uid, CmdId::PAIR_REJECT);
}

/* =============================================================================
 * NVS PERSISTENCE
 * ========================================================================== */

void AutoPair::loadDevicePairing() {
    ConfigStore& cfg = ConfigStore::instance();
    if (cfg.getBool(NVS_DEV_PAIRED, false)) {
        size_t len = 6;
        uint32_t uid = cfg.getU32(NVS_DEV_CTRL_UID, UID_NONE);
        if (uid != UID_NONE
            && cfg.getBlob(NVS_DEV_CTRL_MAC, _controller_mac, &len) == ESP_OK
            && len == 6) {
            _controller_uid = uid;
            _state = PairState::PAIRED;
            return;
        }
        ESP_LOGW(TAG, "Pairing flag set but record invalid — resetting");
        eraseDevicePairing();
    }
    _controller_uid = UID_NONE;
    _state = PairState::UNPAIRED;
}

void AutoPair::saveDevicePairing() {
    ConfigStore& cfg = ConfigStore::instance();
    cfg.setBlob(NVS_DEV_CTRL_MAC, _controller_mac, 6);
    cfg.setU32(NVS_DEV_CTRL_UID, _controller_uid);
    cfg.setBool(NVS_DEV_PAIRED, true);
}

void AutoPair::eraseDevicePairing() {
    ConfigStore& cfg = ConfigStore::instance();
    cfg.eraseKey(NVS_DEV_PAIRED);
    cfg.eraseKey(NVS_DEV_CTRL_MAC);
    cfg.eraseKey(NVS_DEV_CTRL_UID);
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
        _paired[i].uid  = recs[i].uid;
        memcpy(_paired[i].mac, recs[i].mac, 6);
        _paired[i].role = (DeviceRole)recs[i].role;
        _paired[i].room = recs[i].room;
        _paired[i].node = recs[i].node;
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
        recs[i].uid  = _paired[i].uid;
        memcpy(recs[i].mac, _paired[i].mac, 6);
        recs[i].role = (uint8_t)_paired[i].role;
        recs[i].room = _paired[i].room;
        recs[i].node = _paired[i].node;
        memcpy(recs[i].name, _paired[i].name, DEVICE_NAME_LEN);
    }
    cfg.setBlob(NVS_CTRL_DEVICES, recs,
                _paired_count * sizeof(PairedRecord));
}

/* =============================================================================
 * LOCKED HELPERS (caller holds _mutex)
 * ========================================================================== */

int AutoPair::findPendingLocked(DeviceUid uid) const {
    for (uint8_t i = 0; i < _pending_count; i++) {
        if (_pending[i].uid == uid) return i;
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

int AutoPair::findPairedLocked(DeviceUid uid) const {
    for (uint8_t i = 0; i < _paired_count; i++) {
        if (_paired[i].uid == uid) return i;
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
void AutoPair::setChannelSetCallback(ChannelSetCb cb)   { _channel_cb = cb; }

bool AutoPair::isSweeping() const {
    lock(); bool s = _sweeping; unlock(); return s;
}