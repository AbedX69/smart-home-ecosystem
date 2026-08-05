/*
 * =============================================================================
 * FILE:        ota_http.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-07-28
 * VERSION:     1.0.0
 * =============================================================================
 *
 * HTTP transport for OTAManager. Was OTAManager v1.0.0's HTTP half; now an
 * ordinary consumer of the sink. Owns no flash state - every byte goes
 * through beginWrite / writeChunk / finishWrite.
 *
 * =============================================================================
 */

#include "ota_http.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "OTAHttp";

/* Base URL for pull-based updates. Was OTAManager::_update_url. */
static char s_update_url[OTA_MAX_URL_LEN] = {};

/* Forward declarations - the blob and the handlers that use it sit below. */
static esp_err_t webUIHandler(httpd_req_t* req);
static esp_err_t statusHandler(httpd_req_t* req);
static esp_err_t rollbackHandler(httpd_req_t* req);

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
 *   2. ota.beginWrite(content_len)   (erases target partition)
 *   3. Each chunk → ota.writeChunk()
 *   4. ota.finishWrite()             (verifies + flips boot partition)
 *   5. Respond with success, then reboot
 *
 * v1 called esp_ota_* directly here. It no longer does: the sink owns all
 * of that, so an HTTP failure and an ESP-NOW failure now unwind identically.
 * ========================================================================== */

static esp_err_t uploadHandler(httpd_req_t* req) {
    OTAManager& ota = OTAManager::instance();

    if (ota.isWriteInProgress()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Update already in progress");
        return ESP_FAIL;
    }

    /* Cannot start OTA if current firmware is unvalidated */
    if (ota.isPendingValidation()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Validate current firmware before updating");
        return ESP_FAIL;
    }

    esp_err_t err = ota.beginWrite((size_t)req->content_len);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA upload started, %lu bytes incoming",
             (unsigned long)req->content_len);

    /* ── Stream chunks to flash ────────────────────────────────────── */
    char* buf = (char*)malloc(OTA_RECV_BUF_SIZE);
    if (!buf) {
        ota.abortWrite();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int  remaining = req->content_len;
    bool success   = true;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf,
                                       (remaining > OTA_RECV_BUF_SIZE) ? OTA_RECV_BUF_SIZE : remaining);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "Receive error: %d", recv_len);
            success = false;
            break;
        }

        /* writeChunk() emits PROGRESS and aborts the write itself on error. */
        if (ota.writeChunk(buf, recv_len) != ESP_OK) {
            success = false;
            break;
        }

        remaining -= recv_len;

        uint32_t written = ota.bytesWritten();
        if ((written % (64 * 1024)) < (uint32_t)recv_len) {
            ESP_LOGI(TAG, "OTA progress: %lu / %lu bytes",
                     (unsigned long)written,
                     (unsigned long)req->content_len);
        }
    }

    free(buf);

    if (!success) {
        ota.abortWrite();   /* no-op if writeChunk already aborted */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
        return ESP_FAIL;
    }

    /* ── Finalize ──────────────────────────────────────────────────── */
    uint32_t total_written = ota.bytesWritten();

    err = ota.finishWrite();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Image validation failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA complete! %lu bytes written. Rebooting in 2s...",
             (unsigned long)total_written);

    /* Send success response before rebooting */
    httpd_resp_set_type(req, "application/json");
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"bytes\":%lu,\"message\":\"Rebooting...\"}",
             (unsigned long)total_written);
    httpd_resp_sendstr(req, resp);

    /* Delay then reboot. The sink deliberately does not do this for us. */
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;  // Never reached
}

