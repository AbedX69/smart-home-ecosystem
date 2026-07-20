/*
 * =============================================================================
 * FILE:        config_store.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-17
 * VERSION:     1.1.0
 * MODIFIED:    2026-07-14 — exists() any-type fix, getString termination fix
 * =============================================================================
 */

#include "config_store.h"

static const char* TAG = "ConfigStore";

/* =============================================================================
 * SINGLETON
 * ========================================================================== */

ConfigStore& ConfigStore::instance() {
    static ConfigStore inst;
    return inst;
}

ConfigStore::ConfigStore()
    : _handle(0)
    , _open(false)
    , _change_cb(nullptr)
{}

ConfigStore::~ConfigStore() {
    if (_open) {
        nvs_close(_handle);
    }
}

/* =============================================================================
 * LIFECYCLE
 * ========================================================================== */

esp_err_t ConfigStore::begin(const char* ns) {
    if (_open) return ESP_OK;

    /* Initialize NVS flash (safe to call multiple times) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition issue, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Open namespace */
    ret = nvs_open(ns, NVS_READWRITE, &_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS open \"%s\" failed: %s", ns, esp_err_to_name(ret));
        return ret;
    }

    _open = true;
    ESP_LOGI(TAG, "Config store open (namespace: \"%s\")", ns);
    return ESP_OK;
}

esp_err_t ConfigStore::commit() {
    if (!_open) return ESP_ERR_INVALID_STATE;
    return nvs_commit(_handle);
}

esp_err_t ConfigStore::eraseAll() {
    if (!_open) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_erase_all(_handle);
    if (ret == ESP_OK) {
        ret = nvs_commit(_handle);
        ESP_LOGW(TAG, "All config erased (factory reset)");
    }
    return ret;
}

esp_err_t ConfigStore::eraseKey(const char* key) {
    if (!_open) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_erase_key(_handle, key);
    if (ret == ESP_OK) nvs_commit(_handle);
    return ret;
}

bool ConfigStore::exists(const char* key) {
    if (!_open) return false;
    /* v1.1 fix: the old probe only tried str/u8/blob, so existing
     * u16/u32/i8/i16/i32 keys reported "not found". nvs_find_key
     * (IDF >= 5.2) checks ANY type in one call. */
    nvs_type_t type;
    return nvs_find_key(_handle, key, &type) == ESP_OK;
}

bool ConfigStore::isOpen() const { return _open; }

/* =============================================================================
 * STRING
 * ========================================================================== */

esp_err_t ConfigStore::setString(const char* key, const char* value) {
    if (!_open) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_set_str(_handle, key, value);
    if (ret == ESP_OK) {
        nvs_commit(_handle);
        notifyChange(key);
    }
    return ret;
}

esp_err_t ConfigStore::getString(const char* key, char* out, size_t max_len,
                                   const char* default_val) {
    if (!out || max_len == 0) return ESP_ERR_INVALID_ARG;
    if (!_open) {
        strncpy(out, default_val, max_len);
        out[max_len - 1] = '\0';   /* v1.1 fix: was left unterminated */
        return ESP_ERR_INVALID_STATE;
    }

    size_t len = max_len;
    esp_err_t ret = nvs_get_str(_handle, key, out, &len);
    if (ret != ESP_OK) {
        strncpy(out, default_val, max_len);
        out[max_len - 1] = '\0';
    }
    return ret;
}

/* =============================================================================
 * INTEGERS
 * ========================================================================== */

esp_err_t ConfigStore::setU8(const char* key, uint8_t value) {
    if (!_open) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_set_u8(_handle, key, value);
    if (ret == ESP_OK) { nvs_commit(_handle); notifyChange(key); }
    return ret;
}

uint8_t ConfigStore::getU8(const char* key, uint8_t default_val) {
    uint8_t val = default_val;
    if (_open) nvs_get_u8(_handle, key, &val);
    return val;
}

