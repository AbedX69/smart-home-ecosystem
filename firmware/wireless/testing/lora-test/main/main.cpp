/*
 * =============================================================================
 * FILE:        main.cpp
 * PROJECT:     lora-test
 * DESCRIPTION: Test for LoRa SX1262 component.
 * =============================================================================
 * 
 * MODES (set via build flags):
 * 
 *   -DLORA_TEST_TX       → Transmit sensor beacon every 5s (default)
 *   -DLORA_TEST_RX       → Gateway mode: continuous receive, log all packets
 *   -DLORA_TEST_PINGPONG → Alternate TX/RX for range testing
 * 
 * =============================================================================
 */

#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "lora_sx1262.h"

static const char* TAG = "LoRaTest";

/* Default to TX mode */
#if !defined(LORA_TEST_TX) && !defined(LORA_TEST_RX) && !defined(LORA_TEST_PINGPONG)
#define LORA_TEST_TX
#endif

/* ─── Packet Format (shared between TX and RX) ───────────────────────────── */

/*
 *   Byte 0:     Packet type (0x01=sensor, 0x02=ping, 0x03=pong)
 *   Byte 1:     Node ID
 *   Byte 2-3:   Sequence number (big-endian)
 *   Byte 4+:    Payload (type-dependent)
 */

#define PKT_TYPE_SENSOR  0x01
#define PKT_TYPE_PING    0x02
#define PKT_TYPE_PONG    0x03

static uint16_t seq_num = 0;

/* ─── RX Callback ─────────────────────────────────────────────────────────── */

#if defined(LORA_TEST_RX)

static uint32_t rx_count = 0;

static void onPacketReceived(const LoRaRxPacket* pkt) {
    rx_count++;

    ESP_LOGI(TAG, "╔═══════════ PACKET #%lu ═══════════╗", (unsigned long)rx_count);
    ESP_LOGI(TAG, "║  Length: %d bytes", pkt->length);
    ESP_LOGI(TAG, "║  RSSI:  %d dBm", pkt->rssi);
    ESP_LOGI(TAG, "║  SNR:   %d dB", pkt->snr);

    if (pkt->length >= 4) {
        uint8_t type = pkt->data[0];
        uint8_t node = pkt->data[1];
        uint16_t seq = ((uint16_t)pkt->data[2] << 8) | pkt->data[3];

        const char* type_str = (type == PKT_TYPE_SENSOR) ? "SENSOR" :
                                (type == PKT_TYPE_PING) ? "PING" :
                                (type == PKT_TYPE_PONG) ? "PONG" : "UNKNOWN";

        ESP_LOGI(TAG, "║  Type:  %s (0x%02X)", type_str, type);
        ESP_LOGI(TAG, "║  Node:  %d", node);
        ESP_LOGI(TAG, "║  Seq:   %d", seq);

        /* If sensor packet, parse payload */
        if (type == PKT_TYPE_SENSOR && pkt->length >= 8) {
            int16_t temp_x10 = ((int16_t)pkt->data[4] << 8) | pkt->data[5];
            uint16_t hum_x10 = ((uint16_t)pkt->data[6] << 8) | pkt->data[7];
            ESP_LOGI(TAG, "║  Temp:  %.1f°C", temp_x10 / 10.0f);
            ESP_LOGI(TAG, "║  Hum:   %.1f%%", hum_x10 / 10.0f);
        }
    } else {
        /* Raw data dump for short/unknown packets */
        char hex[128] = {};
        for (int i = 0; i < pkt->length && i < 32; i++) {
            sprintf(hex + i * 3, "%02X ", pkt->data[i]);
        }
        ESP_LOGI(TAG, "║  Data:  %s", hex);
    }
    ESP_LOGI(TAG, "╚══════════════════════════════════╝");
}

#endif /* LORA_TEST_RX */

/* ─── Ping-Pong Callback ─────────────────────────────────────────────────── */

#if defined(LORA_TEST_PINGPONG)

static bool waiting_pong = false;
static uint32_t ping_sent_time = 0;

static void onPingPong(const LoRaRxPacket* pkt) {
    if (pkt->length < 4) return;

    uint8_t type = pkt->data[0];
    uint16_t seq = ((uint16_t)pkt->data[2] << 8) | pkt->data[3];

    if (type == PKT_TYPE_PING) {
        ESP_LOGI(TAG, "Got PING #%d (RSSI=%d, SNR=%d) → sending PONG",
                 seq, pkt->rssi, pkt->snr);

        /* Send pong back */
        uint8_t pong[4];
        pong[0] = PKT_TYPE_PONG;
        pong[1] = 0x02;  // Node 2
        pong[2] = pkt->data[2];
        pong[3] = pkt->data[3];

        vTaskDelay(pdMS_TO_TICKS(100));  // Small delay before TX
        LoRaSX1262::instance().send(pong, 4);

        /* Re-enter RX */
        LoRaSX1262::instance().startReceive();
    }
    else if (type == PKT_TYPE_PONG && waiting_pong) {
        uint32_t rtt = (uint32_t)(esp_timer_get_time() / 1000) - ping_sent_time;
        ESP_LOGI(TAG, "Got PONG #%d → RTT: %lu ms (RSSI=%d, SNR=%d)",
                 seq, (unsigned long)rtt, pkt->rssi, pkt->snr);
        waiting_pong = false;
    }
}

