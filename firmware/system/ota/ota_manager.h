/*
 * =============================================================================
 * FILE:        ota_manager.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-14
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    ESP32 / ESP32-S3 / ESP32-C6 (ESP-IDF v5.x)
 * =============================================================================
 * 
 * OTA Manager - Over-The-Air firmware update management.
 * 
 * Provides a complete OTA solution:
 *   - Web UI with drag & drop firmware upload
 *   - Semantic version tracking (stored in NVS)
 *   - Pull-based updates from HTTP server (version check + download)
 *   - Push-based updates via HTTP POST upload
 *   - Rollback protection with configurable validation timeout
 *   - Partition info reporting
 * 
 * =============================================================================
 * BEGINNER'S GUIDE: OTA UPDATES
 * =============================================================================
 * 
 * WHAT IS OTA?
 * ~~~~~~~~~~~~
 * OTA = Over-The-Air. It means updating your device's firmware wirelessly
 * instead of plugging in a USB cable. Essential for deployed devices.
 * 
 * 
 * HOW ESP32 OTA WORKS:
 * ~~~~~~~~~~~~~~~~~~~~
 * 
 * The ESP32 flash is divided into partitions. For OTA, you need at least:
 * 
 *     ┌──────────────────────────────────────────────┐
 *     │                 FLASH (4MB)                   │
 *     ├──────────┬──────────┬──────────┬─────────────┤
 *     │ Bootload │  NVS     │  OTA     │             │
 *     │   er     │  Data    │  Data    │             │
 *     │ (0x7000) │ (0x6000) │ (0x2000) │             │
 *     ├──────────┴──────────┴──────────┤             │
 *     │         ota_0 (app)            │             │
 *     │     Running firmware           │             │
 *     │        (~1.5 MB)               │             │
 *     ├────────────────────────────────┤             │
 *     │         ota_1 (app)            │             │
 *     │     New firmware goes here     │             │
 *     │        (~1.5 MB)               │             │
 *     └────────────────────────────────┴─────────────┘
 * 
 *   1. Current firmware runs from ota_0
 *   2. New firmware is written to ota_1
 *   3. OTA Data partition is updated: "boot from ota_1 next time"
 *   4. Device reboots → now running from ota_1
 *   5. Next OTA writes to ota_0 (they alternate)
 * 
 * 
 * ROLLBACK PROTECTION:
 * ~~~~~~~~~~~~~~~~~~~~
 * What if the new firmware is buggy and crashes on boot?
 * 
 *     ┌─────────────┐      ┌─────────────┐      ┌─────────────┐
 *     │  OTA Write   │─────►│   Reboot    │─────►│ New firmware │
 *     │  new image   │      │             │      │  starts up   │
 *     └─────────────┘      └─────────────┘      └──────┬──────┘
 *                                                       │
 *                                              Validation timer
 *                                              starts (e.g. 60s)
 *                                                       │
 *                                              ┌────────┴────────┐
 *                                              │                 │
 *                                         validate()        Timer expires
 *                                         called             (no validate)
 *                                              │                 │
 *                                         Mark VALID        AUTO-ROLLBACK
 *                                         Stay on new      Reboot to old
 *                                              │                 │
 *                                              ▼                 ▼
 *                                          SUCCESS            SAFE!
 * 
 * The firmware must call validate() within the timeout to confirm
 * it's working. If it crashes before that, the bootloader
 * automatically rolls back to the previous good firmware.
 * 
 * 
 * VERSION CHECKING:
 * ~~~~~~~~~~~~~~~~~
 * Uses semantic versioning: MAJOR.MINOR.PATCH (e.g., "1.2.3")
 * 
 *   Server has manifest.json:
 *     { "version": "1.3.0", "url": "http://server/firmware.bin" }
 * 
 *   Device checks: "I'm 1.2.3, server has 1.3.0 → update available!"
 * 
 * 
 * =============================================================================
 * USAGE EXAMPLES
 * =============================================================================
 * 
 * MINIMAL (web upload only):
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~
 *     OTAManager& ota = OTAManager::instance();
 *     ota.begin();
 *     ota.registerUploadHandler(http_server_handle);
 *     ota.registerWebUI(http_server_handle);
 *     // → Browse to http://device.local/ota
 * 
 * 
 * WITH ROLLBACK:
 * ~~~~~~~~~~~~~~
 *     OTAManager& ota = OTAManager::instance();
 *     ota.begin();
 *     // ... run self-tests, verify hardware, etc ...
 *     if (everything_ok) {
 *         ota.validate();  // Mark firmware as good
 *     }
 *     // If validate() isn't called within 60s → auto rollback
 * 
 * 
 * AUTO-UPDATE FROM SERVER:
 * ~~~~~~~~~~~~~~~~~~~~~~~~
 *     ota.setUpdateURL("http://192.168.1.100:8080/firmware");
 *     ota.checkForUpdate();  // Checks version, downloads if newer
 * 
 * =============================================================================
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

/* ─── Constants ──────────────────────────────────────────────────────────── */
#define OTA_MAX_VERSION_LEN     32
#define OTA_MAX_URL_LEN         256
#define OTA_NVS_NAMESPACE       "ota_mgr"
#define OTA_RECV_BUF_SIZE       4096
#define OTA_DEFAULT_TIMEOUT_S   60      ///< Default rollback timeout in seconds