esp_err_t ota_http_register_upload(httpd_handle_t server) {
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

void ota_http_set_update_url(const char* base_url) {
    if (!base_url) return;
    strncpy(s_update_url, base_url, OTA_MAX_URL_LEN - 1);
    s_update_url[OTA_MAX_URL_LEN - 1] = '\0';
    /* Remove trailing slash */
    size_t len = strlen(s_update_url);
    if (len > 0 && s_update_url[len - 1] == '/') {
        s_update_url[len - 1] = '\0';
    }
}

esp_err_t ota_http_check_for_update(bool auto_update) {
    OTAManager& ota = OTAManager::instance();

    if (strlen(s_update_url) == 0) {
        ESP_LOGE(TAG, "No update URL set");
        return ESP_ERR_INVALID_STATE;
    }

    /* ── Download manifest.json ────────────────────────────────────── */
    char manifest_url[OTA_MAX_URL_LEN + 32];
    snprintf(manifest_url, sizeof(manifest_url), "%s/manifest.json", s_update_url);

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
    OTAManager::parseVersion(ota.getVersion(), current_ver);
    OTAManager::parseVersion(server_version, server_ver);

    OTAEventInfo info = {};
    strncpy(info.new_version, server_version, OTA_MAX_VERSION_LEN - 1);
    info.update_available = (server_ver > current_ver);

    ESP_LOGI(TAG, "Version check: current=%s server=%s -> %s",
             ota.getVersion(), server_version,
             info.update_available ? "UPDATE AVAILABLE" : "up to date");

    ota.emitEvent(OTAEvent::VERSION_CHECK, &info);

    /* ── Auto-update if requested ──────────────────────────────────── */
    if (info.update_available && auto_update) {
        char firmware_url[OTA_MAX_URL_LEN + 128];
        if (strlen(firmware_file) > 0) {
            snprintf(firmware_url, sizeof(firmware_url), "%s/%s", s_update_url, firmware_file);
        } else {
            snprintf(firmware_url, sizeof(firmware_url), "%s/firmware.bin", s_update_url);
        }
        return ota_http_update_from_url(firmware_url);
    }

    return ESP_OK;
}

esp_err_t ota_http_update_from_url(const char* url) {
    if (!url) return ESP_ERR_INVALID_ARG;

    OTAManager& ota = OTAManager::instance();

    if (ota.isWriteInProgress()) return ESP_ERR_INVALID_STATE;
    if (ota.isPendingValidation()) {
        ESP_LOGE(TAG, "Validate current firmware before updating");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Downloading firmware from: %s", url);

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 30000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    /* Headers first, so the sink can be told the real size and erase only
     * what it needs instead of the whole slot. */
    int content_len = esp_http_client_fetch_headers(client);

    err = ota.beginWrite(content_len > 0 ? (size_t)content_len : 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    char* buf = (char*)malloc(OTA_RECV_BUF_SIZE);
    if (!buf) {
        ota.abortWrite();
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

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

        if (ota.writeChunk(buf, read_len) != ESP_OK) {
            success = false;
            break;
        }
    }

    free(buf);
    esp_http_client_cleanup(client);

    if (!success) {
        ota.abortWrite();
        return ESP_FAIL;
    }

    uint32_t total_written = ota.bytesWritten();

    err = ota.finishWrite();
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Download OTA complete! %lu bytes. Rebooting...",
             (unsigned long)total_written);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

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

static esp_err_t webUIHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, OTA_HTML, strlen(OTA_HTML));
}

esp_err_t ota_http_register_web_ui(httpd_handle_t server) {
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

static esp_err_t statusHandler(httpd_req_t* req) {
    OTAManager& ota = OTAManager::instance();
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
        ota.getVersion(),
        pinfo.running_label,
        pinfo.pending_verify ? "true" : "false",
        pinfo.rollback_possible ? "true" : "false",
        pinfo.next_label,
        (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

esp_err_t ota_http_register_status(httpd_handle_t server) {
    httpd_uri_t uri = {};
    uri.uri = "/api/ota/status";
    uri.method = HTTP_GET;
    uri.handler = statusHandler;
    return httpd_register_uri_handler(server, &uri);
}

static esp_err_t rollbackHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"rolling back\"}");

    vTaskDelay(pdMS_TO_TICKS(500));
    OTAManager::instance().rollback();

    return ESP_OK;  // Never reached
}

/* =============================================================================
 * REGISTER EVERYTHING
 * ========================================================================== */

esp_err_t ota_http_register_all(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_ARG;

    esp_err_t err = ota_http_register_upload(server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register upload failed: %s", esp_err_to_name(err));
        return err;
    }

    err = ota_http_register_web_ui(server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register web UI failed: %s", esp_err_to_name(err));
        return err;
    }

    err = ota_http_register_status(server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register status failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OTA HTTP endpoints registered: /ota, /api/ota/{upload,status,rollback}");
    return ESP_OK;
}
