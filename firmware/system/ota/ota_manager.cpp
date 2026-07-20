/*
 * =============================================================================
 * FILE:        ota_manager.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-14
 * VERSION:     1.0.0
 * =============================================================================
 */

#include "ota_manager.h"
#include "esp_http_client.h"
#include "esp_system.h"

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
    , _update_in_progress(false)
    , _event_cb(nullptr)
{
    memset(_version, 0, sizeof(_version));
    memset(_update_url, 0, sizeof(_update_url));
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
 *   1. Read current version from esp_app_desc (compiled into binary)
 *   2. Check OTA state - is this firmware pending validation?
 *   3. If pending, start a timer. If the timer expires before validate()
 *      is called, we rollback automatically.
 * ========================================================================== */

esp_err_t OTAManager::begin(uint32_t validation_timeout_s) {
    if (_initialized) return ESP_OK;

    _validation_timeout_s = validation_timeout_s;

    /* ── Get current version from compiled app descriptor ──────────── */
    const esp_app_desc_t* desc = esp_app_get_description();
    if (desc) {
        strncpy(_version, desc->version, OTA_MAX_VERSION_LEN - 1);
    } else {
        strcpy(_version, "0.0.0");
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
    ESP_LOGI(TAG, "  Version:    %s", _version);
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

const char* OTAManager::getVersion() const { return _version; }

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
 * FIRMWARE UPLOAD (PUSH)
 * =============================================================================
 * 
 * The upload handler receives firmware as a raw binary POST body.
 * It streams directly to the OTA partition in chunks - no need to
 * buffer the entire image in RAM.
 * 
 * Flow:
 *   1. Client POSTs binary to /api/ota/upload
 *   2. First chunk → esp_ota_begin() (erases target partition)
 *   3. Each chunk → esp_ota_write()
 *   4. Last chunk → esp_ota_end() + esp_ota_set_boot_partition()
 *   5. Respond with success, device reboots
 * ========================================================================== */

esp_err_t OTAManager::uploadHandler(httpd_req_t* req) {
    OTAManager& ota = instance();

    if (ota._update_in_progress) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Update already in progress");
        return ESP_FAIL;
    }

    /* Cannot start OTA if current firmware is unvalidated */
    if (ota._pending_verify) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Validate current firmware before updating");
        return ESP_FAIL;
    }

    ota._update_in_progress = true;

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found");
        ota._update_in_progress = false;
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        ota._update_in_progress = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA upload started → %s (%luKB)",
             update_partition->label, (unsigned long)(update_partition->size / 1024));

    OTAEventInfo info = {};
    info.total_size = req->content_len;
    ota.emitEvent(OTAEvent::UPDATE_STARTED, &info);

    /* ── Stream chunks to flash ────────────────────────────────────── */
    char* buf = (char*)malloc(OTA_RECV_BUF_SIZE);
    if (!buf) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        ota._update_in_progress = false;
        return ESP_FAIL;
    }

    uint32_t total_written = 0;
    int remaining = req->content_len;
    bool success = true;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, 
                                       (remaining > OTA_RECV_BUF_SIZE) ? OTA_RECV_BUF_SIZE : remaining);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "Receive error: %d", recv_len);
            success = false;
            break;
        }

        err = esp_ota_write(ota_handle, buf, recv_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            success = false;
            break;
        }

        total_written += recv_len;
        remaining -= recv_len;

        /* Report progress */
        info.bytes_written = total_written;
        info.progress_pct = (req->content_len > 0) ?
                            (total_written * 100.0f / req->content_len) : 0;
        ota.emitEvent(OTAEvent::PROGRESS, &info);

        if ((total_written % (64 * 1024)) < (uint32_t)recv_len) {
            ESP_LOGI(TAG, "OTA progress: %lu / %lu bytes (%.1f%%)",
                     (unsigned long)total_written,
                     (unsigned long)req->content_len,
                     info.progress_pct);
        }
    }

    free(buf);

    if (!success) {
        esp_ota_abort(ota_handle);
        snprintf(info.error_msg, sizeof(info.error_msg), "Upload failed at %lu bytes",
                 (unsigned long)total_written);
        ota.emitEvent(OTAEvent::UPDATE_FAILED, &info);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
        ota._update_in_progress = false;
        return ESP_FAIL;
    }

    /* ── Finalize ──────────────────────────────────────────────────── */
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        snprintf(info.error_msg, sizeof(info.error_msg), "Image validation failed: %s",
                 esp_err_to_name(err));
        ota.emitEvent(OTAEvent::UPDATE_FAILED, &info);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Image validation failed");
        ota._update_in_progress = false;
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set boot partition failed: %s", esp_err_to_name(err));
        ota._update_in_progress = false;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA complete! %lu bytes written. Rebooting in 2s...",
             (unsigned long)total_written);

    info.progress_pct = 100.0f;
    info.bytes_written = total_written;
    ota.emitEvent(OTAEvent::UPDATE_COMPLETE, &info);

    /* Send success response before rebooting */
    httpd_resp_set_type(req, "application/json");
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"bytes\":%lu,\"message\":\"Rebooting...\"}",
             (unsigned long)total_written);
    httpd_resp_sendstr(req, resp);

    /* Delay then reboot */
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;  // Never reached
}

