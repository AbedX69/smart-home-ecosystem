/*
 * =============================================================================
 * FILE:        ota_manager.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-14
 * UPDATED:     2026-07-28
 * VERSION:     2.0.0
 * =============================================================================
 *
 * Core OTA engine. No HTTP, no URLs, no NVS - see ota_http for those.
 *
 * =============================================================================
 */

#include "ota_manager.h"

static const char* TAG = "OTAManager";

/* =============================================================================
 * SINGLETON
 * ========================================================================== */

OTAManager& OTAManager::instance() {
    static OTAManager inst;
    return inst;
}

OTAManager::OTAManager()
    : _initialized(false)
    , _validation_timeout_s(OTA_DEFAULT_TIMEOUT_S)
    , _pending_verify(false)
    , _validation_timer(nullptr)
    , _write_handle(0)
    , _write_partition(nullptr)
    , _bytes_written(0)
    , _expected_size(0)
    , _write_open(false)
    , _event_cb(nullptr)
{
    memset(_version, 0, sizeof(_version));
    memset(_project_name, 0, sizeof(_project_name));
}

OTAManager::~OTAManager() {
    if (_validation_timer) {
        xTimerDelete(_validation_timer, 0);
    }
}

/* =============================================================================
 * LIFECYCLE
 * =============================================================================
 *
 * On begin():
 *   1. Read version + project name from esp_app_desc (compiled into binary)
 *   2. Check OTA state - is this firmware pending validation?
 *   3. If pending, start a timer. If the timer expires before validate()
 *      is called, we rollback automatically.
 *
 * _pending_verify can only ever become true when the bootloader was built
 * with CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y. Without it esp_ota_get_state_
 * partition() never reports PENDING_VERIFY and everything below is inert.
 * ========================================================================== */

esp_err_t OTAManager::begin(uint32_t validation_timeout_s) {
    if (_initialized) return ESP_OK;

    _validation_timeout_s = validation_timeout_s;

    /* ── Identity from the compiled app descriptor ─────────────────── */
    const esp_app_desc_t* desc = esp_app_get_description();
    if (desc) {
        strncpy(_version, desc->version, OTA_MAX_VERSION_LEN - 1);
        strncpy(_project_name, desc->project_name, OTA_MAX_VERSION_LEN - 1);
    } else {
        strcpy(_version, "0.0.0");
        strcpy(_project_name, "unknown");
    }

    /* ── Check rollback state ──────────────────────────────────────── */
    esp_ota_img_states_t ota_state;
    const esp_partition_t* running = esp_ota_get_running_partition();

    if (running && esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        _pending_verify = (ota_state == ESP_OTA_IMG_PENDING_VERIFY);
    }

    /* ── Start validation timer if needed ──────────────────────────── */
    if (_pending_verify && _validation_timeout_s > 0) {
        ESP_LOGW(TAG, "Firmware pending validation! Auto-rollback in %lus",
                 (unsigned long)_validation_timeout_s);

        _validation_timer = xTimerCreate(
            "ota_validate",
            pdMS_TO_TICKS(_validation_timeout_s * 1000),
            pdFALSE,    // One-shot
            this,
            validationTimerCb
        );

        if (_validation_timer) {
            xTimerStart(_validation_timer, 0);
        } else {
            /* No timer means no auto-rollback. Say so loudly rather than
             * letting the caller believe it is protected. */
            ESP_LOGE(TAG, "Failed to create validation timer - NO auto-rollback!");
        }

        OTAEventInfo info = {};
        emitEvent(OTAEvent::ROLLBACK_PENDING, &info);
    }

    _initialized = true;

    /* ── Log partition info ────────────────────────────────────────── */
    OTAPartitionInfo pinfo = {};
    getPartitionInfo(pinfo);

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  OTA Manager initialized");
    ESP_LOGI(TAG, "  Build:      %s v%s", _project_name, _version);
    ESP_LOGI(TAG, "  Running:    %s @ 0x%08lX (%luKB)",
             pinfo.running_label, (unsigned long)pinfo.running_address,
             (unsigned long)(pinfo.running_size / 1024));
    ESP_LOGI(TAG, "  Next slot:  %s @ 0x%08lX (%luKB)",
             pinfo.next_label, (unsigned long)pinfo.next_address,
             (unsigned long)(pinfo.next_size / 1024));
    ESP_LOGI(TAG, "  Pending:    %s", _pending_verify ? "YES" : "no");
    ESP_LOGI(TAG, "  Rollback:   %s", pinfo.rollback_possible ? "available" : "n/a");
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    return ESP_OK;
}

