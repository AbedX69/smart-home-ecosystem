/*
 * =============================================================================
 * FILE:        main.cpp
 * PROJECT:     mesh-test
 * AUTHOR:      AbedX69
 * CREATED:     2026-03-26
 * VERSION:     1.0.0
 * =============================================================================
 *
 * HOW TO USE THIS TEST
 * ====================
 *
 * You need at least TWO ESP32 boards. Flash ONE as ROOT, rest as NODE/LEAF.
 *
 * MODES (set via build flags in platformio.ini or CMake):
 *
 *   -DMESH_MODE_ROOT   → The main controller. Connects to your home WiFi
 *                        router first, then starts the mesh. Only ONE per mesh.
 *
 *   -DMESH_MODE_NODE   → Regular node. Joins the mesh, can relay messages
 *                        to/from other nodes. Good for mains-powered devices.
 *
 *   -DMESH_MODE_LEAF   → Leaf node. Joins the mesh but accepts no children.
 *                        Simulates a battery-powered device.
 *
 * STEP-BY-STEP:
 *
 *   1. Edit platformio.ini, set ROOT mode + router credentials for Board A
 *   2. Flash Board A → it is now the mesh root
 *   3. Open serial monitor on Board A, verify it connects to your router
 *   4. Flash Board B (and C, D...) in NODE mode (default)
 *   5. Watch nodes connect and join the mesh tree
 *   6. Each node sends periodic messages to the root
 *   7. Root broadcasts periodic messages to all nodes
 *   8. Status prints every 30 s (layer, child count, total nodes)
 *
 * WHAT TO LOOK FOR:
 *
 *   Root serial output:
 *     [MeshTest] Connected as ROOT (layer 1)
 *     [MeshTest] Child connected: AA:BB:CC:DD:EE:FF
 *     [MeshTest] RX from AA:BB:... (12 bytes): "NODE #0"
 *     [MeshTest] Broadcast #1 sent to all nodes
 *
 *   Node serial output:
 *     [MeshTest] Connected as NODE (layer 2)
 *     [MeshTest] RX from root (8 bytes): "BCAST #0"
 *     [MeshTest] TX to root: MSG #0 → OK
 *
 * =============================================================================
 * PREREQUISITE
 * =============================================================================
 *
 * WiFiManager MUST be initialized before EspMeshManager.
 * This test does it automatically inside app_main().
 *
 * =============================================================================
 */

#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "esp_mesh_manager.h"

static const char* TAG = "MeshTest";

/* =============================================================================
 * CONFIGURATION
 * =============================================================================
 *
 * All nodes in the same mesh MUST share the same MESH_ID and MESH_PASS.
 * ROOT additionally needs the router SSID/password.
 *
 * You can override MESH_ROUTER_SSID and MESH_ROUTER_PASS via build flags.
 * ========================================================================== */

/* Mesh network identity — same on every board */
static const uint8_t MESH_ID[6]   = {'S', 'M', 'E', 'S', 'H', '1'};
static const char    MESH_PASS[]  = "smarthome_mesh";

/* Router credentials (root only) — override in platformio.ini */
#ifndef MESH_ROUTER_SSID
#define MESH_ROUTER_SSID  "YourRouterSSID"
#endif
#ifndef MESH_ROUTER_PASS
#define MESH_ROUTER_PASS  "YourRouterPassword"
#endif

/* Default to NODE if no mode specified */
#if !defined(MESH_MODE_ROOT) && !defined(MESH_MODE_NODE) && !defined(MESH_MODE_LEAF)
#define MESH_MODE_NODE
#endif

/* How often nodes send a message to the root (ms) */
#define SEND_INTERVAL_MS    5000

/* How often root broadcasts to all nodes (ms) */
#define BCAST_INTERVAL_MS   7000

/* =============================================================================
 * STATE
 * ========================================================================== */

static uint32_t tx_counter  = 0;
static uint32_t rx_counter  = 0;
static uint32_t tx_failures = 0;

/* =============================================================================
 * CALLBACKS
 * ========================================================================== */

/**
 * @brief Called for every mesh event (connection changes, layer changes, etc.)
 */