esp_err_t OTAManager::registerUploadHandler(httpd_handle_t server) {
    httpd_uri_t uri = {};
    uri.uri = "/api/ota/upload";
    uri.method = HTTP_POST;
    uri.handler = uploadHandler;
    uri.user_ctx = nullptr;
    return httpd_register_uri_handler(server, &uri);
}

/* =============================================================================
 * FIRMWARE DOWNLOAD (PULL)
 * =============================================================================
 * 
 * Pull-based OTA:
 *   1. Download manifest.json from server
 *   2. Parse version from manifest
 *   3. Compare with current version
 *   4. If newer, download firmware.bin and flash it
 * 
 * Manifest format:
 *   {
 *     "version": "1.3.0",
 *     "file": "firmware.bin",
 *     "size": 819200
 *   }
 * ========================================================================== */

void OTAManager::setUpdateURL(const char* base_url) {
    strncpy(_update_url, base_url, OTA_MAX_URL_LEN - 1);
    /* Remove trailing slash */
    size_t len = strlen(_update_url);
    if (len > 0 && _update_url[len - 1] == '/') {
        _update_url[len - 1] = '\0';
    }
}

esp_err_t OTAManager::checkForUpdate(bool auto_update) {
    if (strlen(_update_url) == 0) {
        ESP_LOGE(TAG, "No update URL set");
        return ESP_ERR_INVALID_STATE;
    }

    /* ── Download manifest.json ────────────────────────────────────── */
    char manifest_url[OTA_MAX_URL_LEN + 32];
    snprintf(manifest_url, sizeof(manifest_url), "%s/manifest.json", _update_url);

    char response[512] = {};
    esp_http_client_config_t config = {};
    config.url = manifest_url;
    config.timeout_ms = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to update server");
        esp_http_client_cleanup(client);
        return err;
    }

    int content_len = esp_http_client_fetch_headers(client);
    if (content_len <= 0 || content_len >= (int)sizeof(response)) {
        ESP_LOGE(TAG, "Invalid manifest response");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int read_len = esp_http_client_read(client, response, sizeof(response) - 1);
    esp_http_client_cleanup(client);

    if (read_len <= 0) {
        ESP_LOGE(TAG, "Failed to read manifest");
        return ESP_FAIL;
    }
    response[read_len] = '\0';

    /* ── Parse version from manifest (minimal JSON parsing) ────────── */
    char server_version[OTA_MAX_VERSION_LEN] = {};
    char firmware_file[128] = {};

    /* Find "version" field */
    const char* ver_key = strstr(response, "\"version\"");
    if (ver_key) {
        const char* ver_start = strchr(ver_key + 9, '"');
        if (ver_start) {
            ver_start++;
            const char* ver_end = strchr(ver_start, '"');
            if (ver_end && (ver_end - ver_start) < OTA_MAX_VERSION_LEN) {
                memcpy(server_version, ver_start, ver_end - ver_start);
            }
        }
    }

    /* Find "file" field */
    const char* file_key = strstr(response, "\"file\"");
    if (file_key) {
        const char* file_start = strchr(file_key + 6, '"');
        if (file_start) {
            file_start++;
            const char* file_end = strchr(file_start, '"');
            if (file_end && (file_end - file_start) < (int)sizeof(firmware_file)) {
                memcpy(firmware_file, file_start, file_end - file_start);
            }
        }
    }

    if (strlen(server_version) == 0) {
        ESP_LOGE(TAG, "No version found in manifest");
        return ESP_FAIL;
    }

    /* ── Compare versions ──────────────────────────────────────────── */
    SemVer current_ver, server_ver;
    parseVersion(_version, current_ver);
    parseVersion(server_version, server_ver);

    OTAEventInfo info = {};
    strncpy(info.new_version, server_version, OTA_MAX_VERSION_LEN - 1);
    info.update_available = (server_ver > current_ver);

    ESP_LOGI(TAG, "Version check: current=%s server=%s → %s",
             _version, server_version,
             info.update_available ? "UPDATE AVAILABLE" : "up to date");

    emitEvent(OTAEvent::VERSION_CHECK, &info);

    /* ── Auto-update if requested ──────────────────────────────────── */
    if (info.update_available && auto_update) {
        char firmware_url[OTA_MAX_URL_LEN + 128];
        if (strlen(firmware_file) > 0) {
            snprintf(firmware_url, sizeof(firmware_url), "%s/%s", _update_url, firmware_file);
        } else {
            snprintf(firmware_url, sizeof(firmware_url), "%s/firmware.bin", _update_url);
        }
        return updateFromURL(firmware_url);
    }

    return ESP_OK;
}

