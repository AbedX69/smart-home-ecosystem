/*
 * =============================================================================
 * FILE:        wifi_http_client.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-13
 * VERSION:     1.0.0
 * =============================================================================
 */

#include "wifi_http_client.h"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "WiFiHttpClient";

/* =============================================================================
 * RESPONSE COLLECTOR
 * =============================================================================
 * 
 * ESP-IDF's HTTP client delivers response data in chunks via an event handler.
 * We accumulate chunks into the user's buffer.
 * 
 * The user_data pointer in the event config points to a simple struct that 
 * tracks the buffer and current write position.
 * ========================================================================== */

struct ResponseCtx {
    char*   buf;
    size_t  buf_len;
    size_t  pos;
};

esp_err_t WiFiHttpClient::httpEventHandler(esp_http_client_event_t* evt) {
    ResponseCtx* ctx = static_cast<ResponseCtx*>(evt->user_data);
    if (!ctx) return ESP_OK;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            /* Append received data to our buffer */
            if (ctx->pos + evt->data_len < ctx->buf_len) {
                memcpy(ctx->buf + ctx->pos, evt->data, evt->data_len);
                ctx->pos += evt->data_len;
                ctx->buf[ctx->pos] = '\0';  // Keep null-terminated
            } else {
                /* Buffer full - copy what we can */
                size_t remaining = ctx->buf_len - ctx->pos - 1;
                if (remaining > 0) {
                    memcpy(ctx->buf + ctx->pos, evt->data, remaining);
                    ctx->pos += remaining;
                    ctx->buf[ctx->pos] = '\0';
                }
                ESP_LOGW(TAG, "Response buffer full, truncating");
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "Response complete: %d bytes", (int)ctx->pos);
            break;

        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP error event");
            break;

        default:
            break;
    }
    return ESP_OK;
}

/* =============================================================================
 * SHARED REQUEST IMPLEMENTATION
 * ========================================================================== */

int WiFiHttpClient::performRequest(esp_http_client_method_t method,
                                    const char* url, const char* body,
                                    const char* content_type,
                                    char* response_buf, size_t buf_len,
                                    int timeout_ms) {
    if (!url || !response_buf || buf_len == 0) return -1;

    /* Clear response buffer */
    memset(response_buf, 0, buf_len);

    ResponseCtx ctx = { response_buf, buf_len, 0 };

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = method;
    config.event_handler = httpEventHandler;
    config.user_data = &ctx;
    config.timeout_ms = timeout_ms;
    config.disable_auto_redirect = false;
    config.max_redirection_count = 3;

    /* Note: For HTTPS, you'd need to set config.cert_pem to the server's
     * root CA certificate, or set config.skip_cert_common_name_check = true
     * (not recommended for production). */

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return -1;
    }

    /* Set body for POST/PUT */
    if (body && (method == HTTP_METHOD_POST || method == HTTP_METHOD_PUT)) {
        esp_http_client_set_header(client, "Content-Type",
                                   content_type ? content_type : "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = -1;

    if (err == ESP_OK) {
        status_code = esp_http_client_get_status_code(client);
        int content_len = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "%s %s → %d (%d bytes)",
                 method == HTTP_METHOD_GET ? "GET" :
                 method == HTTP_METHOD_POST ? "POST" :
                 method == HTTP_METHOD_PUT ? "PUT" : "DELETE",
                 url, status_code, content_len);
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return status_code;
}

/* =============================================================================
 * PUBLIC API
 * ========================================================================== */

int WiFiHttpClient::get(const char* url, char* response_buf, size_t buf_len,
                         int timeout_ms) {
    return performRequest(HTTP_METHOD_GET, url, nullptr, nullptr,
                          response_buf, buf_len, timeout_ms);
}

int WiFiHttpClient::post(const char* url, const char* body,
                          char* response_buf, size_t buf_len,
                          const char* content_type, int timeout_ms) {
    return performRequest(HTTP_METHOD_POST, url, body, content_type,
                          response_buf, buf_len, timeout_ms);
}

int WiFiHttpClient::put(const char* url, const char* body,
                         char* response_buf, size_t buf_len,
                         const char* content_type, int timeout_ms) {
    return performRequest(HTTP_METHOD_PUT, url, body, content_type,
                          response_buf, buf_len, timeout_ms);
}

int WiFiHttpClient::del(const char* url, char* response_buf, size_t buf_len,
                         int timeout_ms) {
    return performRequest(HTTP_METHOD_DELETE, url, nullptr, nullptr,
                          response_buf, buf_len, timeout_ms);
}
