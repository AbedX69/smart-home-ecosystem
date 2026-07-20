/*
 * =============================================================================
 * FILE:        device_registry.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-17
 * VERSION:     1.0.0
 * =============================================================================
 */

#include "device_registry.h"

static const char* TAG = "DeviceReg";

/* =============================================================================
 * SINGLETON
 * ========================================================================== */

DeviceRegistry& DeviceRegistry::instance() {
    static DeviceRegistry inst;
    return inst;
}

DeviceRegistry::DeviceRegistry()
    : _running(false)
    , _offline_timeout_us(DEFAULT_OFFLINE_MS * 1000LL)
    , _heartbeat_timer(nullptr)
    , _stale_timer(nullptr)
    , _event_cb(nullptr)
{
    _mutex = xSemaphoreCreateMutex();
    memset(_devices, 0, sizeof(_devices));
    memset(_self_mac, 0, sizeof(_self_mac));
    memset(_transports, 0, sizeof(_transports));
}

DeviceRegistry::~DeviceRegistry() {
    stop();
    if (_mutex) vSemaphoreDelete(_mutex);
}

/* =============================================================================
 * SETUP
 * ========================================================================== */

void DeviceRegistry::setSelf(const DeviceIdentity& identity) {
    _self = identity;

    /* Read this device's base MAC */
    esp_read_mac(_self_mac, ESP_MAC_WIFI_STA);

    ESP_LOGI(TAG, "Self: \"%s\" role=%d MAC=%02X:%02X:%02X:%02X:%02X:%02X",
             _self.name, (int)_self.role,
             _self_mac[0], _self_mac[1], _self_mac[2],
             _self_mac[3], _self_mac[4], _self_mac[5]);
}

void DeviceRegistry::registerTransport(uint8_t transport_bit, TransportSendFn send_fn) {
    for (auto& t : _transports) {
        if (!t.active) {
            t.bit = transport_bit;
            t.fn = send_fn;
            t.active = true;

            const char* name = (transport_bit == TRANSPORT_ESPNOW) ? "ESP-NOW" :
                                (transport_bit == TRANSPORT_WIFI)   ? "WiFi" :
                                (transport_bit == TRANSPORT_BLE)    ? "BLE" :
                                (transport_bit == TRANSPORT_LORA)   ? "LoRa" :
                                (transport_bit == TRANSPORT_ZIGBEE) ? "Zigbee" : "?";
            ESP_LOGI(TAG, "Transport registered: %s", name);
            return;
        }
    }
    ESP_LOGW(TAG, "No transport slot available (max 5)");
}

esp_err_t DeviceRegistry::begin(uint32_t heartbeat_ms, uint32_t offline_ms) {
    if (_running) return ESP_OK;

    _offline_timeout_us = (int64_t)offline_ms * 1000LL;

    /* Heartbeat timer: periodic broadcast */
    _heartbeat_timer = xTimerCreate("hb", pdMS_TO_TICKS(heartbeat_ms),
                                      pdTRUE, this, heartbeatTimerCb);
    if (!_heartbeat_timer) return ESP_ERR_NO_MEM;

    /* Stale check timer: runs at 2x heartbeat interval to detect offline devices */
    _stale_timer = xTimerCreate("stale", pdMS_TO_TICKS(heartbeat_ms * 2),
                                  pdTRUE, this, staleCheckTimerCb);
    if (!_stale_timer) return ESP_ERR_NO_MEM;

    xTimerStart(_heartbeat_timer, 0);
    xTimerStart(_stale_timer, 0);

    _running = true;

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  Device Registry started");
    ESP_LOGI(TAG, "  Heartbeat: %lu ms", (unsigned long)heartbeat_ms);
    ESP_LOGI(TAG, "  Offline:   %lu ms", (unsigned long)offline_ms);
    ESP_LOGI(TAG, "  Device:    \"%s\"", _self.name);
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    /* Send first heartbeat immediately */
    sendHeartbeat();

    return ESP_OK;
}

void DeviceRegistry::stop() {
    if (!_running) return;

    if (_heartbeat_timer) {
        xTimerStop(_heartbeat_timer, 0);
        xTimerDelete(_heartbeat_timer, 0);
        _heartbeat_timer = nullptr;
    }
    if (_stale_timer) {
        xTimerStop(_stale_timer, 0);
        xTimerDelete(_stale_timer, 0);
        _stale_timer = nullptr;
    }

    _running = false;
    ESP_LOGI(TAG, "Device Registry stopped");
}