esp_err_t OTAManager::updateFromURL(const char* url) {
    if (!url) return ESP_ERR_INVALID_ARG;
    if (_update_in_progress) return ESP_ERR_INVALID_STATE;
    if (_pending_verify) {
        ESP_LOGE(TAG, "Validate current firmware before updating");
        return ESP_ERR_INVALID_STATE;
    }

    _update_in_progress = true;

    ESP_LOGI(TAG, "Downloading firmware from: %s", url);

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        _update_in_progress = false;
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        _update_in_progress = false;
        return err;
    }

    OTAEventInfo info = {};
    emitEvent(OTAEvent::UPDATE_STARTED, &info);

    /* ── Download and write in chunks ──────────────────────────────── */
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 30000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        esp_ota_abort(ota_handle);
        _update_in_progress = false;
        return ESP_FAIL;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        esp_ota_abort(ota_handle);
        _update_in_progress = false;
        return err;
    }

    int content_len = esp_http_client_fetch_headers(client);
    info.total_size = (content_len > 0) ? content_len : 0;

    char* buf = (char*)malloc(OTA_RECV_BUF_SIZE);
    if (!buf) {
        esp_http_client_cleanup(client);
        esp_ota_abort(ota_handle);
        _update_in_progress = false;
        return ESP_ERR_NO_MEM;
    }

    uint32_t total_written = 0;
    bool success = true;

    while (true) {
        int read_len = esp_http_client_read(client, buf, OTA_RECV_BUF_SIZE);
        if (read_len < 0) {
            ESP_LOGE(TAG, "HTTP read error");
            success = false;
            break;
        }
        if (read_len == 0) {
            /* Check if the connection is truly done */
            if (esp_http_client_is_complete_data_received(client)) break;
            /* Timeout or error on incomplete data */
            ESP_LOGE(TAG, "Connection closed prematurely");
            success = false;
            break;
        }

        err = esp_ota_write(ota_handle, buf, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            success = false;
            break;
        }

        total_written += read_len;
        info.bytes_written = total_written;
        info.progress_pct = (info.total_size > 0) ?
                            (total_written * 100.0f / info.total_size) : 0;
        emitEvent(OTAEvent::PROGRESS, &info);
    }

    free(buf);
    esp_http_client_cleanup(client);

    if (!success) {
        esp_ota_abort(ota_handle);
        snprintf(info.error_msg, sizeof(info.error_msg), "Download failed at %lu bytes",
                 (unsigned long)total_written);
        emitEvent(OTAEvent::UPDATE_FAILED, &info);
        _update_in_progress = false;
        return ESP_FAIL;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        snprintf(info.error_msg, sizeof(info.error_msg), "Validation failed: %s",
                 esp_err_to_name(err));
        emitEvent(OTAEvent::UPDATE_FAILED, &info);
        _update_in_progress = false;
        return err;
    }

    esp_ota_set_boot_partition(update_partition);

    info.progress_pct = 100.0f;
    info.bytes_written = total_written;
    emitEvent(OTAEvent::UPDATE_COMPLETE, &info);

    ESP_LOGI(TAG, "Download OTA complete! %lu bytes. Rebooting...",
             (unsigned long)total_written);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

/* =============================================================================
 * ROLLBACK & VALIDATION
 * ========================================================================== */

esp_err_t OTAManager::validate() {
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
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);

    if (running) {
        strncpy(info.running_label, running->label, sizeof(info.running_label) - 1);
        info.running_address = running->address;
        info.running_size = running->size;

        /* Get version from running partition */
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(running, &desc) == ESP_OK) {
            strncpy(info.running_version, desc.version, OTA_MAX_VERSION_LEN - 1);
        }
    }

    if (next) {
        strncpy(info.next_label, next->label, sizeof(info.next_label) - 1);
        info.next_address = next->address;
        info.next_size = next->size;
    }

    info.pending_verify = _pending_verify;

    /* Check if rollback is possible (previous partition has valid image) */
    const esp_partition_t* last_invalid = esp_ota_get_last_invalid_partition();
    info.rollback_possible = (last_invalid == nullptr && !_pending_verify) ? false :
                             (_pending_verify ? true : false);
    /* More accurate: check if there's a valid image in the other slot */
    if (next) {
        esp_app_desc_t other_desc;
        if (esp_ota_get_partition_description(next, &other_desc) == ESP_OK) {
            info.rollback_possible = true;
        }
    }

    return ESP_OK;
}