/* =============================================================================
 * VERSION MANAGEMENT
 * ========================================================================== */

const char* OTAManager::getVersion() const     { return _version; }
const char* OTAManager::getProjectName() const { return _project_name; }

bool OTAManager::parseVersion(const char* str, SemVer& ver) {
    if (!str) return false;
    ver = {0, 0, 0};

    /* Skip leading 'v' or 'V' */
    if (*str == 'v' || *str == 'V') str++;

    int matched = sscanf(str, "%hu.%hu.%hu", &ver.major, &ver.minor, &ver.patch);
    return (matched >= 1);  // At least major version
}

void OTAManager::versionToStr(const SemVer& ver, char* buf) {
    snprintf(buf, OTA_MAX_VERSION_LEN, "%u.%u.%u", ver.major, ver.minor, ver.patch);
}

/* =============================================================================
 * THE SINK
 * =============================================================================
 *
 * Every transport - HTTP upload, HTTP download, ESP-NOW, LoRa later - goes
 * through these four calls. They are the only place in the codebase that
 * touches esp_ota_begin / write / end / abort.
 *
 * Not thread-safe by design: one write at a time, and beginWrite() refuses
 * if one is already open. Two transports racing for the same slot is a bug
 * worth failing loudly on, not something to silently serialise.
 * ========================================================================== */

esp_err_t OTAManager::beginWrite(size_t total_size) {
    if (!_initialized) {
        ESP_LOGE(TAG, "beginWrite() called before begin()");
        return ESP_ERR_INVALID_STATE;
    }

    if (_write_open) {
        ESP_LOGE(TAG, "Write already open (%lu bytes in) - refusing second",
                 (unsigned long)_bytes_written);
        return ESP_ERR_INVALID_STATE;
    }

    _write_partition = esp_ota_get_next_update_partition(nullptr);
    if (!_write_partition) {
        ESP_LOGE(TAG, "No OTA slot available - check the partition table");
        return ESP_ERR_NOT_FOUND;
    }

    /* Catch an oversized image before erasing a perfectly good slot. */
    if (total_size > 0 && total_size > _write_partition->size) {
        ESP_LOGE(TAG, "Image %lu B exceeds slot %s (%lu B) - refusing",
                 (unsigned long)total_size, _write_partition->label,
                 (unsigned long)_write_partition->size);
        _write_partition = nullptr;
        return ESP_ERR_INVALID_SIZE;
    }

    /* OTA_SIZE_UNKNOWN erases the whole slot, which is slow. Passing the
     * real size erases only what the image needs. */
    size_t begin_size = (total_size > 0) ? total_size : OTA_SIZE_UNKNOWN;

    esp_err_t err = esp_ota_begin(_write_partition, begin_size, &_write_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        _write_partition = nullptr;
        return err;
    }

    _write_open     = true;
    _bytes_written  = 0;
    _expected_size  = (uint32_t)total_size;

    if (total_size > 0) {
        ESP_LOGI(TAG, "Write opened on %s @ 0x%06lX, expecting %lu bytes",
                 _write_partition->label,
                 (unsigned long)_write_partition->address,
                 (unsigned long)total_size);
    } else {
        ESP_LOGI(TAG, "Write opened on %s @ 0x%06lX, size unknown (full erase)",
                 _write_partition->label,
                 (unsigned long)_write_partition->address);
    }

    OTAEventInfo info = {};
    info.total_size = _expected_size;
    emitEvent(OTAEvent::UPDATE_STARTED, &info);

    return ESP_OK;
}

