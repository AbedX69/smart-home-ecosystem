/*
 * =============================================================================
 * FILE:        main.cpp
 * PROJECT:     esp-now-test
 * DESCRIPTION: Comprehensive test for the ESP-NOW Manager component.
 * =============================================================================
 * 
 * HOW TO USE THIS TEST
 * ====================
 * 
 * You need TWO ESP32 boards. Flash this SAME code to both boards.
 * 
 * The test operates in 3 modes controlled by build flags:
 * 
 *   -DESPNOW_MODE_SENDER     → Sends periodic messages to a hardcoded peer
 *   -DESPNOW_MODE_RECEIVER   → Listens and prints received messages
 *   -DESPNOW_MODE_ECHO       → Receives messages and echoes them back (default)
 * 
 * STEP-BY-STEP:
 * 
 *   1. Flash Board A in RECEIVER mode (or ECHO mode)
 *   2. Open serial monitor, note the MAC address printed on boot
 *   3. Update PEER_MAC below with Board A's MAC address
 *   4. Flash Board B in SENDER mode
 *   5. Watch Board A receive messages from Board B
 *   6. If using ECHO mode, Board B also gets replies
 * 
 * For quick testing without changing MAC addresses:
 *   - Flash both boards in ECHO mode (default)
 *   - Both will broadcast a "hello" on boot
 *   - When either receives a broadcast, it echoes back via unicast
 * 
 * =============================================================================
 */

#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_now_manager.h"

static const char* TAG = "ESPNowTest";

/* =============================================================================
 * CONFIGURATION
 * =============================================================================
 * 
 * Change PEER_MAC to the MAC address of your other board.
 * You can find it in the serial monitor when that board boots up.
 * 
 * If you don't know the MAC yet, leave the default and use ECHO/broadcast mode.
 * ========================================================================== */

// Replace with your actual peer's MAC address
// Format: {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
#ifndef PEER_MAC_0
#define PEER_MAC_0  0xFF
#define PEER_MAC_1  0xFF
#define PEER_MAC_2  0xFF
#define PEER_MAC_3  0xFF
#define PEER_MAC_4  0xFF
#define PEER_MAC_5  0xFF
#endif

static const uint8_t PEER_MAC[] = {
    PEER_MAC_0, PEER_MAC_1, PEER_MAC_2,
    PEER_MAC_3, PEER_MAC_4, PEER_MAC_5
};

// Default to ECHO mode if nothing specified
#if !defined(ESPNOW_MODE_SENDER) && !defined(ESPNOW_MODE_RECEIVER) && !defined(ESPNOW_MODE_ECHO)
#define ESPNOW_MODE_ECHO
#endif

/* ─── Send interval (ms) ─────────────────────────────────────────────────── */
#define SEND_INTERVAL_MS    2000

/* ─── Message counter for tracking ───────────────────────────────────────── */
static uint32_t msg_counter = 0;

/* =============================================================================
 * CALLBACKS
 * ========================================================================== */

/**
 * @brief Called when a message is received.
 * 
 * This runs in the receive task context, so we can safely log and do work.
 */
static void onReceive(const uint8_t* sender_mac, const uint8_t* data, int len) {
    char mac_str[18];
    EspNowManager::macToStr(sender_mac, mac_str);

    // Print as string if it looks like text, otherwise hex dump
    bool is_text = true;
    for (int i = 0; i < len; i++) {
        if (data[i] < 0x20 && data[i] != 0x00 && data[i] != '\n' && data[i] != '\r') {
            is_text = false;
            break;
        }
    }

    if (is_text && len < 250) {
        // Null-terminate for safe printing
        char text[251] = {};
        memcpy(text, data, len);
        text[len] = '\0';
        ESP_LOGI(TAG, "📨 RX from %s (%d bytes): \"%s\"", mac_str, len, text);
    } else {
        ESP_LOGI(TAG, "📨 RX from %s (%d bytes):", mac_str, len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);
    }

    /* ── Echo mode: send the message back to whoever sent it ───────── */
#ifdef ESPNOW_MODE_ECHO
    EspNowManager& enm = EspNowManager::instance();

    // Auto-add the sender as a peer if we don't have them yet
    if (!enm.hasPeer(sender_mac)) {
        ESP_LOGI(TAG, "Auto-adding sender %s as peer", mac_str);
        enm.addPeer(sender_mac);
    }

    // Build echo response
    char echo[251];
    int echo_len = snprintf(echo, sizeof(echo), "ECHO: %.*s", (len < 200 ? len : 200), (const char*)data);

    esp_err_t ret = enm.send(sender_mac, (const uint8_t*)echo, echo_len);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "↩️  Echoed back to %s", mac_str);
    } else {
        ESP_LOGE(TAG, "Echo send failed: %s", esp_err_to_name(ret));
    }
