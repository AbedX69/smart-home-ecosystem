/*
 * =============================================================================
 * FILE:        wifi_services.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-13
 * VERSION:     1.0.0
 * =============================================================================
 */

#include "wifi_services.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_system.h"
#include "mdns.h"
#include <cstring>

static const char* TAG = "WiFiServices";

OTAProgressCb WiFiServices::_ota_progress_cb = nullptr;

/* =============================================================================
 * mDNS
 * ========================================================================== */

esp_err_t WiFiServices::startMDNS(const char* hostname, const char* instance) {
    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = mdns_hostname_set(hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS hostname set failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (instance) {
        mdns_instance_name_set(instance);
    }

    ESP_LOGI(TAG, "mDNS started: %s.local", hostname);
    return ESP_OK;
}

esp_err_t WiFiServices::stopMDNS() {
    mdns_free();
    ESP_LOGI(TAG, "mDNS stopped");
    return ESP_OK;
}

esp_err_t WiFiServices::addMDNSService(const char* service, const char* proto,
                                        uint16_t port) {
    esp_err_t ret = mdns_service_add(nullptr, service, proto, port, nullptr, 0);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "mDNS service added: %s.%s port %d", service, proto, port);
    }
    return ret;
}

/* =============================================================================
 * OTA - HTTP UPLOAD HANDLER
 * ========================================================================== */

esp_err_t WiFiServices::registerOTAHandler(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_ARG;

    httpd_uri_t ota_uri = {};
    ota_uri.uri = "/api/ota";
    ota_uri.method = HTTP_POST;
    ota_uri.handler = otaUploadHandler;

    esp_err_t ret = httpd_register_uri_handler(server, &ota_uri);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA endpoint registered: POST /api/ota");
    }
    return ret;
}

void WiFiServices::setOTAProgressCallback(OTAProgressCb cb) {
    _ota_progress_cb = cb;
}

esp_err_t WiFiServices::otaUploadHandler(httpd_req_t* req) {
    ESP_LOGI(TAG, "OTA upload started, content length: %d", req->content_len);

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found! Check partition table.");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                           "No OTA partition available");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing to partition: %s (offset 0x%lx)",
             update_partition->label, (unsigned long)update_partition->address);

    esp_ota_handle_t ota_handle;
    esp_err_t ret = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    /* Receive and write firmware in chunks */
    char buf[1024];
    size_t total_written = 0;
    size_t total_size = req->content_len;
    int received;

    while (total_written < total_size) {
        received = httpd_req_recv(req, buf, sizeof(buf));

        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "Receive error at %d bytes", (int)total_written);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }

        ret = esp_ota_write(ota_handle, buf, received);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(ret));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            return ESP_FAIL;
        }

        total_written += received;

        if (_ota_progress_cb) {
            _ota_progress_cb(total_written, total_size);
        }

        /* Log every ~10% */
        if (total_size > 0 && (total_written % (total_size / 10 + 1)) < (size_t)received) {
            ESP_LOGI(TAG, "OTA: %d / %d (%d%%)",
                     (int)total_written, (int)total_size,
                     (int)(total_written * 100 / total_size));
        }
    }

    ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA finalize failed");
        return ESP_FAIL;
    }

    ret = esp_ota_set_boot_partition(update_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set boot partition failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA complete! %d bytes written. Rebooting in 2s...",
             (int)total_written);

    /* Send success response before rebooting */
    httpd_resp_set_type(req, "application/json");
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"bytes\":%d,\"message\":\"rebooting\"}", (int)total_written);
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    /* Delay then reboot */
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;  // Never reached
}

/* =============================================================================
 * OTA FROM URL (PULL-BASED)
 * ========================================================================== */

esp_err_t WiFiServices::otaFromURL(const char* url) {
    if (!url) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Starting OTA from URL: %s", url);

    esp_http_client_config_t http_config = {};
    http_config.url = url;
    http_config.timeout_ms = 30000;

    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &http_config;

    esp_https_ota_handle_t ota_handle;
    esp_err_t ret = esp_https_ota_begin(&ota_config, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    while (true) {
        ret = esp_https_ota_perform(ota_handle);
        if (ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        int image_read = esp_https_ota_get_image_len_read(ota_handle);
        int image_size = esp_https_ota_get_image_size(ota_handle);

        if (_ota_progress_cb && image_size > 0) {
            _ota_progress_cb(image_read, image_size);
        }
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA from URL failed: %s", esp_err_to_name(ret));
        esp_https_ota_abort(ota_handle);
        return ret;
    }

    ret = esp_https_ota_finish(ota_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA from URL complete! Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    return ret;
}

/* =============================================================================
 * OTA ROLLBACK / VALIDATE
 * ========================================================================== */

esp_err_t WiFiServices::otaRollback() {
    esp_err_t ret = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Rollback failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t WiFiServices::otaValidate() {
    esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Firmware validated, rollback cancelled");
    }
    return ret;
}
