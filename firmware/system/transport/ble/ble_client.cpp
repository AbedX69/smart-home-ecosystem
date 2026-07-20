/*
 * =============================================================================
 * FILE:        ble_client.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-14
 * VERSION:     1.0.0
 * =============================================================================
 */

#include "ble_client.h"
#include "ble_server.h"  // For parseUUID reuse
#include "esp_log.h"
#include "os/os_mbuf.h"

static const char* TAG = "BLEClient";

/* =============================================================================
 * SINGLETON
 * ========================================================================== */

BLEClient& BLEClient::instance() {
    static BLEClient inst;
    return inst;
}

BLEClient::BLEClient()
    : _notify_cb(nullptr)
    , _discovery_cb(nullptr)
{
    memset(&_discovery, 0, sizeof(_discovery));
    memset(&_sync_op, 0, sizeof(_sync_op));
    _sync_op.sem = xSemaphoreCreateBinary();
}

BLEClient::~BLEClient() {
    if (_sync_op.sem) vSemaphoreDelete(_sync_op.sem);
}

/* =============================================================================
 * UUID HELPERS
 * ========================================================================== */

bool BLEClient::parseUUID(const char* str, ble_uuid_any_t* uuid) {
    if (!str || !uuid) return false;

    const char* hex = str;
    if (strncmp(hex, "0x", 2) == 0 || strncmp(hex, "0X", 2) == 0) hex += 2;

    if (strlen(hex) == 4) {
        uuid->u.type = BLE_UUID_TYPE_16;
        uuid->u16.value = (uint16_t)strtol(hex, nullptr, 16);
        return true;
    }

    if (strlen(str) == 36) {
        uuid->u.type = BLE_UUID_TYPE_128;
        uint8_t* dst = uuid->u128.value;
        const char* src = str;
        int byte_idx = 15;
        for (int i = 0; i < 36 && byte_idx >= 0; i++) {
            if (src[i] == '-') continue;
            uint8_t byte = 0;
            char h = src[i], l = src[i + 1];
            i++;
            if (h >= '0' && h <= '9') byte = (h - '0') << 4;
            else if (h >= 'a' && h <= 'f') byte = (h - 'a' + 10) << 4;
            else if (h >= 'A' && h <= 'F') byte = (h - 'A' + 10) << 4;
            if (l >= '0' && l <= '9') byte |= (l - '0');
            else if (l >= 'a' && l <= 'f') byte |= (l - 'a' + 10);
            else if (l >= 'A' && l <= 'F') byte |= (l - 'A' + 10);
            dst[byte_idx--] = byte;
        }
        return true;
    }
    return false;
}

bool BLEClient::uuidMatch(const ble_uuid_any_t* a, const ble_uuid_any_t* b) {
    if (a->u.type != b->u.type) return false;
    if (a->u.type == BLE_UUID_TYPE_16) {
        return a->u16.value == b->u16.value;
    }
    return memcmp(a->u128.value, b->u128.value, 16) == 0;
}

/* =============================================================================
 * SERVICE DISCOVERY
 * =============================================================================
 * 
 * Discovery is a multi-step async process:
 *   1. Discover all services → svcDiscoveryCb called for each
 *   2. For each service, discover its characteristics → chrDiscoveryCb
 *   3. When all done, notify user via discovery callback
 * 
 * NimBLE calls our callbacks from its host task. We chain the steps:
 * after all services are found, we kick off char discovery for each.
 * ========================================================================== */

