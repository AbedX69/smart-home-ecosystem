/*
 * =============================================================================
 * FILE:        ota_manager.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-14
 * UPDATED:     2026-07-28
 * VERSION:     2.0.0
 * LICENSE:     MIT
 * PLATFORM:    ESP32 / ESP32-S3 / ESP32-C6 (ESP-IDF v5.x)
 * =============================================================================
 * 
 * OTA Manager - Over-The-Air firmware update management.
 * 
 * Transport-neutral update engine:
 *   - Sink API (beginWrite / writeChunk / finishWrite / abortWrite)
 *   - Rollback protection with configurable validation timeout
 *   - Semantic version tracking
 *   - Partition info reporting
 * 
 * The HTTP half - web UI, drag & drop upload, pull-from-URL - lives in the
 * separate `ota_http` component and is an ordinary consumer of the sink.
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
 * WHAT CHANGED IN v2.0.0
 * =============================================================================
 * 
 * v1.0.0 was HTTP-only: the upload handler owned the esp_ota_* calls, and the
 * public API took httpd_handle_t, which dragged esp_http_server into every
 * consumer's build - including leaf nodes that will never serve a web page.
 * 
 * v2.0.0 splits that in two:
 * 
 *   ota_manager  (this file)  lifecycle, rollback, validation, and the SINK.
 *                             Depends only on app_update / esp_partition /
 *                             esp_timer / freertos. This is what a strip node
 *                             on a ceiling requires.
 * 
 *   ota_http     (separate)   HTTP upload handler, web UI, pull-from-URL.
 *                             Hub only.
 * 
 * The ESP-NOW bulk plane becomes a third consumer of the same sink. All three
 * transports share one esp_ota engine, so there is exactly one place where a
 * write can go wrong.
 * 
 * 
 * THE SINK:
 * ~~~~~~~~~
 *     beginWrite(total)     esp_ota_begin on the next slot. Erases it.
 *     writeChunk(buf, len)  esp_ota_write. Call as many times as needed.
 *     finishWrite()         esp_ota_end + set_boot_partition. No reboot.
 *     abortWrite()          esp_ota_abort. Safe to call at any point.
 * 
 * finishWrite() deliberately does NOT reboot. The caller decides when - an
 * HTTP handler wants to answer the request first; an ESP-NOW transfer wants
 * to ACK the final chunk first. Rebooting inside the sink would cut both off.
 * 
 * 
 * TARGET CHECKING - deliberately NOT here:
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * The sink does not inspect what it is writing. Refusing an image meant for a
 * different kind of device is the job of the OTA offer handshake, which
 * compares DeviceRole (core_types.h) BEFORE any bytes transfer. Cheaper than
 * discovering it after a multi-minute radio transfer, and it matches on a role
 * byte rather than on any name - so build names and user-facing device names
 * can both change freely without ever breaking an update.
 * 
 * 
 * REQUIRES CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y:
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Without it the running partition never reports ESP_OTA_IMG_PENDING_VERIFY,
 * the timer never arms, validate() is a no-op, and this whole mechanism
 * silently does nothing while looking correct. Set in sdkconfig.defaults for
 * both smart-light apps as of 28/07/2026.
 * 
 * 
 * WHAT validate() SHOULD MEAN:
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Not "do the LEDs work" but "can I still be reached and updated". A strip
 * with dead LEDs and a live radio is fixable from the couch. A strip with
 * perfect LEDs and a broken receive path is a ladder. Gate validate() on
 * proof of round-trip comms, not on peripherals.
 * 
 * =============================================================================
 * USAGE EXAMPLES
 * =============================================================================
 * 
 * MINIMAL (web upload only) - hub, needs the ota_http component:
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *     OTAManager::instance().begin();
 *     ota_http_register_all(http_server_handle);
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
 * AUTO-UPDATE FROM SERVER (hub, ota_http component):
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *     ota_http_set_update_url("http://192.168.1.100:8080/firmware");
 *     ota_http_check_for_update();  // Checks version, downloads if newer
 * 
 * 
 * LEAF NODE (strip) - core component only, no HTTP:
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *     OTAManager& ota = OTAManager::instance();
 *     ota.begin(60);                    // arms rollback timer if pending
 * 
 *     ... bring up radio, protocol, pairing ...
 * 
 *     if (ota.isPendingValidation()) {
 *         // send a reliable PING to the paired controller; call
 *         // ota.validate() from the delivery callback when it is ACKed
 *     }
 * 
 * 
 * ANY TRANSPORT (HTTP, ESP-NOW, LoRa later):
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *     ota.beginWrite(image_size);
 *     while (more) ota.writeChunk(buf, n);
 *     ota.finishWrite();
 *     esp_restart();                    // caller's choice, when ready
 * 
 *     // on any error:
 *     ota.abortWrite();
 * 
 * =============================================================================
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>

#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

/* ─── Constants ──────────────────────────────────────────────────────────── */
#define OTA_MAX_VERSION_LEN     32
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
     * Reads the running version from esp_app_desc, checks rollback state,
     * starts the validation timer if firmware is pending verify.
     * 
     * Call early in app_main. Safe to call before the radio exists.
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
     * CONFIG_APP_PROJECT_VER in sdkconfig.defaults).
     */
    const char* getVersion() const;

    /**
     * @brief Get the running firmware's project name, from esp_app_desc.
     * 
     * Set by project() in the app's top-level CMakeLists.txt.
     * For logging and offer handshakes - NOT used as a target check.
     */
    const char* getProjectName() const;

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

    /* ─── The Sink - used by every transport ───────────────────────────── */

    /**
     * @brief Open the next OTA slot for writing. Erases it.
     * 
     * @param total_size  Expected image size, or 0 if unknown. Passing the
     *                    real size lets esp_ota_begin erase only what it
     *                    needs, which is much faster than erasing the slot.
     * @return ESP_ERR_INVALID_STATE if a write is already open.
     */
    esp_err_t beginWrite(size_t total_size = 0);

    /**
     * @brief Append to the open slot. Must follow a successful beginWrite.
     */
    esp_err_t writeChunk(const void* data, size_t len);

    /**
     * @brief Close the image, verify it, set it as the boot partition.
     * 
     * esp_ota_end() rejects a truncated or corrupt image here, so a failed
     * return means the slot was NOT made bootable - the running image is
     * untouched and still bootable.
     * 
     * Does NOT reboot. Caller decides when.
     */
    esp_err_t finishWrite();

    /**
     * @brief Discard the open write. Safe to call when none is open.
     */
    void abortWrite();

    bool     isWriteInProgress() const;
    uint32_t bytesWritten() const;
    uint32_t expectedSize() const;

    /**
     * @brief Append to the open slot at an explicit offset.
     *
     * For transports where chunks arrive out of order (the ESP-NOW bulk
     * plane). Backed by esp_ota_write_with_offset(), which is safe ONLY
     * because beginWrite() passes a real size to esp_ota_begin() and the
     * whole range is erased up front.
     *
     * Do NOT mix with writeChunk() in one session - the IDF advises
     * against it and _write_mode enforces it.
     *
     * Duplicate offsets are the caller's problem: _bytes_written counts
     * bytes accepted, so writing a chunk twice inflates it and breaks the
     * completeness check in finishWrite(). Gate on a bitmap.
     */
    esp_err_t writeChunkAt(const void* data, size_t len, uint32_t offset);

    /**
     * @brief CRC32 the first len bytes of the open slot and compare.
     *
     * Call after the last chunk lands, before finishWrite(). Catches a
     * chunk written at the wrong offset with valid-looking content -
     * something esp_ota_end()'s image check cannot see.
     *
     * Standard zlib CRC-32: seed 0, chained, no final inversion. Matches
     * Python zlib.crc32(). Sanity vector: CRC32("123456789") = 0xCBF43926.
     *
     * @return ESP_OK on match, ESP_ERR_INVALID_CRC on mismatch.
     */
    esp_err_t verifyCrc32(uint32_t expected, uint32_t len);

    /* ─── Rollback & Validation ────────────────────────────────────────── */

    /**
     * @brief Mark current firmware as valid (cancel rollback).
     * 
     * MUST be called after successful boot to prevent auto-rollback.
     * Call this only once the device has proven it is still reachable -
     * see "WHAT validate() SHOULD MEAN" at the top of this file.
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

    /**
     * @brief Emit an event to the registered callback.
     * 
     * Public so ota_http (and later the ESP-NOW plane) can report
     * transport-level outcomes through the same channel as the sink,
     * rather than each transport inventing its own reporting path.
     */
    void emitEvent(OTAEvent event, const OTAEventInfo* info = nullptr);

private:
    OTAManager();
    ~OTAManager();

    /* Validation timer */
    static void validationTimerCb(TimerHandle_t timer);

    /* Lifecycle state */
    bool            _initialized;
    char            _version[OTA_MAX_VERSION_LEN];
    char            _project_name[OTA_MAX_VERSION_LEN];
    uint32_t        _validation_timeout_s;
    bool            _pending_verify;
    TimerHandle_t   _validation_timer;

    /* Sink state */
    esp_ota_handle_t        _write_handle;
    const esp_partition_t*  _write_partition;
    uint32_t                _bytes_written;
    uint32_t                _expected_size;
    bool                    _write_open;

    /* Which write API this session committed to. Mixing sequential and
     * offset writes corrupts the slot silently. */
    enum class WriteMode : uint8_t { NONE, SEQUENTIAL, OFFSET };
    WriteMode               _write_mode;

    OTAEventCb      _event_cb;
};

#endif // OTA_MANAGER_H