#endif
}

/**
 * @brief Called when a send completes (or fails).
 */
static void onSend(const uint8_t* dest_mac, bool success) {
    char mac_str[18];
    EspNowManager::macToStr(dest_mac, mac_str);
    ESP_LOGI(TAG, "📤 TX to %s: %s", mac_str, success ? "✅ ACK" : "❌ FAIL");
}

/* =============================================================================
 * SENDER TASK
 * =============================================================================
 * Periodically sends numbered messages to the configured peer.
 * ========================================================================== */

#if defined(ESPNOW_MODE_SENDER) || defined(ESPNOW_MODE_ECHO)
static void sender_task(void* arg) {
    EspNowManager& enm = EspNowManager::instance();

    // Wait a bit for everything to stabilize
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (true) {
        char msg[128];
        int len;

        // Check if PEER_MAC is broadcast (all 0xFF)
        const uint8_t bcast[] = ESPNOW_BROADCAST_MAC;
        bool is_broadcast = (memcmp(PEER_MAC, bcast, 6) == 0);

        if (is_broadcast) {
            len = snprintf(msg, sizeof(msg), "HELLO #%lu (broadcast)", (unsigned long)msg_counter++);
            esp_err_t ret = enm.broadcast((const uint8_t*)msg, len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Broadcast failed: %s", esp_err_to_name(ret));
            }
        } else {
            len = snprintf(msg, sizeof(msg), "MSG #%lu from sender", (unsigned long)msg_counter++);
            esp_err_t ret = enm.send(PEER_MAC, (const uint8_t*)msg, len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Unicast send failed: %s", esp_err_to_name(ret));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_MS));
    }
}
#endif

/* =============================================================================
 * MAIN
 * ========================================================================== */

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
#if defined(ESPNOW_MODE_SENDER)
    ESP_LOGI(TAG, "║    ESP-NOW Test - SENDER MODE            ║");
#elif defined(ESPNOW_MODE_RECEIVER)
    ESP_LOGI(TAG, "║    ESP-NOW Test - RECEIVER MODE          ║");
#else
    ESP_LOGI(TAG, "║    ESP-NOW Test - ECHO MODE              ║");
#endif
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    /* ── Get the singleton ─────────────────────────────────────────── */
    EspNowManager& enm = EspNowManager::instance();

    /* ── Register callbacks BEFORE begin() ─────────────────────────── */
    enm.setReceiveCallback(onReceive);
    enm.setSendCallback(onSend);

    /* ── Initialize ────────────────────────────────────────────────── */
    EspNowConfig cfg;
    // cfg.channel = 1;       // Uncomment to force a specific channel
    // cfg.long_range = true; // Uncomment for extended range (slower)

    esp_err_t ret = enm.begin(cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Halting.");
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    /* ── Add peer (sender and echo modes) ──────────────────────────── */
#if defined(ESPNOW_MODE_SENDER)
    const uint8_t bcast[] = ESPNOW_BROADCAST_MAC;
    bool is_broadcast = (memcmp(PEER_MAC, bcast, 6) == 0);
    if (!is_broadcast) {
        ret = enm.addPeer(PEER_MAC);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add peer!");
        }
    }
#endif

    /* ── Print peer info ───────────────────────────────────────────── */
    int total = 0, encrypted = 0;
    enm.getPeerCount(total, encrypted);
    ESP_LOGI(TAG, "Peers: %d total, %d encrypted", total, encrypted);

    /* ── Start sender task (sender and echo modes) ─────────────────── */
#if defined(ESPNOW_MODE_SENDER) || defined(ESPNOW_MODE_ECHO)
    xTaskCreate(sender_task, "espnow_tx", 4096, nullptr, 4, nullptr);
    ESP_LOGI(TAG, "Sender task started (interval: %d ms)", SEND_INTERVAL_MS);
#endif

    /* ── Receiver mode: just wait forever, callback handles everything */
#if defined(ESPNOW_MODE_RECEIVER)
    ESP_LOGI(TAG, "Listening for ESP-NOW messages...");
#endif

    /* ── Status reporting loop ─────────────────────────────────────── */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(30000));  // Every 30 seconds
        enm.getPeerCount(total, encrypted);
        ESP_LOGI(TAG, "[Status] Peers: %d | Messages sent: %lu",
                 total, (unsigned long)msg_counter);
    }
}