#endif /* LORA_TEST_PINGPONG */

/* =============================================================================
 * MAIN
 * ========================================================================== */

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
#if defined(LORA_TEST_TX)
    ESP_LOGI(TAG, "║     LoRa Test - SENSOR BEACON TX          ║");
#elif defined(LORA_TEST_RX)
    ESP_LOGI(TAG, "║     LoRa Test - GATEWAY RX                ║");
#else
    ESP_LOGI(TAG, "║     LoRa Test - PING-PONG                 ║");
#endif
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    /* ── Configure LoRa ────────────────────────────────────────────── */
    LoRaSX1262& lora = LoRaSX1262::instance();

    LoRaConfig config;
    config.frequency = 915000000;       // 915 MHz (US ISM band)
    config.spreading_factor = 7;        // Fast, ~2km range
    config.bandwidth = 7;               // 125 kHz (index 7)
    config.coding_rate = 1;             // 4/5
    config.tx_power = 22;              // Max power
    config.crc_on = true;
    config.sync_word = 0x12;           // Private network

    esp_err_t ret = lora.begin(LoRaPinPresets::XIAO_S3_WIO_B2B, config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LoRa init failed: %s", esp_err_to_name(ret));
        return;
    }

    /* ── SENSOR BEACON TX MODE ─────────────────────────────────────── */
#if defined(LORA_TEST_TX)

    lora.setTxDoneCallback([]() {
        ESP_LOGI(TAG, "Beacon transmitted");
    });

    uint8_t node_id = 0x01;

    while (true) {
        /* Build sensor packet with simulated data */
        int16_t temp_x10 = 235 + (seq_num % 20);   // 23.5 - 25.4 °C
        uint16_t hum_x10 = 580 + (seq_num % 30);   // 58.0 - 60.9 %

        uint8_t packet[8];
        packet[0] = PKT_TYPE_SENSOR;
        packet[1] = node_id;
        packet[2] = (seq_num >> 8) & 0xFF;
        packet[3] = seq_num & 0xFF;
        packet[4] = (temp_x10 >> 8) & 0xFF;
        packet[5] = temp_x10 & 0xFF;
        packet[6] = (hum_x10 >> 8) & 0xFF;
        packet[7] = hum_x10 & 0xFF;

        ESP_LOGI(TAG, "TX beacon #%d: temp=%.1f°C hum=%.1f%%",
                 seq_num, temp_x10 / 10.0f, hum_x10 / 10.0f);

        ret = lora.send(packet, sizeof(packet));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Send failed: %s", esp_err_to_name(ret));
        }

        seq_num++;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    /* ── GATEWAY RX MODE ───────────────────────────────────────────── */
#elif defined(LORA_TEST_RX)

    lora.setRxCallback(onPacketReceived);
    lora.startReceive();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  Gateway listening on 915.00 MHz...");
    ESP_LOGI(TAG, "  SF7 / BW125 / CR4/5");
    ESP_LOGI(TAG, "");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        ESP_LOGI(TAG, "Gateway alive. Packets received: %lu", (unsigned long)rx_count);
    }

    /* ── PING-PONG MODE ────────────────────────────────────────────── */
#else

    lora.setRxCallback(onPingPong);

    /*
     * This device acts as the PING sender.
     * Flash the other device with LORA_TEST_PINGPONG too —
     * it will respond to PINGs with PONGs automatically.
     *
     * To make one device the "responder only", just set:
     *   node_id = 0x02 and skip the ping-sending loop.
     */
    uint8_t node_id = 0x01;  // Change to 0x02 for responder

    if (node_id == 0x01) {
        /* Ping sender */
        while (true) {
            uint8_t ping[4];
            ping[0] = PKT_TYPE_PING;
            ping[1] = node_id;
            ping[2] = (seq_num >> 8) & 0xFF;
            ping[3] = seq_num & 0xFF;

            ESP_LOGI(TAG, "Sending PING #%d...", seq_num);
            waiting_pong = true;
            ping_sent_time = (uint32_t)(esp_timer_get_time() / 1000);

            ret = lora.send(ping, 4);
            if (ret == ESP_OK) {
                /* Enter RX to wait for pong */
                lora.receiveOnce(3000);

                /* Wait for pong or timeout */
                uint32_t wait_start = xTaskGetTickCount();
                while (waiting_pong &&
                       (xTaskGetTickCount() - wait_start) < pdMS_TO_TICKS(3000)) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }

                if (waiting_pong) {
                    ESP_LOGW(TAG, "No PONG received (timeout)");
                    waiting_pong = false;
                }
            }

            seq_num++;
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    } else {
        /* Pong responder: just listen */
        lora.startReceive();
        ESP_LOGI(TAG, "Responder mode — waiting for PINGs...");

        while (true) {
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
    }

#endif
}