esp_err_t OTAManager::writeChunk(const void* data, size_t len) {
    if (!_write_open)        return ESP_ERR_INVALID_STATE;
    if (!data || len == 0)   return ESP_ERR_INVALID_ARG;

    esp_err_t err = esp_ota_write(_write_handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed at %lu bytes: %s",
                 (unsigned long)_bytes_written, esp_err_to_name(err));

        OTAEventInfo info = {};
        info.bytes_written = _bytes_written;
        info.total_size    = _expected_size;
        snprintf(info.error_msg, sizeof(info.error_msg),
                 "write failed at %lu B: %s",
                 (unsigned long)_bytes_written, esp_err_to_name(err));
        emitEvent(OTAEvent::UPDATE_FAILED, &info);

        abortWrite();
        return err;
    }

    _bytes_written += (uint32_t)len;

    /* NOTE: one PROGRESS event per chunk. A 230-byte ESP-NOW chunk over a
     * ~900 KB image is ~3900 callbacks - throttle in the callback, not here,
     * so slower transports still get fine-grained progress. */
    OTAEventInfo info = {};
    info.bytes_written = _bytes_written;
    info.total_size    = _expected_size;
    info.progress_pct  = (_expected_size > 0)
                       ? (_bytes_written * 100.0f / _expected_size)
                       : 0.0f;
    emitEvent(OTAEvent::PROGRESS, &info);

    return ESP_OK;
}

esp_err_t OTAManager::finishWrite() {
    if (!_write_open) {
        ESP_LOGE(TAG, "finishWrite() with no write open");
        return ESP_ERR_INVALID_STATE;
    }

    OTAEventInfo info = {};
    info.bytes_written = _bytes_written;
    info.total_size    = _expected_size;

    /* A short image would fail esp_ota_end anyway, but catching it here
     * gives a message that says what actually went wrong. */
    if (_expected_size > 0 && _bytes_written != _expected_size) {
        ESP_LOGE(TAG, "Size mismatch: expected %lu B, received %lu B",
                 (unsigned long)_expected_size, (unsigned long)_bytes_written);
        snprintf(info.error_msg, sizeof(info.error_msg),
                 "size mismatch: %lu of %lu B",
                 (unsigned long)_bytes_written, (unsigned long)_expected_size);
        emitEvent(OTAEvent::UPDATE_FAILED, &info);
        abortWrite();
        return ESP_ERR_INVALID_SIZE;
    }

    /* esp_ota_end() consumes the handle whether it succeeds or fails. */
    const esp_partition_t* target = _write_partition;
    esp_err_t err = esp_ota_end(_write_handle);
    _write_open      = false;
    _write_handle    = 0;
    _write_partition = nullptr;

    if (err != ESP_OK) {
        /* Image rejected: bad magic, bad checksum, truncated, or failing
         * signature verification. The running image is untouched. */
        ESP_LOGE(TAG, "Image rejected by esp_ota_end: %s", esp_err_to_name(err));
        snprintf(info.error_msg, sizeof(info.error_msg),
                 "image rejected: %s", esp_err_to_name(err));
        emitEvent(OTAEvent::UPDATE_FAILED, &info);
        return err;
    }

    /* Log what we just accepted. Read-only - this is NOT a target check,
     * see the header note on where target checking belongs. */
    esp_app_desc_t d;
    if (target && esp_ota_get_partition_description(target, &d) == ESP_OK) {
        ESP_LOGI(TAG, "Staged image: %s v%s", d.project_name, d.version);
    }

    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot_partition failed: %s", esp_err_to_name(err));
        snprintf(info.error_msg, sizeof(info.error_msg),
                 "boot flip failed: %s", esp_err_to_name(err));
        emitEvent(OTAEvent::UPDATE_FAILED, &info);
        return err;
    }

    ESP_LOGI(TAG, "Image accepted: %lu bytes -> %s. Reboot when ready.",
             (unsigned long)_bytes_written,
             target ? target->label : "?");

    info.progress_pct = 100.0f;
    emitEvent(OTAEvent::UPDATE_COMPLETE, &info);

    return ESP_OK;
}

