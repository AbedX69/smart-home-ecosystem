/*
 * =============================================================================
 * FILE:        ble_server.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-14
 * VERSION:     1.0.0
 * =============================================================================
 */

#include "ble_server.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "os/os_mbuf.h"

static const char* TAG = "BLEServer";

/* =============================================================================
 * SINGLETON
 * ========================================================================== */

BLEServer& BLEServer::instance() {
    static BLEServer inst;
    return inst;
}

BLEServer::BLEServer()
    : _svc_count(0)
    , _built(false)
    , _total_chars(0)
    , _access_cb(nullptr)
{
    memset(_svcs, 0, sizeof(_svcs));
    memset(_svc_defs, 0, sizeof(_svc_defs));
    memset(_char_values, 0, sizeof(_char_values));
}

BLEServer::~BLEServer() {}

/* =============================================================================
 * UUID PARSING
 * =============================================================================
 * 
 * Supports two formats:
 *   16-bit:  "0x180D" or "180D"
 *   128-bit: "12345678-1234-1234-1234-123456789ABC"
 * ========================================================================== */

bool BLEServer::parseUUID(const char* str, ble_uuid_any_t* uuid) {
    if (!str || !uuid) return false;

    /* Try 16-bit first (4 hex chars, optionally prefixed with "0x") */
    const char* hex = str;
    if (strncmp(hex, "0x", 2) == 0 || strncmp(hex, "0X", 2) == 0) {
        hex += 2;
    }

    if (strlen(hex) == 4) {
        uint16_t val = (uint16_t)strtol(hex, nullptr, 16);
        uuid->u.type = BLE_UUID_TYPE_16;
        uuid->u16.value = val;
        return true;
    }

    /* 128-bit: "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX" */
    if (strlen(str) == 36) {
        uuid->u.type = BLE_UUID_TYPE_128;

        /* Parse 128-bit UUID. NimBLE stores in LITTLE-ENDIAN order.
         * UUID string is big-endian, so we reverse while parsing. */
        uint8_t* dst = uuid->u128.value;
        const char* src = str;
        int byte_idx = 15;  // Start from the end (little-endian)

        for (int i = 0; i < 36 && byte_idx >= 0; i++) {
            if (src[i] == '-') continue;

            char high = src[i];
            char low = src[i + 1];
            i++;  // Skip the low nibble (loop will increment once more)

            uint8_t byte = 0;
            if (high >= '0' && high <= '9') byte = (high - '0') << 4;
            else if (high >= 'a' && high <= 'f') byte = (high - 'a' + 10) << 4;
            else if (high >= 'A' && high <= 'F') byte = (high - 'A' + 10) << 4;

            if (low >= '0' && low <= '9') byte |= (low - '0');
            else if (low >= 'a' && low <= 'f') byte |= (low - 'a' + 10);
            else if (low >= 'A' && low <= 'F') byte |= (low - 'A' + 10);

            dst[byte_idx--] = byte;
        }
        return true;
    }

    ESP_LOGE(TAG, "Invalid UUID: %s", str);
    return false;
}

/* =============================================================================
 * SERVICE & CHARACTERISTIC BUILDING
 * =============================================================================
 * 
 * NimBLE requires a static GATT table defined as arrays of structs:
 * 
 *   ble_gatt_svc_def[] → array of services (null-terminated)
 *     └─ ble_gatt_chr_def[] → array of characteristics (null-terminated)
 * 
 * These arrays MUST persist for the lifetime of the BLE stack.
 * That's why we store them in the singleton's member variables.
 * 
 * The build process:
 *   1. User calls addService() + addCharacteristic() to define the table
 *   2. User calls buildServices() to finalize
 *   3. buildServices() calls ble_gatts_count_cfg() + ble_gatts_add_svcs()
 *   4. User calls BLEManager::begin() which starts the stack
 *   5. NimBLE assigns attribute handles to each characteristic
 * ========================================================================== */

int BLEServer::addService(const char* uuid_str) {
    if (_built) {
        ESP_LOGE(TAG, "Cannot add services after buildServices()");
        return -1;
    }
    if (_svc_count >= BLE_SRV_MAX_SERVICES) {
        ESP_LOGE(TAG, "Max services (%d) reached", BLE_SRV_MAX_SERVICES);
        return -1;
    }

    SvcStorage& svc = _svcs[_svc_count];
    if (!parseUUID(uuid_str, &svc.uuid)) return -1;

    svc.char_count = 0;
    /* Zero-initialize the characteristics array (null terminator) */
    memset(svc.chars, 0, sizeof(svc.chars));

    ESP_LOGI(TAG, "Added service #%d: %s", _svc_count, uuid_str);
    return _svc_count++;
}

