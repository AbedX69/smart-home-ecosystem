/*
 * =============================================================================
 * FILE:        hybrid_transport.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-03-18
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    All ESP32 variants (ESP-IDF v5.x)
 * =============================================================================
 * 
 * Hybrid Transport — ESP-NOW first, mesh fallback.
 * 
 * Your design:
 *   1. Try ESP-NOW direct (fast, ~1-5ms)
 *   2. If no ACK, retry via mesh (routed, ~10-50ms)
 * 
 * This gives you the best of both:
 *   - Speed when devices are in range
 *   - Reliability when they're not
 * 
 * =============================================================================
 * HOW IT WORKS
 * =============================================================================
 * 
 *     ┌─────────────────────────────────────────────────────────────────┐
 *     │                                                                 │
 *     │   You call: hybrid.send(dest_mac, data, len);                   │
 *     │                                                                 │
 *     │   ┌───────────────────┐                                         │
 *     │   │ Try ESP-NOW       │──► ACK received?                        │
 *     │   │ (direct, fast)    │       │                                 │
 *     │   └───────────────────┘       ├── YES: Done! ✓                  │
 *     │                               │                                 │
 *     │                               └── NO: Fall back to mesh         │
 *     │                                       │                         │
 *     │                               ┌───────▼───────┐                 │
 *     │                               │ Send via mesh │                 │
 *     │                               │ (routed)      │                 │
 *     │                               └───────────────┘                 │
 *     │                                                                 │
 *     └─────────────────────────────────────────────────────────────────┘
 * 
 * 
 * WHY THIS DESIGN?
 * ~~~~~~~~~~~~~~~~
 * 
 *     ESP-NOW:
 *       ✓ Very fast (~1-5ms latency)
 *       ✓ Low power
 *       ✓ Simple
 *       ✗ No routing — device must be in direct range
 * 
 *     Mesh:
 *       ✓ Automatic routing through intermediate nodes
 *       ✓ Self-healing if a node dies
 *       ✗ Slower (~10-50ms latency)
 *       ✗ Higher power (WiFi)
 * 
 *     Hybrid (this component):
 *       ✓ Fast when possible (ESP-NOW)
 *       ✓ Reliable when needed (mesh fallback)
 *       ✓ Transparent to your application code
 * 
 * 
 * =============================================================================
 * USAGE
 * =============================================================================
 * 
 *     // Initialize both transports
 *     EspNowManager& espnow = EspNowManager::instance();
 *     EspMeshManager& mesh = EspMeshManager::instance();
 *     HybridTransport& hybrid = HybridTransport::instance();
 *     
 *     espnow.begin();
 *     mesh.begin(mesh_config);
 *     hybrid.begin();
 *     
 *     // Send with automatic fallback
 *     hybrid.send(dest_mac, data, len);
 *     
 *     // Or specify which transport to use
 *     hybrid.sendVia(TRANSPORT_ESPNOW, dest_mac, data, len);  // Force ESP-NOW
 *     hybrid.sendVia(TRANSPORT_MESH, dest_mac, data, len);    // Force mesh
 *     
 *     // Broadcast to all
 *     hybrid.broadcast(data, len);  // Uses both transports
 * 
 * =============================================================================
 */

#ifndef HYBRID_TRANSPORT_H
#define HYBRID_TRANSPORT_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_now_manager.h"
#include "esp_mesh_manager.h"

/* ─── Transport Selection ────────────────────────────────────────────────── */

/* Reuse the transport bits from device_registry.h */
#ifndef TRANSPORT_ESPNOW
#define TRANSPORT_ESPNOW    0x01
#endif
#ifndef TRANSPORT_MESH
#define TRANSPORT_MESH      0x20  /* New bit for mesh */
#endif

/* ─── Configuration ──────────────────────────────────────────────────────── */

struct HybridConfig {
    /**
     * How long to wait for ESP-NOW ACK before falling back to mesh.
     * 
     * ESP-NOW ACK is typically <10ms if device is in range.
     * 50ms gives plenty of margin.
     */
    uint32_t    espnow_timeout_ms = 50;
    
    /**
     * Number of ESP-NOW retries before falling back.
     * 
     * 1 = try once, if fail go to mesh
     * 2 = try twice, etc.
     */
    uint8_t     espnow_retries = 1;
    
    /**
     * Enable mesh fallback.
     * 
     * TRUE:  If ESP-NOW fails, try mesh (default)
     * FALSE: ESP-NOW only, no fallback
     */
    bool        enable_mesh_fallback = true;
    
    /**
     * For broadcast: which transports to use?
     * 
     * Default: both (ESP-NOW broadcast + mesh broadcast)
     */
    uint8_t     broadcast_transports = TRANSPORT_ESPNOW | TRANSPORT_MESH;
};

/* ─── Send Result ────────────────────────────────────────────────────────── */

enum class HybridResult : uint8_t {
    OK_ESPNOW,      ///< Sent successfully via ESP-NOW
    OK_MESH,        ///< Sent successfully via mesh (ESP-NOW failed)
    FAIL_ALL,       ///< Both transports failed
    FAIL_NO_CONN,   ///< Not connected to either transport
};