/* ─── Event Types ────────────────────────────────────────────────────────── */

enum class OTAEvent {
    UPDATE_STARTED,     ///< OTA write has begun
    PROGRESS,           ///< Chunk written (check OTAEventInfo::progress_pct)
    UPDATE_COMPLETE,    ///< OTA write finished, pending reboot
    UPDATE_FAILED,      ///< OTA failed (check OTAEventInfo::error_msg)
    ROLLBACK_PENDING,   ///< Running unvalidated firmware
    VALIDATED,          ///< Firmware marked as good
    ROLLED_BACK,        ///< Rolled back to previous firmware
    VERSION_CHECK,      ///< Version check result (check OTAEventInfo::update_available)
};

struct OTAEventInfo {
    float       progress_pct;       ///< 0.0 - 100.0
    uint32_t    bytes_written;      ///< Total bytes written so far
    uint32_t    total_size;         ///< Total image size (0 if unknown)
    bool        update_available;   ///< True if server has newer version
    char        new_version[OTA_MAX_VERSION_LEN];
    char        error_msg[128];
};

using OTAEventCb = std::function<void(OTAEvent event, const OTAEventInfo* info)>;

/* ─── Partition Info ─────────────────────────────────────────────────────── */

struct OTAPartitionInfo {
    char        running_label[16];      ///< e.g., "ota_0"
    char        running_version[OTA_MAX_VERSION_LEN];
    uint32_t    running_address;
    uint32_t    running_size;
    char        next_label[16];         ///< e.g., "ota_1"
    uint32_t    next_address;
    uint32_t    next_size;
    bool        rollback_possible;      ///< Previous partition has valid firmware
    bool        pending_verify;         ///< Current firmware is unvalidated
};

/* ─── Semantic Version ───────────────────────────────────────────────────── */

struct SemVer {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;

    bool operator>(const SemVer& other) const {
        if (major != other.major) return major > other.major;
        if (minor != other.minor) return minor > other.minor;
        return patch > other.patch;
    }
    bool operator==(const SemVer& other) const {
        return major == other.major && minor == other.minor && patch == other.patch;
    }
    bool operator!=(const SemVer& other) const { return !(*this == other); }
};

/* ─── Main Class ─────────────────────────────────────────────────────────── */

class OTAManager {
public:
    static OTAManager& instance();
    OTAManager(const OTAManager&) = delete;
    OTAManager& operator=(const OTAManager&) = delete;

    /* ─── Lifecycle ────────────────────────────────────────────────────── */

    /**
     * @brief Initialize OTA manager.
     * 
     * Reads current version from NVS, checks rollback state,
     * starts validation timer if firmware is pending verify.
     * 
     * @param validation_timeout_s  Seconds before auto-rollback (0 = disabled)
     * @return ESP_OK on success
     */
    esp_err_t begin(uint32_t validation_timeout_s = OTA_DEFAULT_TIMEOUT_S);

    /* ─── Version Management ───────────────────────────────────────────── */

    /**
     * @brief Get current firmware version string.
     * 
     * Returns the version from esp_app_desc (set at compile time via
     * PROJECT_VER in CMakeLists.txt or build flags).
     */
    const char* getVersion() const;

    /**
     * @brief Parse a version string into SemVer components.
     * @param str  Version string (e.g., "1.2.3")
     * @param ver  Output SemVer struct
     * @return true if parsed successfully
     */
    static bool parseVersion(const char* str, SemVer& ver);