int BLEServer::addCharacteristic(const char* uuid_str, uint16_t flags,
                                  uint8_t* value, uint16_t value_len,
                                  uint16_t max_len, uint16_t* val_handle) {
    if (_built) {
        ESP_LOGE(TAG, "Cannot add characteristics after buildServices()");
        return -1;
    }
    if (_svc_count == 0) {
        ESP_LOGE(TAG, "Add a service first");
        return -1;
    }

    SvcStorage& svc = _svcs[_svc_count - 1];  // Current (last) service
    if (svc.char_count >= BLE_SRV_MAX_CHARS_PER_SVC) {
        ESP_LOGE(TAG, "Max characteristics per service reached");
        return -1;
    }
    if (_total_chars >= BLE_SRV_MAX_CHARS_TOTAL) {
        ESP_LOGE(TAG, "Max total characteristics reached");
        return -1;
    }

    int ci = svc.char_count;

    /* Parse and store UUID */
    if (!parseUUID(uuid_str, &svc.char_uuids[ci])) return -1;

    /* Store value reference for access callback */
    _char_values[_total_chars].buf = value;
    _char_values[_total_chars].len = value_len;
    _char_values[_total_chars].max_len = (max_len > 0) ? max_len : value_len;

    /* Allocate a static handle pointer if user didn't provide one */
    static uint16_t s_handles[BLE_SRV_MAX_CHARS_TOTAL];
    uint16_t* handle_ptr = val_handle ? val_handle : &s_handles[_total_chars];
    svc.char_val_handles[ci] = handle_ptr;

    /* Build NimBLE characteristic definition */
    ble_gatt_chr_def& chr = svc.chars[ci];
    chr.uuid = &svc.char_uuids[ci].u;
    chr.access_cb = gattAccessCb;
    chr.arg = (void*)(intptr_t)_total_chars;  // Index into _char_values
    chr.flags = flags;
    chr.val_handle = handle_ptr;

    svc.char_count++;
    _total_chars++;

    ESP_LOGI(TAG, "Added characteristic: %s (flags=0x%04X)", uuid_str, flags);
    return 0;
}

esp_err_t BLEServer::buildServices() {
    if (_built) {
        ESP_LOGW(TAG, "Services already built");
        return ESP_OK;
    }
    if (_svc_count == 0) {
        ESP_LOGW(TAG, "No services to build");
        return ESP_OK;
    }

    /* Build the ble_gatt_svc_def array */
    for (int i = 0; i < _svc_count; i++) {
        _svc_defs[i].type = BLE_GATT_SVC_TYPE_PRIMARY;
        _svc_defs[i].uuid = &_svcs[i].uuid.u;
        _svc_defs[i].characteristics = _svcs[i].chars;
        /* chars array is already null-terminated (last entry is zeroed) */
    }
    /* Null-terminate the service array */
    memset(&_svc_defs[_svc_count], 0, sizeof(ble_gatt_svc_def));

    /* Register with NimBLE */
    int rc = ble_gatts_count_cfg(_svc_defs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT count config failed: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(_svc_defs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT add services failed: %d", rc);
        return ESP_FAIL;
    }

    _built = true;
    ESP_LOGI(TAG, "GATT table built: %d services, %d characteristics",
             _svc_count, _total_chars);
    return ESP_OK;
}

/* =============================================================================
 * GATT ACCESS CALLBACK
 * =============================================================================
 * 
 * NimBLE calls this when a client reads or writes a characteristic.
 * The `arg` pointer contains the index into our _char_values array.
 * 
 * For READs: We copy our stored value into the response mbuf.
 * For WRITEs: We copy incoming data into our stored buffer.
 * 
 * The user's access callback is called for both operations so they
 * can react to writes or dynamically update values before reads.
 * ========================================================================== */

int BLEServer::gattAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt* ctxt, void* arg) {
    BLEServer& srv = instance();
    int char_idx = (int)(intptr_t)arg;

    if (char_idx < 0 || char_idx >= srv._total_chars) {
        ESP_LOGE(TAG, "Invalid char index: %d", char_idx);
        return BLE_ATT_ERR_UNLIKELY;
    }

    CharValueRef& val = srv._char_values[char_idx];

    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR: {
            ESP_LOGD(TAG, "Read: conn=%d attr=%d idx=%d",
                     conn_handle, attr_handle, char_idx);

            /* Notify user callback before read (chance to update value) */
            if (srv._access_cb) {
                BLECharAccess access = {};
                access.conn_handle = conn_handle;
                access.attr_handle = attr_handle;
                access.op = ctxt->op;
                access.data = val.buf;
                access.data_len = val.len;
                if (ctxt->chr) {
                    memcpy(&access.char_uuid, ctxt->chr->uuid, sizeof(ble_uuid_any_t));
                }
                srv._access_cb(&access);
                /* User may have updated val.buf contents */
            }

            /* Append value to the response mbuf */
            int rc = os_mbuf_append(ctxt->om, val.buf, val.len);
            return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        case BLE_GATT_ACCESS_OP_WRITE_CHR: {
            /* Extract data from the incoming mbuf */
            uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
            if (om_len > val.max_len) {
                ESP_LOGW(TAG, "Write too large: %d > %d", om_len, val.max_len);
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }

            /* Flatten mbuf into our buffer.
             * ble_hs_mbuf_to_flat() copies from chain of mbufs to flat buffer. */
            uint16_t copied = 0;
            int rc = ble_hs_mbuf_to_flat(ctxt->om, val.buf, val.max_len, &copied);
            if (rc != 0) {
                ESP_LOGE(TAG, "Mbuf flatten failed: %d", rc);
                return BLE_ATT_ERR_UNLIKELY;
            }
            val.len = copied;

            ESP_LOGD(TAG, "Write: conn=%d attr=%d len=%d",
                     conn_handle, attr_handle, copied);

            /* Notify user callback */
            if (srv._access_cb) {
                BLECharAccess access = {};
                access.conn_handle = conn_handle;
                access.attr_handle = attr_handle;
                access.op = ctxt->op;
                access.data = val.buf;
                access.data_len = val.len;
                if (ctxt->chr) {
                    memcpy(&access.char_uuid, ctxt->chr->uuid, sizeof(ble_uuid_any_t));
                }
                srv._access_cb(&access);
            }
            return 0;
        }

        default:
            ESP_LOGW(TAG, "Unhandled access op: %d", ctxt->op);
            return BLE_ATT_ERR_UNLIKELY;
    }
}