/* =============================================================================
 * HEARTBEAT
 * =============================================================================
 * 
 * Builds a 32-byte announcement packet and sends it on ALL registered
 * transports. Each transport broadcasts in its own way:
 *   - ESP-NOW: broadcast to FF:FF:FF:FF:FF:FF
 *   - WiFi UDP: broadcast to 255.255.255.255:DISCOVERY_PORT
 *   - LoRa: send()
 *   - BLE: advertise / notify
 * ========================================================================== */

void DeviceRegistry::buildPacket(AnnouncementPacket& pkt) {
    pkt.magic = DEVICE_MAGIC;
    memcpy(pkt.mac, _self_mac, 6);
    pkt.role = (uint8_t)_self.role;
    pkt.transports = _self.transports;
    pkt.fw_version = ((uint16_t)_self.fw_major << 8) | _self.fw_minor;
    pkt.state_flags = _self.state_flags;
    memcpy(pkt.name, _self.name, DEVICE_NAME_LEN);
}

void DeviceRegistry::sendHeartbeat() {
    AnnouncementPacket pkt;
    buildPacket(pkt);

    for (auto& t : _transports) {
        if (t.active && t.fn) {
            esp_err_t ret = t.fn((const uint8_t*)&pkt, sizeof(pkt));
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Heartbeat send failed on transport 0x%02X: %s",
                         t.bit, esp_err_to_name(ret));
            }
        }
    }
}

void DeviceRegistry::heartbeatTimerCb(TimerHandle_t timer) {
    DeviceRegistry* self = (DeviceRegistry*)pvTimerGetTimerID(timer);
    self->sendHeartbeat();
}

/* =============================================================================
 * PROCESS INCOMING ANNOUNCEMENTS
 * =============================================================================
 * 
 * Called from any transport's receive callback. Validates the packet,
 * ignores our own broadcasts, and updates the device table.
 * 
 * Three possible outcomes:
 *   1. New device → add to table, fire DEVICE_JOINED
 *   2. Known device → update last_seen, check for changes → DEVICE_UPDATED
 *   3. Invalid packet → ignore
 * ========================================================================== */

esp_err_t DeviceRegistry::processAnnouncement(const uint8_t* data, uint8_t len) {
    if (!data || len < sizeof(AnnouncementPacket)) return ESP_ERR_INVALID_SIZE;

    const AnnouncementPacket* pkt = (const AnnouncementPacket*)data;

    /* Validate magic */
    if (pkt->magic != DEVICE_MAGIC) return ESP_ERR_INVALID_ARG;

    /* Ignore our own broadcasts */
    if (memcmp(pkt->mac, _self_mac, 6) == 0) return ESP_OK;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    int64_t now = esp_timer_get_time();
    DeviceEntry* entry = findSlotByMAC(pkt->mac);
    bool is_new = false;

    if (!entry) {
        /* New device */
        entry = findEmptySlot();
        if (!entry) {
            xSemaphoreGive(_mutex);
            ESP_LOGW(TAG, "Device table full (max %d)", MAX_DEVICES);
            return ESP_ERR_NO_MEM;
        }
        is_new = true;
        entry->active = true;
        entry->heartbeat_count = 0;
        memcpy(entry->mac, pkt->mac, 6);
    }

    /* Update fields */
    bool was_offline = !entry->online;
    entry->online = true;
    entry->role = (DeviceRole)pkt->role;
    entry->transports = pkt->transports;
    entry->fw_version = pkt->fw_version;
    entry->state_flags = pkt->state_flags;
    memcpy(entry->name, pkt->name, DEVICE_NAME_LEN);
    entry->last_seen_us = now;
    entry->heartbeat_count++;

    xSemaphoreGive(_mutex);

    /* Fire event */
    if (_event_cb) {
        RegistryEventInfo info;
        info.device = entry;

        if (is_new) {
            info.event = RegistryEvent::DEVICE_JOINED;
            ESP_LOGI(TAG, "╔═ NEW DEVICE ═══════════════════════════╗");
            ESP_LOGI(TAG, "║  Name: %-16s                ║", entry->name);
            ESP_LOGI(TAG, "║  MAC:  %02X:%02X:%02X:%02X:%02X:%02X            ║",
                     entry->mac[0], entry->mac[1], entry->mac[2],
                     entry->mac[3], entry->mac[4], entry->mac[5]);
            ESP_LOGI(TAG, "║  Role: %d   FW: %d.%d                    ║",
                     (int)entry->role, entry->fw_version >> 8,
                     entry->fw_version & 0xFF);
            ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
        } else if (was_offline) {
            info.event = RegistryEvent::DEVICE_JOINED;
            ESP_LOGI(TAG, "Device back online: \"%s\"", entry->name);
        } else {
            info.event = RegistryEvent::DEVICE_UPDATED;
        }
        _event_cb(&info);
    }

    return ESP_OK;
}