    /**
     * @brief Format SemVer to string.
     * @param ver  SemVer struct
     * @param buf  Output buffer (must be >= OTA_MAX_VERSION_LEN)
     */
    static void versionToStr(const SemVer& ver, char* buf);

    /* ─── Firmware Upload (Push) ───────────────────────────────────────── */

    /**
     * @brief Register HTTP POST handler for firmware upload.
     * 
     * Accepts binary firmware at POST /api/ota/upload
     * Streams directly to flash (no full-image buffering needed).
     * 
     * @param server  HTTP server handle (from WiFiHttpServer or httpd_start)
     * @return ESP_OK on success
     */
    esp_err_t registerUploadHandler(httpd_handle_t server);

    /**
     * @brief Register the web UI page at GET /ota
     * 
     * Serves an embedded HTML page with:
     *   - Drag & drop firmware upload
     *   - Progress bar
     *   - Current version display
     *   - Partition info
     *   - Rollback button
     * 
     * @param server  HTTP server handle
     * @return ESP_OK on success
     */
    esp_err_t registerWebUI(httpd_handle_t server);

    /* ─── Firmware Download (Pull) ─────────────────────────────────────── */

    /**
     * @brief Set the URL for checking/downloading updates.
     * 
     * The URL should point to a directory with:
     *   - manifest.json: { "version": "x.y.z", "file": "firmware.bin" }
     *   - firmware.bin: the actual firmware binary
     * 
     * @param base_url  Base URL (e.g., "http://192.168.1.100:8080/firmware")
     */
    void setUpdateURL(const char* base_url);

    /**
     * @brief Check if an update is available on the server.
     * 
     * Downloads manifest.json, compares version with current.
     * Reports result via OTAEvent::VERSION_CHECK callback.
     * 
     * @param auto_update  If true and update available, download immediately
     * @return ESP_OK if check succeeded (doesn't mean update is available)
     */
    esp_err_t checkForUpdate(bool auto_update = false);

    /**
     * @brief Download and flash firmware from URL.
     * 
     * @param url  Direct URL to .bin file
     * @return ESP_OK on success (device will reboot)
     */
    esp_err_t updateFromURL(const char* url);

    /* ─── Rollback & Validation ────────────────────────────────────────── */

    /**
     * @brief Mark current firmware as valid (cancel rollback).
     * 
     * MUST be called after successful boot to prevent auto-rollback.
     * Call this after your application passes self-tests.
     * 
     * @return ESP_OK on success
     */
    esp_err_t validate();

    /**
     * @brief Manually trigger rollback to previous firmware.
     * @return ESP_OK if rollback initiated (device will reboot)
     */
    esp_err_t rollback();

    /**
     * @brief Check if current firmware is pending validation.
     */
    bool isPendingValidation() const;

    /* ─── Partition Info ───────────────────────────────────────────────── */

    /**
     * @brief Get detailed partition information.
     */
    esp_err_t getPartitionInfo(OTAPartitionInfo& info) const;

    /* ─── Callbacks ────────────────────────────────────────────────────── */

    void setEventCallback(OTAEventCb cb);

    /* ─── Status API (for web UI) ──────────────────────────────────────── */

    /**
     * @brief Register JSON status endpoint at GET /api/ota/status
     * 
     * Returns: { "version", "partition", "pending_verify",
     *            "rollback_possible", "uptime" }
     */
    esp_err_t registerStatusHandler(httpd_handle_t server);

private:
    OTAManager();
    ~OTAManager();

    /* HTTP handlers */
    static esp_err_t uploadHandler(httpd_req_t* req);
    static esp_err_t webUIHandler(httpd_req_t* req);
    static esp_err_t statusHandler(httpd_req_t* req);
    static esp_err_t rollbackHandler(httpd_req_t* req);

    /* Validation timer */
    static void validationTimerCb(TimerHandle_t timer);

    void emitEvent(OTAEvent event, const OTAEventInfo* info = nullptr);

    /* State */
    bool            _initialized;
    char            _version[OTA_MAX_VERSION_LEN];
    char            _update_url[OTA_MAX_URL_LEN];
    uint32_t        _validation_timeout_s;
    bool            _pending_verify;
    TimerHandle_t   _validation_timer;
    bool            _update_in_progress;

    OTAEventCb      _event_cb;
};

#endif // OTA_MANAGER_H