/* ─── Callbacks ──────────────────────────────────────────────────────────── */

/**
 * Called when a send completes.
 */
using HybridSendCb = std::function<void(
    const uint8_t dest_mac[6], HybridResult result)>;

/**
 * Called when a message is received (from either transport).
 */
using HybridReceiveCb = std::function<void(
    const uint8_t src_mac[6], const uint8_t* data, size_t len,
    uint8_t transport)>;  /* TRANSPORT_ESPNOW or TRANSPORT_MESH */

/* ─── Main Class ─────────────────────────────────────────────────────────── */

class HybridTransport {
public:
    static HybridTransport& instance();
    HybridTransport(const HybridTransport&) = delete;
    HybridTransport& operator=(const HybridTransport&) = delete;

    /* ─── Lifecycle ────────────────────────────────────────────────────── */

    /**
     * @brief Initialize the hybrid transport.
     * 
     * Call this AFTER initializing EspNowManager and EspMeshManager.
     * This component hooks into both and provides unified send/receive.
     * 
     * @param config  Configuration options
     * @return ESP_OK on success
     */
    esp_err_t begin(const HybridConfig& config = HybridConfig{});

    /**
     * @brief Stop the hybrid transport.
     */
    esp_err_t end();

    /* ─── Sending ──────────────────────────────────────────────────────── */

    /**
     * @brief Send data with automatic fallback.
     * 
     * Tries ESP-NOW first. If no ACK within timeout, retries via mesh.
     * This is the primary send function you should use.
     * 
     * @param dest_mac  Destination MAC address
     * @param data      Data to send
     * @param len       Length of data
     * @return Result indicating which transport succeeded (or failure)
     */
    HybridResult send(const uint8_t dest_mac[6], const uint8_t* data, size_t len);

    /**
     * @brief Send via a specific transport (no fallback).
     * 
     * Use this when you know which transport to use.
     * 
     * @param transport  TRANSPORT_ESPNOW or TRANSPORT_MESH
     * @param dest_mac   Destination MAC address
     * @param data       Data to send
     * @param len        Length of data
     * @return ESP_OK on success
     */
    esp_err_t sendVia(uint8_t transport, const uint8_t dest_mac[6],
                       const uint8_t* data, size_t len);

    /**
     * @brief Broadcast data via all configured transports.
     * 
     * By default, broadcasts via both ESP-NOW and mesh.
     * 
     * @param data  Data to send
     * @param len   Length of data
     * @return ESP_OK if at least one transport succeeded
     */
    esp_err_t broadcast(const uint8_t* data, size_t len);

    /**
     * @brief Send to the mesh root (main controller).
     * 
     * Only works via mesh transport.
     * 
     * @param data  Data to send
     * @param len   Length of data
     * @return ESP_OK on success
     */
    esp_err_t sendToRoot(const uint8_t* data, size_t len);

    /* ─── Callbacks ────────────────────────────────────────────────────── */

    void setSendCallback(HybridSendCb cb);
    void setReceiveCallback(HybridReceiveCb cb);

    /* ─── Status ───────────────────────────────────────────────────────── */

    /**
     * @brief Check which transports are available.
     * 
     * @return Bitmask of available transports
     */
    uint8_t getAvailableTransports() const;

    /**
     * @brief Get statistics.
     */
    struct Stats {
        uint32_t espnow_sent;       ///< Packets sent via ESP-NOW
        uint32_t espnow_acked;      ///< ESP-NOW packets that got ACK
        uint32_t espnow_failed;     ///< ESP-NOW packets that failed
        uint32_t mesh_sent;         ///< Packets sent via mesh (including fallback)
        uint32_t mesh_success;      ///< Mesh packets that succeeded
        uint32_t mesh_failed;       ///< Mesh packets that failed
        uint32_t fallback_count;    ///< Times we fell back to mesh
    };
    
    Stats getStats() const;
    void resetStats();

private:
    HybridTransport();
    ~HybridTransport();

    /* Internal callbacks */
    void onEspNowSend(const uint8_t* mac, bool success);
    void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len);
    void onMeshRecv(const uint8_t* mac, const uint8_t* data, size_t len, bool from_root);

    /* Pending send tracking */
    struct PendingSend {
        uint8_t     dest_mac[6];
        uint8_t*    data;
        size_t      len;
        bool        waiting_ack;
        bool        ack_received;
        bool        ack_success;
        int64_t     send_time;
    };

    /* State */
    bool            _initialized;
    HybridConfig    _config;
    SemaphoreHandle_t _mutex;
    EventGroupHandle_t _ack_event;
    
    PendingSend     _pending;       /* Current pending send (one at a time) */
    Stats           _stats;

    HybridSendCb    _send_cb;
    HybridReceiveCb _recv_cb;

    /* Event bits */
    static constexpr uint32_t BIT_ACK_RECEIVED = BIT0;
};

#endif // HYBRID_TRANSPORT_H