static void onMeshEvent(MeshEvent event, const MeshEventInfo* info) {
    switch (event) {

        case MeshEvent::STARTED:
            ESP_LOGI(TAG, "Mesh stack started");
            break;

        case MeshEvent::CONNECTED: {
            char mac_str[18] = "??:??:??:??:??:??";
            if (info) EspMeshManager::macToStr(info->mac, mac_str);

#ifdef MESH_MODE_ROOT
            ESP_LOGI(TAG, "═══ Connected as ROOT (layer %d) ═══",
                     info ? info->layer : 0);
            ESP_LOGI(TAG, "  Router connected, mesh is up");
#else
            ESP_LOGI(TAG, "═══ Connected to mesh (layer %d) ═══",
                     info ? info->layer : 0);
            ESP_LOGI(TAG, "  Parent: %s", mac_str);
#endif
            break;
        }

        case MeshEvent::DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from mesh — searching for parent...");
            break;

        case MeshEvent::CHILD_CONNECTED: {
            char mac_str[18] = {};
            if (info) EspMeshManager::macToStr(info->mac, mac_str);
            ESP_LOGI(TAG, "Child connected: %s (total children: %d)",
                     mac_str, info ? info->child_count : 0);
            break;
        }

        case MeshEvent::CHILD_DISCONNECTED: {
            char mac_str[18] = {};
            if (info) EspMeshManager::macToStr(info->mac, mac_str);
            ESP_LOGW(TAG, "Child disconnected: %s (remaining: %d)",
                     mac_str, info ? info->child_count : 0);
            break;
        }

        case MeshEvent::LAYER_CHANGED:
            ESP_LOGI(TAG, "Layer changed → %d", info ? info->layer : 0);
            break;

        case MeshEvent::STOPPED:
            ESP_LOGW(TAG, "Mesh stopped");
            break;

        default:
            break;
    }
}

/**
 * @brief Called when a message is received over the mesh.
 */
static void onMeshReceive(const uint8_t src_mac[6], const uint8_t* data,
                           size_t len, bool from_root) {
    rx_counter++;

    char mac_str[18];
    EspMeshManager::macToStr(src_mac, mac_str);

    /* Try to print as text */
    bool is_text = true;
    for (size_t i = 0; i < len; i++) {
        if (data[i] < 0x20 || data[i] > 0x7E) { is_text = false; break; }
    }

    if (is_text && len < 240) {
        char text[241] = {};
        memcpy(text, data, len);
        ESP_LOGI(TAG, "RX [%s %s] %zu bytes: \"%s\"",
                 mac_str,
                 from_root ? "(root→)" : "(node→)",
                 len, text);
    } else {
        ESP_LOGI(TAG, "RX [%s] %zu bytes (binary)", mac_str, len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);
    }
}

/**
 * @brief Called when a mesh send completes.
 */
static void onMeshSend(const uint8_t dest_mac[6], bool success) {
    if (!success) {
        char mac_str[18];
        EspMeshManager::macToStr(dest_mac, mac_str);
        ESP_LOGW(TAG, "TX to %s: FAILED", mac_str);
        tx_failures++;
    }
}

/* =============================================================================
 * ROOT BROADCAST TASK
 * =============================================================================
 * Root periodically floods a message to every node in the mesh.
 * ========================================================================== */

#ifdef MESH_MODE_ROOT
static void root_bcast_task(void* arg) {
    EspMeshManager& mesh = EspMeshManager::instance();

    /* Wait until connected */
    while (!mesh.isConnected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    vTaskDelay(pdMS_TO_TICKS(2000));  /* Extra settle time */

    while (true) {
        char msg[64];
        int len = snprintf(msg, sizeof(msg), "BCAST #%lu from ROOT",
                           (unsigned long)tx_counter++);

        esp_err_t ret = mesh.broadcast((const uint8_t*)msg, len);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Broadcast #%lu sent (%d nodes in mesh)",
                     (unsigned long)(tx_counter - 1), mesh.getTotalNodes());
        } else {
            ESP_LOGW(TAG, "Broadcast failed: %s", esp_err_to_name(ret));
            tx_failures++;
        }

        vTaskDelay(pdMS_TO_TICKS(BCAST_INTERVAL_MS));
    }
}
#endif

/* =============================================================================
 * NODE SEND TASK
 * =============================================================================
 * Non-root nodes periodically send a message up to the root.
 * ========================================================================== */

#if defined(MESH_MODE_NODE) || defined(MESH_MODE_LEAF)
static void node_send_task(void* arg) {
    EspMeshManager& mesh = EspMeshManager::instance();

    /* Wait until connected to mesh */
    while (!mesh.isConnected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    vTaskDelay(pdMS_TO_TICKS(3000));  /* Settle */

    while (true) {
        char msg[64];
        int len = snprintf(msg, sizeof(msg),
#ifdef MESH_MODE_LEAF
                           "LEAF MSG #%lu (layer %d)",
#else
                           "NODE MSG #%lu (layer %d)",
#endif
                           (unsigned long)tx_counter++,
                           (int)mesh.getLayer());

        esp_err_t ret = mesh.sendToRoot((const uint8_t*)msg, len);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "TX to root: \"%s\" → queued", msg);
        } else {
            ESP_LOGW(TAG, "TX to root failed: %s", esp_err_to_name(ret));
            tx_failures++;
        }

        vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_MS));
    }
}
#endif

/* =============================================================================
 * MAIN
 * ========================================================================== */