/* =============================================================================
 * NOTIFY / INDICATE
 * ========================================================================== */

esp_err_t BLEServer::notify(uint16_t attr_handle, const uint8_t* data,
                             uint16_t len, uint16_t conn_handle) {
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;

    /* Build an mbuf with the notification data */
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        ESP_LOGE(TAG, "Failed to allocate mbuf for notify");
        return ESP_ERR_NO_MEM;
    }

    if (conn_handle == 0xFFFF) {
        /* Send to all connected clients.
         * We need a fresh mbuf for each connection since NimBLE consumes it. */
        BLEManager& mgr = BLEManager::instance();
        bool sent = false;
        for (int i = 0; i < BLE_MAX_CONNECTIONS; i++) {
            if (mgr.isConnected(i)) {
                struct os_mbuf* om_copy;
                if (sent) {
                    /* Need a new mbuf for subsequent sends */
                    om_copy = ble_hs_mbuf_from_flat(data, len);
                    if (!om_copy) continue;
                } else {
                    om_copy = om;
                    sent = true;
                }
                ble_gatts_notify_custom(i, attr_handle, om_copy);
            }
        }
        if (!sent) {
            os_mbuf_free_chain(om);  // Nobody to send to
        }
        return ESP_OK;
    }

    int rc = ble_gatts_notify_custom(conn_handle, attr_handle, om);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t BLEServer::indicate(uint16_t attr_handle, const uint8_t* data,
                               uint16_t len, uint16_t conn_handle) {
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;

    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
    if (!om) return ESP_ERR_NO_MEM;

    int rc = ble_gatts_indicate_custom(conn_handle, attr_handle, om);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t BLEServer::setValue(uint16_t attr_handle, const uint8_t* data, uint16_t len) {
    /* Find the characteristic by scanning stored handles */
    for (int i = 0; i < _total_chars; i++) {
        /* Check all services for a matching handle pointer */
        for (int s = 0; s < _svc_count; s++) {
            for (int c = 0; c < _svcs[s].char_count; c++) {
                if (_svcs[s].char_val_handles[c] &&
                    *_svcs[s].char_val_handles[c] == attr_handle) {
                    /* Found it - update the value */
                    CharValueRef& val = _char_values[i];
                    uint16_t copy_len = (len > val.max_len) ? val.max_len : len;
                    memcpy(val.buf, data, copy_len);
                    val.len = copy_len;
                    return ESP_OK;
                }
            }
        }
    }
    return ESP_ERR_NOT_FOUND;
}

void BLEServer::setAccessCallback(BLEAccessCb cb) {
    _access_cb = cb;
}
