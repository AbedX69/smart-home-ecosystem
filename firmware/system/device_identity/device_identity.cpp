/*
 * =============================================================================
 * FILE:        device_identity.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-07-25
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * =============================================================================
 */

#include "device_identity.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_rom_crc.h"

#include "config_store.h"

static const char* TAG = "DeviceIdentity";

/* ─── Singleton ──────────────────────────────────────────────────────────── */

DeviceIdentity& DeviceIdentity::instance() {
    static DeviceIdentity inst;
    return inst;
}

DeviceIdentity::DeviceIdentity()
    : _ready(false),
      _uid(UID_NONE),
      _mac{0, 0, 0, 0, 0, 0},
      _house(HOUSE_UNASSIGNED),
      _room(ROOM_UNASSIGNED),
      _node(NODE_UNASSIGNED) {
    _uid_str[0] = '\0';
}

/* ─── Static helpers ─────────────────────────────────────────────────────── */

DeviceUid DeviceIdentity::uidFromMac(const uint8_t mac[6]) {
    DeviceUid uid = (DeviceUid)esp_rom_crc32_le(0, mac, 6);

    /* UID_NONE is the "address by room+node instead" sentinel in the wire
     * format, so no real device may ever hold it. The odds are 1 in 4.3
     * billion, but a silent address collision would be miserable to debug. */
    if (uid == UID_NONE) {
        uid = 0x00000001u;
    }
    return uid;
}

void DeviceIdentity::formatUid(DeviceUid uid, char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) return;
    snprintf(out, out_len, "%08lX", (unsigned long)uid);
}

/* ─── Lifecycle ──────────────────────────────────────────────────────────── */

esp_err_t DeviceIdentity::begin() {
    if (_ready) return ESP_OK;

    /* The factory eFuse MAC — burned at manufacture, not erasable, and
     * independent of whatever the WiFi/BT interfaces report at runtime. */
    uint8_t mac[6] = {0};
   esp_err_t err = esp_read_mac(mac, ESP_MAC_BASE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_read_mac(ESP_MAC_BASE) failed: %s", esp_err_to_name(err));
        return err;
    }

    
    memcpy(_mac, mac, sizeof(_mac));
    _uid = uidFromMac(mac);
    formatUid(_uid, _uid_str, sizeof(_uid_str));

    ConfigStore& cfg = ConfigStore::instance();
    if (!cfg.isOpen()) {
        err = cfg.begin();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ConfigStore::begin failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    _house = cfg.getU16(IDENTITY_KEY_HOUSE, HOUSE_UNASSIGNED);
    _room  = cfg.getU8(IDENTITY_KEY_ROOM,   ROOM_UNASSIGNED);
    _node  = cfg.getU8(IDENTITY_KEY_NODE,   NODE_UNASSIGNED);

    _ready = true;

    ESP_LOGI(TAG, "UID %s  (eFuse MAC %02X:%02X:%02X:%02X:%02X:%02X)",
             _uid_str, _mac[0], _mac[1], _mac[2], _mac[3], _mac[4], _mac[5]);

    if (isProvisioned()) {
        ESP_LOGI(TAG, "House 0x%04X  room %u  node %u",
                 (unsigned)_house, (unsigned)_room, (unsigned)_node);
    } else {
        ESP_LOGW(TAG, "Not commissioned — no house assigned yet");
    }

    return ESP_OK;
}

/* ─── Assignment ─────────────────────────────────────────────────────────── */

esp_err_t DeviceIdentity::setHouse(HouseId h) {
    ConfigStore& cfg = ConfigStore::instance();
    esp_err_t err = cfg.setU16(IDENTITY_KEY_HOUSE, h);
    if (err != ESP_OK) return err;

    err = cfg.commit();
    if (err != ESP_OK) return err;

    _house = h;
    ESP_LOGI(TAG, "House set to 0x%04X", (unsigned)h);
    return ESP_OK;
}

esp_err_t DeviceIdentity::setRoom(RoomId r) {
    ConfigStore& cfg = ConfigStore::instance();
    esp_err_t err = cfg.setU8(IDENTITY_KEY_ROOM, r);
    if (err != ESP_OK) return err;

    err = cfg.commit();
    if (err != ESP_OK) return err;

    _room = r;
    ESP_LOGI(TAG, "Room set to %u", (unsigned)r);
    return ESP_OK;
}

esp_err_t DeviceIdentity::setNode(NodeId n) {
    ConfigStore& cfg = ConfigStore::instance();
    esp_err_t err = cfg.setU8(IDENTITY_KEY_NODE, n);
    if (err != ESP_OK) return err;

    err = cfg.commit();
    if (err != ESP_OK) return err;

    _node = n;
    ESP_LOGI(TAG, "Node set to %u", (unsigned)n);
    return ESP_OK;
}

esp_err_t DeviceIdentity::setLocation(HouseId h, RoomId r, NodeId n) {
    ConfigStore& cfg = ConfigStore::instance();

    esp_err_t err = cfg.setU16(IDENTITY_KEY_HOUSE, h);
    if (err != ESP_OK) return err;

    err = cfg.setU8(IDENTITY_KEY_ROOM, r);
    if (err != ESP_OK) return err;

    err = cfg.setU8(IDENTITY_KEY_NODE, n);
    if (err != ESP_OK) return err;

    err = cfg.commit();
    if (err != ESP_OK) return err;

    _house = h;
    _room  = r;
    _node  = n;

    ESP_LOGI(TAG, "Location set: house 0x%04X  room %u  node %u",
             (unsigned)h, (unsigned)r, (unsigned)n);
    return ESP_OK;
}

esp_err_t DeviceIdentity::provisionAsNewHouse() {
    HouseId h;
    do {
        h = (HouseId)(esp_random() & 0xFFFFu);
    } while (h == HOUSE_UNASSIGNED);

    ESP_LOGI(TAG, "Minting new house id");
    return setHouse(h);
}

esp_err_t DeviceIdentity::clearLocation() {
    ESP_LOGW(TAG, "Clearing location — device becomes uncommissioned");
    return setLocation(HOUSE_UNASSIGNED, ROOM_UNASSIGNED, NODE_UNASSIGNED);
}

/* ─── Inbound filtering ──────────────────────────────────────────────────── */

bool DeviceIdentity::acceptsHouse(HouseId incoming) const {
    /* We are not commissioned — accept anything so we can be adopted. */
    if (_house == HOUSE_UNASSIGNED) return true;

    /* Sender is not commissioned — likely a factory-fresh node broadcasting
     * a pair request. Let it through so pairing can complete. */
    if (incoming == HOUSE_UNASSIGNED) return true;

    return incoming == _house;
}

bool DeviceIdentity::isForMe(DeviceUid dst_uid,
                             RoomId    dst_room,
                             NodeId    dst_node) const {
    /* Unicast wins outright. */
    if (dst_uid != UID_NONE) {
        return dst_uid == _uid;
    }

    /* Group address: match room and node, honouring wildcards. */
    const bool room_ok = (dst_room == ROOM_ALL) || (dst_room == _room);
    const bool node_ok = (dst_node == NODE_ALL) || (dst_node == _node);

    return room_ok && node_ok;
}