extern "C" void app_main(void) {
    /* ── Banner ──────────────────────────────────────────────────────── */
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
#if defined(MESH_MODE_ROOT)
    ESP_LOGI(TAG, "║      ESP-MESH Test  -  ROOT MODE         ║");
#elif defined(MESH_MODE_LEAF)
    ESP_LOGI(TAG, "║      ESP-MESH Test  -  LEAF MODE         ║");
#else
    ESP_LOGI(TAG, "║      ESP-MESH Test  -  NODE MODE         ║");
#endif
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    /* ── NVS (required by WiFi) ──────────────────────────────────────── */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ── Step 1: Init WiFi stack (REQUIRED before EspMeshManager) ────── */
    /* EspMeshManager builds on top of the WiFi stack.
     * WiFiManager handles: netif init, event loop, wifi init, wifi start.
     * We call begin() in a minimal mode (no SSID needed for non-root). */
/* Minimal WiFi stack init for mesh (no connection, mesh handles that) */ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(nullptr, nullptr));
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi stack ready");

    /* ── Step 2: Configure mesh ──────────────────────────────────────── */
    EspMeshManager& mesh = EspMeshManager::instance();

    /* Register callbacks BEFORE begin() */
    mesh.setEventCallback(onMeshEvent);
    mesh.setReceiveCallback(onMeshReceive);
    mesh.setSendCallback(onMeshSend);

    MeshConfig cfg;
    memcpy(cfg.mesh_id, MESH_ID, sizeof(MESH_ID));
    strncpy(cfg.mesh_pass, MESH_PASS, sizeof(cfg.mesh_pass) - 1);

#ifdef MESH_MODE_ROOT
    cfg.is_root = true;
    strncpy(cfg.router_ssid, MESH_ROUTER_SSID, sizeof(cfg.router_ssid) - 1);
    strncpy(cfg.router_pass, MESH_ROUTER_PASS, sizeof(cfg.router_pass) - 1);
    cfg.allow_root_election = false;   /* We are always root */
    ESP_LOGI(TAG, "Router SSID: %s", cfg.router_ssid);
#endif

#ifdef MESH_MODE_LEAF
    cfg.is_root   = false;
    cfg.leaf_only = true;
    strncpy(cfg.router_ssid, MESH_ROUTER_SSID, sizeof(cfg.router_ssid) - 1);
    strncpy(cfg.router_pass, MESH_ROUTER_PASS, sizeof(cfg.router_pass) - 1);
#endif

#ifdef MESH_MODE_NODE
    cfg.is_root   = false;
    cfg.leaf_only = false;
    strncpy(cfg.router_ssid, MESH_ROUTER_SSID, sizeof(cfg.router_ssid) - 1);
    strncpy(cfg.router_pass, MESH_ROUTER_PASS, sizeof(cfg.router_pass) - 1);
#endif

    /* ── Step 3: Start mesh ──────────────────────────────────────────── */
    ret = mesh.begin(cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mesh init failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Halting.");
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
    ESP_LOGI(TAG, "Mesh started — waiting to connect...");

    /* ── Step 4: Start mode-specific tasks ───────────────────────────── */
#ifdef MESH_MODE_ROOT
    xTaskCreate(root_bcast_task, "mesh_bcast", 4096, nullptr, 4, nullptr);
    ESP_LOGI(TAG, "Root broadcast task started (interval: %d ms)", BCAST_INTERVAL_MS);
#endif

#if defined(MESH_MODE_NODE) || defined(MESH_MODE_LEAF)
    xTaskCreate(node_send_task, "mesh_tx", 4096, nullptr, 4, nullptr);
    ESP_LOGI(TAG, "Node send task started (interval: %d ms)", SEND_INTERVAL_MS);
#endif

    /* ── Step 5: Status reporting loop ──────────────────────────────── */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(30000));   /* Every 30 seconds */

        ESP_LOGI(TAG, "──────────── STATUS ────────────");
        ESP_LOGI(TAG, "  Connected : %s", mesh.isConnected() ? "YES" : "NO");
        ESP_LOGI(TAG, "  Is root   : %s", mesh.isRoot()      ? "YES" : "NO");
        ESP_LOGI(TAG, "  Layer     : %d", mesh.getLayer());
        ESP_LOGI(TAG, "  Children  : %d", mesh.getChildCount());
        ESP_LOGI(TAG, "  Total nodes (root only accurate): %d", mesh.getTotalNodes());
        ESP_LOGI(TAG, "  TX sent   : %lu", (unsigned long)tx_counter);
        ESP_LOGI(TAG, "  RX recv   : %lu", (unsigned long)rx_counter);
        ESP_LOGI(TAG, "  TX fail   : %lu", (unsigned long)tx_failures);

        uint8_t own_mac[6];
        mesh.getOwnMac(own_mac);
        char mac_str[18];
        EspMeshManager::macToStr(own_mac, mac_str);
        ESP_LOGI(TAG, "  Own MAC   : %s", mac_str);
        ESP_LOGI(TAG, "────────────────────────────────");
    }
}
