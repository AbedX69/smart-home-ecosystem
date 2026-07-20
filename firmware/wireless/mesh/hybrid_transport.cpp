/*
 * =============================================================================
 * FILE:        hybrid_transport.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-03-18
 * VERSION:     1.0.0
 * =============================================================================
 */

#include "hybrid_transport.h"
#include "esp_timer.h"
#include <cstdio>

static const char* TAG = "Hybrid";

/* ─── Singleton ──────────────────────────────────────────────────────────── */

HybridTransport& HybridTransport::instance() {
    static HybridTransport inst;
    return inst;
}

HybridTransport::HybridTransport()
    : _initialized(false)
    , _mutex(nullptr)
    , _ack_event(nullptr)
    , _send_cb(nullptr)
    , _recv_cb(nullptr)
{
    memset(&_pending, 0, sizeof(_pending));
    memset(&_stats, 0, sizeof(_stats));
}

HybridTransport::~HybridTransport() {
    end();
}

/* ─── Lifecycle ──────────────────────────────────────────────────────────── */

esp_err_t HybridTransport::begin(const HybridConfig& config) {
    if (_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    _config = config;

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  Hybrid Transport starting");
    ESP_LOGI(TAG, "  ESP-NOW timeout: %lu ms", _config.espnow_timeout_ms);
    ESP_LOGI(TAG, "  ESP-NOW retries: %d", _config.espnow_retries);
    ESP_LOGI(TAG, "  Mesh fallback: %s", _config.enable_mesh_fallback ? "YES" : "NO");
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    /* Create mutex */
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Create event group for ACK synchronization */
    _ack_event = xEventGroupCreate();
    if (!_ack_event) {
        ESP_LOGE(TAG, "Failed to create event group");
        vSemaphoreDelete(_mutex);
        return ESP_ERR_NO_MEM;
    }

    /* Hook into ESP-NOW callbacks */
    EspNowManager& espnow = EspNowManager::instance();
    
    espnow.setSendCallback([this](const uint8_t* mac, bool success) {
        this->onEspNowSend(mac, success);
    });
    
    espnow.setReceiveCallback([this](const uint8_t* mac, const uint8_t* data, int len) {
        this->onEspNowRecv(mac, data, len);
    });

    /* Hook into mesh callbacks */
    EspMeshManager& mesh = EspMeshManager::instance();
    
    mesh.setReceiveCallback([this](const uint8_t* mac, const uint8_t* data, 
                                    size_t len, bool from_root) {
        this->onMeshRecv(mac, data, len, from_root);
    });

    _initialized = true;
    
    ESP_LOGI(TAG, "Hybrid transport ready");
    return ESP_OK;
}

esp_err_t HybridTransport::end() {
    if (!_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Stopping hybrid transport...");

    /* Clean up pending send data if any */
    if (_pending.data) {
        free(_pending.data);
        _pending.data = nullptr;
    }

    if (_ack_event) {
        vEventGroupDelete(_ack_event);
        _ack_event = nullptr;
    }
    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }

    _initialized = false;
    return ESP_OK;
}

/* ─── Sending ────────────────────────────────────────────────────────────── */

HybridResult HybridTransport::send(const uint8_t dest_mac[6], 
                                    const uint8_t* data, size_t len) {
    if (!_initialized) {
        return HybridResult::FAIL_NO_CONN;
    }

    EspNowManager& espnow = EspNowManager::instance();
    EspMeshManager& mesh = EspMeshManager::instance();

    bool espnow_available = espnow.isReady();
    bool mesh_available = mesh.isConnected();

    if (!espnow_available && !mesh_available) {
        ESP_LOGW(TAG, "No transports available");
        return HybridResult::FAIL_NO_CONN;
    }

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             dest_mac[0], dest_mac[1], dest_mac[2], 
             dest_mac[3], dest_mac[4], dest_mac[5]);

    /* ── Try ESP-NOW first ─────────────────────────────────────────────── */
    if (espnow_available) {
        
        xSemaphoreTake(_mutex, portMAX_DELAY);
        
        /* Set up pending send tracking */
        memcpy(_pending.dest_mac, dest_mac, 6);
        _pending.waiting_ack = true;
        _pending.ack_received = false;
        _pending.ack_success = false;
        _pending.send_time = esp_timer_get_time();
        
        xSemaphoreGive(_mutex);

        /* Clear any previous ACK event */
        xEventGroupClearBits(_ack_event, BIT_ACK_RECEIVED);

        /* Try sending with retries */
        for (int attempt = 0; attempt < _config.espnow_retries; attempt++) {
            
            ESP_LOGD(TAG, "ESP-NOW send to %s (attempt %d)", mac_str, attempt + 1);
            
            esp_err_t err = espnow.send(dest_mac, data, len);
            _stats.espnow_sent++;
            
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "ESP-NOW send failed: %s", esp_err_to_name(err));
                _stats.espnow_failed++;
                continue;
            }

            /* Wait for ACK callback */
            EventBits_t bits = xEventGroupWaitBits(
                _ack_event, BIT_ACK_RECEIVED,
                pdTRUE,   /* Clear on exit */
                pdFALSE,  /* Wait for any bit */
                pdMS_TO_TICKS(_config.espnow_timeout_ms));

            if (bits & BIT_ACK_RECEIVED) {
                xSemaphoreTake(_mutex, portMAX_DELAY);
                bool success = _pending.ack_success;
                _pending.waiting_ack = false;
                xSemaphoreGive(_mutex);

                if (success) {
                    ESP_LOGD(TAG, "ESP-NOW ACK received from %s", mac_str);
                    _stats.espnow_acked++;
                    
                    if (_send_cb) {
                        _send_cb(dest_mac, HybridResult::OK_ESPNOW);
                    }
                    return HybridResult::OK_ESPNOW;
                } else {
                    ESP_LOGD(TAG, "ESP-NOW NAK from %s", mac_str);
                    _stats.espnow_failed++;
                }
            } else {
                ESP_LOGD(TAG, "ESP-NOW timeout waiting for ACK from %s", mac_str);
                _stats.espnow_failed++;
            }
        }

        /* ESP-NOW failed after all retries */
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _pending.waiting_ack = false;
        xSemaphoreGive(_mutex);
    }

    /* ── Fall back to mesh ─────────────────────────────────────────────── */
    if (_config.enable_mesh_fallback && mesh_available) {
        
        ESP_LOGI(TAG, "ESP-NOW failed, falling back to mesh for %s", mac_str);
        _stats.fallback_count++;
        
        esp_err_t err = mesh.sendTo(dest_mac, data, len);
        _stats.mesh_sent++;
        
        if (err == ESP_OK) {
            ESP_LOGD(TAG, "Mesh send succeeded to %s", mac_str);
            _stats.mesh_success++;
            
            if (_send_cb) {
                _send_cb(dest_mac, HybridResult::OK_MESH);
            }
            return HybridResult::OK_MESH;
        } else {
            ESP_LOGW(TAG, "Mesh send failed: %s", esp_err_to_name(err));
            _stats.mesh_failed++;
        }
    }

    /* Both transports failed */
    ESP_LOGW(TAG, "All transports failed for %s", mac_str);
    
    if (_send_cb) {
        _send_cb(dest_mac, HybridResult::FAIL_ALL);
    }
    return HybridResult::FAIL_ALL;
}

