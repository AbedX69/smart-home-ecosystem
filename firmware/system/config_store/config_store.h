/*
 * =============================================================================
 * FILE:        config_store.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-17
 * VERSION:     1.1.0
 * LICENSE:     MIT
 * PLATFORM:    All ESP32 variants (ESP-IDF v5.x)
 * =============================================================================
 * 
 * Config Store — NVS-based persistent configuration for any device.
 * 
 * Provides:
 *   - Typed key-value storage (string, int, bool, float, blob)
 *   - Predefined keys for common device settings
 *   - Namespace isolation per device type
 *   - Factory reset (erase all config)
 *   - Change callbacks (get notified when config changes)
 * 
 * =============================================================================
 * BEGINNER'S GUIDE: NVS (Non-Volatile Storage)
 * =============================================================================
 * 
 * NVS is flash-based key-value storage built into ESP-IDF. Data survives
 * reboots and power cycles. Think of it as a tiny persistent database.
 * 
 *     ┌─────────────────────────────────────────────┐
 *     │  NVS Flash                                  │
 *     │  ┌──────────────┐  ┌──────────────┐        │
 *     │  │ "smarthome"  │  │ "wifi"       │        │
 *     │  │  name="Kit"  │  │  ssid=...    │        │
 *     │  │  role=0x02   │  │  pass=...    │        │
 *     │  │  group=1     │  │              │        │
 *     │  └──────────────┘  └──────────────┘        │
 *     │   (namespace)        (namespace)            │
 *     └─────────────────────────────────────────────┘
 * 
 * ConfigStore wraps NVS with a clean C++ API and adds:
 *   - Standard keys every device uses (name, role, group)
 *   - Float support (NVS doesn't natively support floats)
 *   - Change notification callbacks
 *   - Bulk read of device identity
 * 
 * 
 * =============================================================================
 * USAGE
 * =============================================================================
 * 
 *     ConfigStore& cfg = ConfigStore::instance();
 *     cfg.begin();  // or cfg.begin("mydevice") for custom namespace
 * 
 *     // Write settings
 *     cfg.setString("device_name", "Kitchen Light");
 *     cfg.setU8("device_role", (uint8_t)DeviceRole::LIGHT);
 *     cfg.setU8("group_id", 1);
 *     cfg.setBool("auto_on", true);
 *     cfg.setFloat("brightness_default", 0.75f);
 * 
 *     // Read settings
 *     char name[32];
 *     cfg.getString("device_name", name, sizeof(name), "Unnamed");
 *     uint8_t role = cfg.getU8("device_role", 0);
 *     bool autoOn = cfg.getBool("auto_on", false);
 * 
 *     // Factory reset
 *     cfg.eraseAll();
 * 
 * =============================================================================
 */

#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

/* ─── Constants ──────────────────────────────────────────────────────────── */

#define CONFIG_NAMESPACE_DEFAULT    "smarthome"
#define CONFIG_KEY_MAX_LEN          15      // NVS key max is 15 chars

/* ─── Standard Keys ──────────────────────────────────────────────────────── */
/*
 * Every device in the ecosystem uses these keys.
 * Custom keys can also be stored alongside them.
 */
namespace ConfigKeys {
    constexpr const char* DEVICE_NAME   = "dev_name";       ///< char[32]
    constexpr const char* DEVICE_ROLE   = "dev_role";       ///< uint8_t
    constexpr const char* GROUP_ID      = "group_id";       ///< uint8_t (0=ungrouped)
    constexpr const char* FW_MAJOR      = "fw_major";       ///< uint8_t
    constexpr const char* FW_MINOR      = "fw_minor";       ///< uint8_t
    constexpr const char* TRANSPORTS    = "transports";     ///< uint8_t bitmask
    constexpr const char* HEARTBEAT_MS  = "heartbeat_ms";   ///< uint32_t
    constexpr const char* PAIRED_MAC_1  = "pair_mac_1";     ///< blob (6 bytes)
    constexpr const char* PAIRED_MAC_2  = "pair_mac_2";     ///< blob (6 bytes)
    constexpr const char* PAIRED_MAC_3  = "pair_mac_3";     ///< blob (6 bytes)
    constexpr const char* CONFIGURED    = "configured";     ///< bool (first-run flag)
    constexpr const char* WIFI_CHANNEL  = "wifi_chan";      ///< uint8_t (0 = default)
}

/* ─── Callback ───────────────────────────────────────────────────────────── */

using ConfigChangeCb = std::function<void(const char* key)>;

/* ─── Main Class ─────────────────────────────────────────────────────────── */

class ConfigStore {
public:
    static ConfigStore& instance();
    ConfigStore(const ConfigStore&) = delete;
    ConfigStore& operator=(const ConfigStore&) = delete;

    /* ─── Lifecycle ────────────────────────────────────────────────────── */

    /**
     * @brief Initialize NVS and open the config namespace.
     * @param ns  Namespace name (default "smarthome", max 15 chars)
     */
    esp_err_t begin(const char* ns = CONFIG_NAMESPACE_DEFAULT);

    /** @brief Commit any pending writes to flash */
    esp_err_t commit();

    /** @brief Erase all keys in this namespace (factory reset) */
    esp_err_t eraseAll();

    /** @brief Erase a single key */
    esp_err_t eraseKey(const char* key);

    /** @brief Check if a key exists */
    bool exists(const char* key);

    bool isOpen() const;

    /* ─── String ───────────────────────────────────────────────────────── */

    esp_err_t setString(const char* key, const char* value);
    esp_err_t getString(const char* key, char* out, size_t max_len,
                        const char* default_val = "");

    /* ─── Integers ─────────────────────────────────────────────────────── */

    esp_err_t setU8(const char* key, uint8_t value);
    uint8_t   getU8(const char* key, uint8_t default_val = 0);

    esp_err_t setI8(const char* key, int8_t value);
    int8_t    getI8(const char* key, int8_t default_val = 0);

    esp_err_t setU16(const char* key, uint16_t value);
    uint16_t  getU16(const char* key, uint16_t default_val = 0);

    esp_err_t setI16(const char* key, int16_t value);
    int16_t   getI16(const char* key, int16_t default_val = 0);

    esp_err_t setU32(const char* key, uint32_t value);
    uint32_t  getU32(const char* key, uint32_t default_val = 0);

    esp_err_t setI32(const char* key, int32_t value);
    int32_t   getI32(const char* key, int32_t default_val = 0);

    /* ─── Bool ─────────────────────────────────────────────────────────── */

    esp_err_t setBool(const char* key, bool value);
    bool      getBool(const char* key, bool default_val = false);

    /* ─── Float ────────────────────────────────────────────────────────── */
    /* NVS doesn't support float natively. We store as uint32_t (bitcast). */

    esp_err_t setFloat(const char* key, float value);
    float     getFloat(const char* key, float default_val = 0.0f);

    /* ─── Blob (raw bytes) ─────────────────────────────────────────────── */

    esp_err_t setBlob(const char* key, const void* data, size_t len);
    esp_err_t getBlob(const char* key, void* out, size_t* len);

    /* ─── Callbacks ────────────────────────────────────────────────────── */

    void setChangeCallback(ConfigChangeCb cb);

private:
    ConfigStore();
    ~ConfigStore();

    void notifyChange(const char* key);

    nvs_handle_t    _handle;
    bool            _open;
    ConfigChangeCb  _change_cb;
};

#endif // CONFIG_STORE_H