/* =============================================================================
 * STALE DEVICE DETECTION
 * ========================================================================== */

void DeviceRegistry::staleCheckTimerCb(TimerHandle_t timer) {
    DeviceRegistry* self = (DeviceRegistry*)pvTimerGetTimerID(timer);
    self->markStaleDevices();
}

void DeviceRegistry::markStaleDevices() {
    int64_t now = esp_timer_get_time();

    xSemaphoreTake(_mutex, portMAX_DELAY);

    for (auto& d : _devices) {
        if (!d.active || !d.online) continue;

        int64_t elapsed = now - d.last_seen_us;
        if (elapsed > _offline_timeout_us) {
            d.online = false;

            ESP_LOGW(TAG, "Device offline: \"%s\" (no heartbeat for %llds)",
                     d.name, elapsed / 1000000LL);

            if (_event_cb) {
                RegistryEventInfo info;
                info.device = &d;
                info.event = RegistryEvent::DEVICE_LEFT;

                /* Release mutex before callback to avoid deadlock */
                xSemaphoreGive(_mutex);
                _event_cb(&info);
                xSemaphoreTake(_mutex, portMAX_DELAY);
            }
        }
    }

    xSemaphoreGive(_mutex);
}

/* =============================================================================
 * QUERY
 * ========================================================================== */

const DeviceEntry* DeviceRegistry::findByRole(DeviceRole role) const {
    for (const auto& d : _devices) {
        if (d.active && d.online && d.role == role) return &d;
    }
    return nullptr;
}

const DeviceEntry* DeviceRegistry::findByMAC(const uint8_t mac[6]) const {
    for (const auto& d : _devices) {
        if (d.active && memcmp(d.mac, mac, 6) == 0) return &d;
    }
    return nullptr;
}

const DeviceEntry* DeviceRegistry::findByName(const char* name) const {
    if (!name) return nullptr;
    for (const auto& d : _devices) {
        if (d.active && strstr(d.name, name) != nullptr) return &d;
    }
    return nullptr;
}

uint8_t DeviceRegistry::getDevices(const DeviceEntry** out, uint8_t max_count) const {
    uint8_t count = 0;
    for (const auto& d : _devices) {
        if (d.active && count < max_count) {
            out[count++] = &d;
        }
    }
    return count;
}

uint8_t DeviceRegistry::getOnlineCount() const {
    uint8_t count = 0;
    for (const auto& d : _devices) {
        if (d.active && d.online) count++;
    }
    return count;
}

uint8_t DeviceRegistry::getTotalCount() const {
    uint8_t count = 0;
    for (const auto& d : _devices) {
        if (d.active) count++;
    }
    return count;
}

const uint8_t* DeviceRegistry::getSelfMAC() const {
    return _self_mac;
}

/* =============================================================================
 * HELPERS
 * ========================================================================== */

DeviceEntry* DeviceRegistry::findSlotByMAC(const uint8_t mac[6]) {
    for (auto& d : _devices) {
        if (d.active && memcmp(d.mac, mac, 6) == 0) return &d;
    }
    return nullptr;
}

DeviceEntry* DeviceRegistry::findEmptySlot() {
    for (auto& d : _devices) {
        if (!d.active) return &d;
    }
    return nullptr;
}

void DeviceRegistry::setEventCallback(RegistryEventCb cb) { _event_cb = cb; }

void DeviceRegistry::updateStateFlags(uint16_t flags) {
    _self.state_flags = flags;
}

void DeviceRegistry::clearDevices() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    memset(_devices, 0, sizeof(_devices));
    xSemaphoreGive(_mutex);
    ESP_LOGI(TAG, "Device table cleared");
}