esp_err_t HybridTransport::sendVia(uint8_t transport, const uint8_t dest_mac[6],
                                    const uint8_t* data, size_t len) {
    if (!_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (transport == TRANSPORT_ESPNOW) {
        EspNowManager& espnow = EspNowManager::instance();
        if (!espnow.isReady()) {
            return ESP_ERR_INVALID_STATE;
        }
        _stats.espnow_sent++;
        return espnow.send(dest_mac, data, len);
    }
    
    if (transport == TRANSPORT_MESH) {
        EspMeshManager& mesh = EspMeshManager::instance();
        if (!mesh.isConnected()) {
            return ESP_ERR_INVALID_STATE;
        }
        _stats.mesh_sent++;
        return mesh.sendTo(dest_mac, data, len);
    }

    return ESP_ERR_INVALID_ARG;
}

esp_err_t HybridTransport::broadcast(const uint8_t* data, size_t len) {
    if (!_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_FAIL;

    /* Broadcast via ESP-NOW */
    if (_config.broadcast_transports & TRANSPORT_ESPNOW) {
        EspNowManager& espnow = EspNowManager::instance();
        if (espnow.isReady()) {
            esp_err_t err = espnow.broadcast(data, len);
            if (err == ESP_OK) {
                result = ESP_OK;
                _stats.espnow_sent++;
            }
        }
    }

    /* Broadcast via mesh */
    if (_config.broadcast_transports & TRANSPORT_MESH) {
        EspMeshManager& mesh = EspMeshManager::instance();
        if (mesh.isConnected()) {
            esp_err_t err = mesh.broadcast(data, len);
            if (err == ESP_OK) {
                result = ESP_OK;
                _stats.mesh_sent++;
            }
        }
    }

    return result;
}

esp_err_t HybridTransport::sendToRoot(const uint8_t* data, size_t len) {
    if (!_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    EspMeshManager& mesh = EspMeshManager::instance();
    if (!mesh.isConnected()) {
        return ESP_ERR_INVALID_STATE;
    }

    _stats.mesh_sent++;
    return mesh.sendToRoot(data, len);
}

/* ─── Callbacks ──────────────────────────────────────────────────────────── */

void HybridTransport::setSendCallback(HybridSendCb cb) { _send_cb = cb; }
void HybridTransport::setReceiveCallback(HybridReceiveCb cb) { _recv_cb = cb; }

/* ─── Status ─────────────────────────────────────────────────────────────── */

uint8_t HybridTransport::getAvailableTransports() const {
    uint8_t transports = 0;
    
    EspNowManager& espnow = EspNowManager::instance();
    if (espnow.isReady()) {
        transports |= TRANSPORT_ESPNOW;
    }
    
    EspMeshManager& mesh = EspMeshManager::instance();
    if (mesh.isConnected()) {
        transports |= TRANSPORT_MESH;
    }
    
    return transports;
}

HybridTransport::Stats HybridTransport::getStats() const {
    return _stats;
}

void HybridTransport::resetStats() {
    memset(&_stats, 0, sizeof(_stats));
}

/* ─── Internal Callbacks ─────────────────────────────────────────────────── */

void HybridTransport::onEspNowSend(const uint8_t* mac, bool success) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    if (_pending.waiting_ack && 
        memcmp(_pending.dest_mac, mac, 6) == 0) {
        
        _pending.ack_received = true;
        _pending.ack_success = success;
        
        /* Signal the waiting send() call */
        xEventGroupSetBits(_ack_event, BIT_ACK_RECEIVED);
    }
    
    xSemaphoreGive(_mutex);
}

void HybridTransport::onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (_recv_cb) {
        _recv_cb(mac, data, len, TRANSPORT_ESPNOW);
    }
}

void HybridTransport::onMeshRecv(const uint8_t* mac, const uint8_t* data, 
                                  size_t len, bool from_root) {
    (void)from_root;  /* Could be useful for your protocol */
    
    if (_recv_cb) {
        _recv_cb(mac, data, len, TRANSPORT_MESH);
    }
}