/* =============================================================================
 * WEB UI
 * =============================================================================
 * 
 * Embedded single-page HTML with:
 *   - Dark theme matching the captive portal style
 *   - Drag & drop firmware upload zone
 *   - Real-time progress bar
 *   - Current firmware info display
 *   - Rollback button
 *   - Status polling
 * ========================================================================== */

static const char OTA_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OTA Update</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#1a1a2e;color:#e0e0e0;
     display:flex;justify-content:center;padding:20px;min-height:100vh}
.c{max-width:500px;width:100%}
h1{color:#00d4ff;font-size:1.5em;margin-bottom:8px}
.sub{color:#888;font-size:0.85em;margin-bottom:20px}
.card{background:#16213e;border-radius:12px;padding:20px;margin-bottom:16px}
.card h2{font-size:1em;color:#00d4ff;margin-bottom:12px}
.info{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.info div{background:#0f3460;border-radius:8px;padding:10px}
.info .label{font-size:0.75em;color:#888}
.info .val{font-size:0.95em;font-weight:600;margin-top:2px}
.drop{border:2px dashed #0f3460;border-radius:12px;padding:40px 20px;
      text-align:center;cursor:pointer;transition:all 0.3s}
.drop:hover,.drop.active{border-color:#00d4ff;background:rgba(0,212,255,0.05)}
.drop input{display:none}
.drop .icon{font-size:2em;margin-bottom:8px}
.drop p{color:#888;font-size:0.9em}
.progress{display:none;margin-top:16px}
.bar-bg{background:#0f3460;border-radius:8px;height:24px;overflow:hidden}
.bar{background:linear-gradient(90deg,#00d4ff,#0ea5e9);height:100%;
     border-radius:8px;transition:width 0.3s;width:0%}
.pct{text-align:center;margin-top:6px;font-size:0.85em;color:#00d4ff}
.status{margin-top:12px;padding:12px;border-radius:8px;font-size:0.85em;display:none}
.status.ok{background:rgba(0,200,100,0.15);color:#00c864;display:block}
.status.err{background:rgba(255,60,60,0.15);color:#ff3c3c;display:block}
.status.warn{background:rgba(255,180,0,0.15);color:#ffb400;display:block}
button{background:#0f3460;color:#e0e0e0;border:1px solid #1a4080;
       border-radius:8px;padding:10px 20px;cursor:pointer;font-size:0.9em;
       margin-top:8px;transition:all 0.2s}
button:hover{background:#1a4080;border-color:#00d4ff}
button.danger{border-color:#ff3c3c}
button.danger:hover{background:#3c1010;border-color:#ff6060}
</style></head>
<body><div class="c">
<h1>OTA Firmware Update</h1>
<div class="sub" id="ver">Loading...</div>

<div class="card">
<h2>Device Info</h2>
<div class="info" id="info">
<div><div class="label">Version</div><div class="val" id="v">-</div></div>
<div><div class="label">Partition</div><div class="val" id="p">-</div></div>
<div><div class="label">Status</div><div class="val" id="s">-</div></div>
<div><div class="label">Uptime</div><div class="val" id="u">-</div></div>
</div>
</div>

<div class="card">
<h2>Upload Firmware</h2>
<div class="drop" id="drop" onclick="document.getElementById('file').click()">
<div class="icon">&#128228;</div>
<p>Drag & drop .bin file here<br>or click to browse</p>
<input type="file" id="file" accept=".bin">
</div>
<div class="progress" id="prog">
<div class="bar-bg"><div class="bar" id="bar"></div></div>
<div class="pct" id="pct">0%</div>
</div>
<div class="status" id="msg"></div>
</div>

<div class="card" id="rb-card" style="display:none">
<h2>Rollback</h2>
<p style="font-size:0.85em;color:#888;margin-bottom:8px">
Revert to the previous firmware version.</p>
<button class="danger" onclick="doRollback()">Rollback Now</button>
</div>

</div>
<script>
const $ = id => document.getElementById(id);
const drop = $('drop'), file = $('file');

drop.ondragover = e => { e.preventDefault(); drop.classList.add('active'); };
drop.ondragleave = () => drop.classList.remove('active');
drop.ondrop = e => { e.preventDefault(); drop.classList.remove('active');
  if(e.dataTransfer.files.length) upload(e.dataTransfer.files[0]); };
file.onchange = () => { if(file.files.length) upload(file.files[0]); };

function upload(f) {
  if(!f.name.endsWith('.bin')){ msg('err','Please select a .bin file'); return; }
  $('prog').style.display='block';
  msg('','');
  const xhr = new XMLHttpRequest();
  xhr.open('POST','/api/ota/upload');
  xhr.setRequestHeader('Content-Type','application/octet-stream');
  xhr.upload.onprogress = e => {
    if(e.lengthComputable){
      const p = Math.round(e.loaded/e.total*100);
      $('bar').style.width=p+'%'; $('pct').textContent=p+'%';
    }
  };
  xhr.onload = () => {
    if(xhr.status===200){ msg('ok','Update complete! Rebooting...'); }
    else { msg('err','Upload failed: '+xhr.responseText); }
  };
  xhr.onerror = () => msg('err','Connection lost');
  xhr.send(f);
}

function msg(cls, text) {
  const m=$('msg'); m.className='status'+(cls?' '+cls:''); m.textContent=text;
  m.style.display=text?'block':'none';
}

function doRollback() {
  if(!confirm('Rollback to previous firmware?')) return;
  fetch('/api/ota/rollback',{method:'POST'}).then(()=>msg('warn','Rolling back...'));
}

function refresh() {
  fetch('/api/ota/status').then(r=>r.json()).then(d=>{
    $('v').textContent=d.version||'-';
    $('p').textContent=d.partition||'-';
    $('s').textContent=d.pending_verify?'PENDING VERIFY':'Validated';
    $('u').textContent=d.uptime||'-';
    $('ver').textContent='Firmware v'+d.version;
    $('rb-card').style.display=d.rollback_possible?'block':'none';
    $('s').style.color=d.pending_verify?'#ffb400':'#00c864';
  }).catch(()=>{});
}
refresh(); setInterval(refresh, 5000);
</script></body></html>
)rawliteral";

esp_err_t OTAManager::webUIHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, OTA_HTML, strlen(OTA_HTML));
}

esp_err_t OTAManager::registerWebUI(httpd_handle_t server) {
    httpd_uri_t uri = {};
    uri.uri = "/ota";
    uri.method = HTTP_GET;
    uri.handler = webUIHandler;
    esp_err_t err = httpd_register_uri_handler(server, &uri);
    if (err != ESP_OK) return err;

    /* Also register rollback endpoint */
    httpd_uri_t rb_uri = {};
    rb_uri.uri = "/api/ota/rollback";
    rb_uri.method = HTTP_POST;
    rb_uri.handler = rollbackHandler;
    return httpd_register_uri_handler(server, &rb_uri);
}

/* =============================================================================
 * STATUS & ROLLBACK HANDLERS
 * ========================================================================== */

esp_err_t OTAManager::statusHandler(httpd_req_t* req) {
    OTAManager& ota = instance();
    OTAPartitionInfo pinfo = {};
    ota.getPartitionInfo(pinfo);

    /* Calculate uptime */
    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t hours = uptime_s / 3600;
    uint32_t mins = (uptime_s % 3600) / 60;
    uint32_t secs = uptime_s % 60;

    char json[384];
    snprintf(json, sizeof(json),
        "{\"version\":\"%s\","
        "\"partition\":\"%s\","
        "\"pending_verify\":%s,"
        "\"rollback_possible\":%s,"
        "\"next_slot\":\"%s\","
        "\"uptime\":\"%luh %lum %lus\"}",
        ota._version,
        pinfo.running_label,
        pinfo.pending_verify ? "true" : "false",
        pinfo.rollback_possible ? "true" : "false",
        pinfo.next_label,
        (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

esp_err_t OTAManager::registerStatusHandler(httpd_handle_t server) {
    httpd_uri_t uri = {};
    uri.uri = "/api/ota/status";
    uri.method = HTTP_GET;
    uri.handler = statusHandler;
    return httpd_register_uri_handler(server, &uri);
}

esp_err_t OTAManager::rollbackHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"rolling back\"}");

    vTaskDelay(pdMS_TO_TICKS(500));
    instance().rollback();

    return ESP_OK;  // Never reached
}

/* =============================================================================
 * CALLBACKS
 * ========================================================================== */

void OTAManager::setEventCallback(OTAEventCb cb) { _event_cb = cb; }

void OTAManager::emitEvent(OTAEvent event, const OTAEventInfo* info) {
    if (_event_cb) _event_cb(event, info);
}