void OTAManager::abortWrite() {
    if (!_write_open) return;

    ESP_LOGW(TAG, "Write aborted after %lu bytes", (unsigned long)_bytes_written);

    esp_ota_abort(_write_handle);

    _write_open      = false;
    _write_handle    = 0;
    _write_partition = nullptr;
    _bytes_written   = 0;
    _expected_size   = 0;
}

bool     OTAManager::isWriteInProgress() const { return _write_open; }
uint32_t OTAManager::bytesWritten() const      { return _bytes_written; }
uint32_t OTAManager::expectedSize() const      { return _expected_size; }

/* =============================================================================
 * ROLLBACK & VALIDATION
 * ========================================================================== */

esp_err_t OTAManager::validate() {
    if (!_pending_verify) {
        /* Serial-flashed images are already valid. Saying "validated!" here
         * would imply protection that was never armed. */
        ESP_LOGD(TAG, "validate() called but image is not pending - no-op");
        return ESP_OK;
    }

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to validate: %s", esp_err_to_name(err));
        return err;
    }

    _pending_verify = false;

    /* Stop the validation timer */
    if (_validation_timer) {
        xTimerStop(_validation_timer, 0);
    }

    ESP_LOGI(TAG, "Firmware validated! Rollback cancelled.");
    emitEvent(OTAEvent::VALIDATED);
    return ESP_OK;
}

esp_err_t OTAManager::rollback() {
    ESP_LOGW(TAG, "Rolling back to previous firmware...");

    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Rollback failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Device reboots, this line is never reached */
    return ESP_OK;
}

bool OTAManager::isPendingValidation() const { return _pending_verify; }

void OTAManager::validationTimerCb(TimerHandle_t timer) {
    (void)timer;
    ESP_LOGE(TAG, "Validation timeout expired! Auto-rolling back...");
    OTAManager& ota = instance();
    ota.emitEvent(OTAEvent::ROLLED_BACK);
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

/* =============================================================================
 * PARTITION INFO
 * ========================================================================== */

esp_err_t OTAManager::getPartitionInfo(OTAPartitionInfo& info) const {
    memset(&info, 0, sizeof(info));

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next    = esp_ota_get_next_update_partition(nullptr);

    if (running) {
        strncpy(info.running_label, running->label, sizeof(info.running_label) - 1);
        info.running_address = running->address;
        info.running_size    = running->size;

        /* Get version from running partition */
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(running, &desc) == ESP_OK) {
            strncpy(info.running_version, desc.version, OTA_MAX_VERSION_LEN - 1);
        }
    }

    if (next) {
        strncpy(info.next_label, next->label, sizeof(info.next_label) - 1);
        info.next_address = next->address;
        info.next_size    = next->size;
    }

    info.pending_verify = _pending_verify;

    /* Rollback is possible when the other slot holds a readable image.
     * (v1 computed this twice - the first expression was always overwritten
     * by this one, so only the meaningful check remains.) */
    info.rollback_possible = false;
    if (next) {
        esp_app_desc_t other_desc;
        if (esp_ota_get_partition_description(next, &other_desc) == ESP_OK) {
            info.rollback_possible = true;
        }
    }

    return ESP_OK;
}

/* =============================================================================
 * EVENTS
 * ========================================================================== */

void OTAManager::setEventCallback(OTAEventCb cb) {
    _event_cb = cb;
}

void OTAManager::emitEvent(OTAEvent event, const OTAEventInfo* info) {
    if (!_event_cb) return;

    if (info) {
        _event_cb(event, info);
    } else {
        OTAEventInfo empty = {};
        _event_cb(event, &empty);
    }
}