/*
 * =============================================================================
 * FILE:        ota_http.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-07-28
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    ESP32 / ESP32-S3 / ESP32-C6 (ESP-IDF v5.x)
 * =============================================================================
 *
 * HTTP transport for OTAManager.
 *
 * Everything in here was OTAManager v1.0.0's HTTP half. It moved out because
 * the public API took httpd_handle_t, which forced esp_http_server into every
 * build that touched OTA - including leaf nodes on ceilings that will never
 * serve a web page.
 *
 * This is now an ordinary consumer of the sink in ota_manager.h. It owns no
 * flash state of its own: every byte it receives goes through
 * beginWrite/writeChunk/finishWrite like any other transport.
 *
 * HUB ONLY. A strip node requires `ota` and not this.
 *
 * =============================================================================
 * WHAT IT PROVIDES
 * =============================================================================
 *
 *   POST /api/ota/upload     raw .bin body, streamed to flash
 *   GET  /ota                the drag & drop web UI
 *   GET  /api/ota/status     JSON: version, partition, pending, uptime
 *   POST /api/ota/rollback   revert to the other slot
 *
 * Plus pull-based updating from a server that hosts a manifest.json:
 *
 *   { "version": "1.3.0", "file": "firmware.bin", "size": 819200 }
 *
 * =============================================================================
 * USAGE
 * =============================================================================
 *
 *     OTAManager::instance().begin();
 *     ota_http_register_all(server);       // all four endpoints
 *     // → browse to http://device.local/ota
 *
 * Or individually, if you only want some of them:
 *
 *     ota_http_register_upload(server);
 *     ota_http_register_status(server);
 *
 * Pull mode:
 *
 *     ota_http_set_update_url("http://192.168.1.100:8080/firmware");
 *     ota_http_check_for_update(true);     // true = download if newer
 *
 * =============================================================================
 * NOTE ON REBOOTING
 * =============================================================================
 *
 * The sink's finishWrite() does not reboot - that is deliberate, so an
 * ESP-NOW transfer can ACK its final chunk first. HTTP does want to reboot,
 * so the handlers here answer the request, wait, then call esp_restart()
 * themselves. That policy lives here, not in the sink.
 *
 * =============================================================================
 */

#ifndef OTA_HTTP_H
#define OTA_HTTP_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "ota_manager.h"

/* ─── Constants ──────────────────────────────────────────────────────────── */
#define OTA_MAX_URL_LEN         256
#define OTA_RECV_BUF_SIZE       4096

/* ─── Endpoint registration ──────────────────────────────────────────────── */

/** POST /api/ota/upload - raw binary body streamed straight to flash. */
esp_err_t ota_http_register_upload(httpd_handle_t server);

/** GET /ota (web UI) and POST /api/ota/rollback. */
esp_err_t ota_http_register_web_ui(httpd_handle_t server);

/** GET /api/ota/status - JSON status for the web UI to poll. */
esp_err_t ota_http_register_status(httpd_handle_t server);

/** All of the above. Returns the first error encountered, or ESP_OK. */
esp_err_t ota_http_register_all(httpd_handle_t server);

/* ─── Pull-based updates ─────────────────────────────────────────────────── */

/**
 * @brief Set the base URL holding manifest.json and the firmware binary.
 * @param base_url  e.g. "http://192.168.1.100:8080/firmware" (trailing / ok)
 */
void ota_http_set_update_url(const char* base_url);

/**
 * @brief Fetch manifest.json and compare its version against the running one.
 *
 * Result is reported via OTAEvent::VERSION_CHECK on the OTAManager callback.
 *
 * @param auto_update  If true and the server is newer, download immediately.
 * @return ESP_OK if the check completed - not that an update was found.
 */
esp_err_t ota_http_check_for_update(bool auto_update = false);

/**
 * @brief Download a .bin from an explicit URL and flash it.
 *
 * Reboots on success and does not return.
 */
esp_err_t ota_http_update_from_url(const char* url);

#endif // OTA_HTTP_H