esp_err_t ConfigStore::setI8(const char* key, int8_t value) {
    if (!_open) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_set_i8(_handle, key, value);
    if (ret == ESP_OK) { nvs_commit(_handle); notifyChange(key); }
    return ret;
}

int8_t ConfigStore::getI8(const char* key, int8_t default_val) {
    int8_t val = default_val;
    if (_open) nvs_get_i8(_handle, key, &val);
    return val;
}

esp_err_t ConfigStore::setU16(const char* key, uint16_t value) {
    if (!_open) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_set_u16(_handle, key, value);
    if (ret == ESP_OK) { nvs_commit(_handle); notifyChange(key); }
    return ret;
}

uint16_t ConfigStore::getU16(const char* key, uint16_t default_val) {
    uint16_t val = default_val;
    if (_open) nvs_get_u16(_handle, key, &val);
    return val;
}

esp_err_t ConfigStore::setI16(const char* key, int16_t value) {
    if (!_open) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_set_i16(_handle, key, value);
    if (ret == ESP_OK) { nvs_commit(_handle); notifyChange(key); }
    return ret;
}

int16_t ConfigStore::getI16(const char* key, int16_t default_val) {
    int16_t val = default_val;
    if (_open) nvs_get_i16(_handle, key, &val);
    return val;
}

esp_err_t ConfigStore::setU32(const char* key, uint32_t value) {
    if (!_open) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_set_u32(_handle, key, value);
    if (ret == ESP_OK) { nvs_commit(_handle); notifyChange(key); }
    return ret;
}

uint32_t ConfigStore::getU32(const char* key, uint32_t default_val) {
    uint32_t val = default_val;
    if (_open) nvs_get_u32(_handle, key, &val);
    return val;
}

esp_err_t ConfigStore::setI32(const char* key, int32_t value) {
    if (!_open) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_set_i32(_handle, key, value);
    if (ret == ESP_OK) { nvs_commit(_handle); notifyChange(key); }
    return ret;
}

int32_t ConfigStore::getI32(const char* key, int32_t default_val) {
    int32_t val = default_val;
    if (_open) nvs_get_i32(_handle, key, &val);
    return val;
}

/* =============================================================================
 * BOOL
 * ========================================================================== */

esp_err_t ConfigStore::setBool(const char* key, bool value) {
    return setU8(key, value ? 1 : 0);
}

bool ConfigStore::getBool(const char* key, bool default_val) {
    return getU8(key, default_val ? 1 : 0) != 0;
}

/* =============================================================================
 * FLOAT
 * =============================================================================
 * NVS doesn't support float. We bitcast to uint32_t (same size).
 * This preserves exact IEEE 754 representation.
 * ========================================================================== */

esp_err_t ConfigStore::setFloat(const char* key, float value) {
    uint32_t raw;
    memcpy(&raw, &value, sizeof(raw));
    return setU32(key, raw);
}

float ConfigStore::getFloat(const char* key, float default_val) {
    uint32_t raw;
    memcpy(&raw, &default_val, sizeof(raw));
    raw = getU32(key, raw);
    float result;
    memcpy(&result, &raw, sizeof(result));
    return result;
}

/* =============================================================================
 * BLOB
 * ========================================================================== */

esp_err_t ConfigStore::setBlob(const char* key, const void* data, size_t len) {
    if (!_open) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_set_blob(_handle, key, data, len);
    if (ret == ESP_OK) { nvs_commit(_handle); notifyChange(key); }
    return ret;
}

esp_err_t ConfigStore::getBlob(const char* key, void* out, size_t* len) {
    if (!_open) return ESP_ERR_INVALID_STATE;
    return nvs_get_blob(_handle, key, out, len);
}

/* =============================================================================
 * CALLBACK
 * ========================================================================== */

void ConfigStore::setChangeCallback(ConfigChangeCb cb) { _change_cb = cb; }

void ConfigStore::notifyChange(const char* key) {
    if (_change_cb) _change_cb(key);
}