esp_err_t BLEClient::discoverServices(uint16_t conn_handle) {
    if (_discovery.in_progress) {
        ESP_LOGW(TAG, "Discovery already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    memset(&_discovery, 0, sizeof(_discovery));
    _discovery.conn_handle = conn_handle;
    _discovery.in_progress = true;

    ESP_LOGI(TAG, "Starting service discovery on conn=%d", conn_handle);

    int rc = ble_gattc_disc_all_svcs(conn_handle, svcDiscoveryCb, this);
    if (rc != 0) {
        ESP_LOGE(TAG, "Service discovery failed: %d", rc);
        _discovery.in_progress = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

int BLEClient::svcDiscoveryCb(uint16_t conn_handle,
                                const struct ble_gatt_error* error,
                                const struct ble_gatt_svc* service, void* arg) {
    BLEClient* self = static_cast<BLEClient*>(arg);

    if (error->status == 0 && service) {
        /* Got a service - store it */
        if (self->_discovery.svc_count < BLE_CLI_MAX_SERVICES) {
            BLEDiscoveredService& svc = self->_discovery.services[self->_discovery.svc_count];
            svc.start_handle = service->start_handle;
            svc.end_handle = service->end_handle;
            memcpy(&svc.uuid, &service->uuid, sizeof(ble_uuid_any_t));
            svc.char_count = 0;
            self->_discovery.svc_count++;
        }
        return 0;
    }

    if (error->status == BLE_HS_EDONE) {
        /* All services discovered. Now discover characteristics for each. */
        ESP_LOGI(TAG, "Found %d services, discovering characteristics...",
                 self->_discovery.svc_count);

        if (self->_discovery.svc_count > 0) {
            self->_discovery.current_svc = 0;
            BLEDiscoveredService& svc = self->_discovery.services[0];
            ble_gattc_disc_all_chrs(conn_handle, svc.start_handle, svc.end_handle,
                                    chrDiscoveryCb, self);
        } else {
            self->_discovery.in_progress = false;
            if (self->_discovery_cb) {
                self->_discovery_cb(conn_handle, 0);
            }
        }
    }

    return 0;
}

int BLEClient::chrDiscoveryCb(uint16_t conn_handle,
                                const struct ble_gatt_error* error,
                                const struct ble_gatt_chr* chr, void* arg) {
    BLEClient* self = static_cast<BLEClient*>(arg);
    int svc_idx = self->_discovery.current_svc;

    if (error->status == 0 && chr) {
        BLEDiscoveredService& svc = self->_discovery.services[svc_idx];
        if (svc.char_count < BLE_CLI_MAX_CHARS) {
            BLEDiscoveredChar& c = svc.chars[svc.char_count];
            c.def_handle = chr->def_handle;
            c.val_handle = chr->val_handle;
            c.properties = chr->properties;
            memcpy(&c.uuid, &chr->uuid, sizeof(ble_uuid_any_t));
            svc.char_count++;
        }
        return 0;
    }

    if (error->status == BLE_HS_EDONE) {
        /* Done with this service's characteristics */
        ESP_LOGD(TAG, "Service %d: %d characteristics",
                 svc_idx, self->_discovery.services[svc_idx].char_count);

        /* Move to next service */
        self->_discovery.current_svc++;
        if (self->_discovery.current_svc < self->_discovery.svc_count) {
            BLEDiscoveredService& next = self->_discovery.services[self->_discovery.current_svc];
            ble_gattc_disc_all_chrs(conn_handle, next.start_handle, next.end_handle,
                                    chrDiscoveryCb, self);
        } else {
            /* All done! */
            self->_discovery.in_progress = false;
            ESP_LOGI(TAG, "Discovery complete: %d services", self->_discovery.svc_count);

            if (self->_discovery_cb) {
                self->_discovery_cb(conn_handle, self->_discovery.svc_count);
            }
        }
    }

    return 0;
}

const BLEDiscoveredService* BLEClient::getServices(uint16_t conn_handle, int& count) const {
    if (_discovery.conn_handle != conn_handle || _discovery.in_progress) {
        count = 0;
        return nullptr;
    }
    count = _discovery.svc_count;
    return _discovery.services;
}

const BLEDiscoveredChar* BLEClient::getCharByUUID(uint16_t conn_handle,
                                                    const char* uuid_str) const {
    ble_uuid_any_t target;
    if (!parseUUID(uuid_str, &target)) return nullptr;

    for (int s = 0; s < _discovery.svc_count; s++) {
        const BLEDiscoveredService& svc = _discovery.services[s];
        for (int c = 0; c < svc.char_count; c++) {
            if (uuidMatch(&svc.chars[c].uuid, &target)) {
                return &svc.chars[c];
            }
        }
    }
    return nullptr;
}

/* =============================================================================
 * READ
 * =============================================================================
 * 
 * NimBLE reads are async. We use a semaphore to make it synchronous
 * from the user's perspective. The callback signals the semaphore
 * when data arrives.
 * ========================================================================== */

int BLEClient::readCb(uint16_t conn_handle, const struct ble_gatt_error* error,
                       struct ble_gatt_attr* attr, void* arg) {
    BLEClient* self = static_cast<BLEClient*>(arg);

    if (error->status == 0 && attr && attr->om) {
        uint16_t len = OS_MBUF_PKTLEN(attr->om);
        uint16_t copy_len = (len > self->_sync_op.buf_len) ? self->_sync_op.buf_len : len;
        ble_hs_mbuf_to_flat(attr->om, self->_sync_op.buf, self->_sync_op.buf_len,
                            &self->_sync_op.result_len);
        self->_sync_op.status = 0;
    } else {
        self->_sync_op.status = error->status;
        self->_sync_op.result_len = 0;
    }

    xSemaphoreGive(self->_sync_op.sem);
    return 0;
}

esp_err_t BLEClient::read(uint16_t conn_handle, uint16_t attr_handle,
                           uint8_t* buf, uint16_t buf_len, uint16_t* out_len) {
    if (!buf || buf_len == 0) return ESP_ERR_INVALID_ARG;

    _sync_op.buf = buf;
    _sync_op.buf_len = buf_len;
    _sync_op.result_len = 0;
    _sync_op.status = -1;

    int rc = ble_gattc_read(conn_handle, attr_handle, readCb, this);
    if (rc != 0) {
        ESP_LOGE(TAG, "Read initiate failed: %d", rc);
        return ESP_FAIL;
    }

    /* Wait for callback (5 second timeout) */
    if (xSemaphoreTake(_sync_op.sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Read timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (_sync_op.status != 0) {
        ESP_LOGE(TAG, "Read failed: status=%d", _sync_op.status);
        return ESP_FAIL;
    }

    if (out_len) *out_len = _sync_op.result_len;
    return ESP_OK;
}

/* =============================================================================
 * WRITE
 * ========================================================================== */

int BLEClient::writeCb(uint16_t conn_handle, const struct ble_gatt_error* error,
                        struct ble_gatt_attr* attr, void* arg) {
    BLEClient* self = static_cast<BLEClient*>(arg);
    self->_sync_op.status = error->status;
    xSemaphoreGive(self->_sync_op.sem);
    return 0;
}

esp_err_t BLEClient::write(uint16_t conn_handle, uint16_t attr_handle,
                            const uint8_t* data, uint16_t len) {
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;

    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
    if (!om) return ESP_ERR_NO_MEM;

    _sync_op.status = -1;

    int rc = ble_gattc_write(conn_handle, attr_handle, om, writeCb, this);
    if (rc != 0) {
        ESP_LOGE(TAG, "Write initiate failed: %d", rc);
        return ESP_FAIL;
    }

    if (xSemaphoreTake(_sync_op.sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Write timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (_sync_op.status != 0) {
        ESP_LOGE(TAG, "Write failed: status=%d", _sync_op.status);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t BLEClient::writeNoResponse(uint16_t conn_handle, uint16_t attr_handle,
                                      const uint8_t* data, uint16_t len) {
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;

    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
    if (!om) return ESP_ERR_NO_MEM;

    int rc = ble_gattc_write_no_rsp(conn_handle, attr_handle, om);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

/* =============================================================================
 * SUBSCRIBE / UNSUBSCRIBE
 * =============================================================================
 * 
 * To get notifications/indications, we write to the CCCD (Client
 * Characteristic Configuration Descriptor). The CCCD handle is
 * typically val_handle + 1 (the descriptor right after the value).
 * 
 *   0x0001 = enable notifications
 *   0x0002 = enable indications
 *   0x0000 = disable both
 * ========================================================================== */

esp_err_t BLEClient::subscribe(uint16_t conn_handle, uint16_t attr_handle, bool notify) {
    uint16_t cccd_val = notify ? 0x0001 : 0x0002;
    /* CCCD is typically the next handle after the characteristic value */
    uint16_t cccd_handle = attr_handle + 1;

    uint8_t data[2] = { (uint8_t)(cccd_val & 0xFF), (uint8_t)(cccd_val >> 8) };

    ESP_LOGI(TAG, "Subscribing: conn=%d attr=%d cccd=%d type=%s",
             conn_handle, attr_handle, cccd_handle,
             notify ? "notify" : "indicate");

    return write(conn_handle, cccd_handle, data, 2);
}

esp_err_t BLEClient::unsubscribe(uint16_t conn_handle, uint16_t attr_handle) {
    uint16_t cccd_handle = attr_handle + 1;
    uint8_t data[2] = {0x00, 0x00};
    return write(conn_handle, cccd_handle, data, 2);
}

/* =============================================================================
 * NOTIFICATION HANDLING
 * ========================================================================== */

void BLEClient::handleNotify(uint16_t conn_handle, uint16_t attr_handle,
                              struct os_mbuf* om) {
    if (!_notify_cb || !om) return;

    uint16_t len = OS_MBUF_PKTLEN(om);
    uint8_t buf[256];
    uint16_t flat_len = 0;

    if (len > sizeof(buf)) len = sizeof(buf);
    ble_hs_mbuf_to_flat(om, buf, sizeof(buf), &flat_len);

    BLENotifyData data = {};
    data.conn_handle = conn_handle;
    data.attr_handle = attr_handle;
    data.data = buf;
    data.data_len = flat_len;

    _notify_cb(&data);
}

void BLEClient::setNotifyCallback(BLENotifyCb cb) { _notify_cb = cb; }
void BLEClient::setDiscoveryCallback(BLEDiscoveryCb cb) { _discovery_cb = cb; }
