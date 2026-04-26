/*
 * =============================================================================
 * FILE:        esp_mesh_manager.h
 * AUTHOR:      AbedX69
 * CREATED:     2026-03-18
 * VERSION:     1.0.0
 * LICENSE:     MIT
 * PLATFORM:    All ESP32 variants (ESP-IDF v5.x)
 * =============================================================================
 * 
 * ESP-MESH Manager — WiFi-based mesh networking with automatic routing.
 * 
 * Provides:
 *   - Self-healing mesh network (devices route for each other)
 *   - Automatic parent selection and rerouting
 *   - Root node connects to your home WiFi router
 *   - Non-root nodes connect through the mesh (no router needed)
 *   - Works alongside ESP-NOW (dual-transport system)
 *   - Sleep support for leaf nodes (battery devices)
 * 
 * =============================================================================
 * BEGINNER'S GUIDE: ESP-MESH
 * =============================================================================
 * 
 * WHAT IS ESP-MESH?
 * ~~~~~~~~~~~~~~~~~
 * ESP-MESH is WiFi-based mesh networking built into ESP-IDF. Unlike ESP-NOW
 * (point-to-point), mesh devices automatically route messages through
 * intermediate nodes to reach devices that are too far away.
 * 
 * Think of it like this:
 * 
 *     ESP-NOW (what you know):
 *     ~~~~~~~~~~~~~~~~~~~~~~~~
 *     Device A ─────────────────────────────────► Device B
 *                    Direct. Must be in range.
 *     
 *     If B is too far? Message fails. No backup.
 * 
 * 
 *     ESP-MESH (this component):
 *     ~~~~~~~~~~~~~~~~~~~~~~~~~~
 *     Device A ──► Device C ──► Device D ──► Device B
 *                    │
 *                    └── Automatic routing through intermediate nodes!
 *     
 *     If B is too far from A, the mesh finds a path through C and D.
 *     If C dies, the mesh reroutes through other nodes.
 * 
 * 
 * THE TREE STRUCTURE:
 * ~~~~~~~~~~~~~~~~~~~
 * ESP-MESH forms a tree, not a web. There's one "root" at the top,
 * and everyone else connects as children/grandchildren.
 * 
 *                      [Your Home Router]
 *                             │
 *                             │ regular WiFi
 *                             │
 *                      ┌──────┴──────┐
 *                      │  ROOT NODE  │  ← Your main controller
 *                      │ (Layer 1)   │     Connects to router + mesh
 *                      └──────┬──────┘
 *                             │
 *              ┌──────────────┼──────────────┐
 *              │              │              │
 *        ┌─────┴─────┐  ┌─────┴─────┐  ┌─────┴─────┐
 *        │  Node A   │  │  Node B   │  │  Node C   │   Layer 2
 *        │ (parent)  │  │ (parent)  │  │  (leaf)   │   
 *        └─────┬─────┘  └─────┬─────┘  └───────────┘
 *              │              │
 *        ┌─────┴─────┐  ┌─────┴─────┐
 *        │  Node D   │  │  Node E   │                  Layer 3
 *        │  (leaf)   │  │  (leaf)   │
 *        └───────────┘  └───────────┘
 * 
 *     ROOT:   Connects to your home WiFi. Bridge to internet.
 *     PARENT: Has children below it. Relays messages. Must stay awake.
 *     LEAF:   No children. Can sleep. Like Zigbee end devices.
 * 
 * 
 * WHY USE MESH + ESP-NOW TOGETHER?
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Your design: ESP-NOW first, mesh as fallback.
 * 
 *     ┌─────────────────────────────────────────────────────────┐
 *     │                                                         │
 *     │   Switch wants to turn on Light:                        │
 *     │                                                         │
 *     │   1. Try ESP-NOW direct ──────────────► Light           │
 *     │      │                                                  │
 *     │      ├── ACK received? ✓ Done! Fast path worked.        │
 *     │      │                                                  │
 *     │      └── No ACK? Light is out of range...               │
 *     │              │                                          │
 *     │              └── 2. Send via mesh ──► Router ──► Light  │
 *     │                     (automatic routing)                 │
 *     │                                                         │
 *     └─────────────────────────────────────────────────────────┘
 * 
 *     ESP-NOW: Fast (~1-5ms), low power, but no routing
 *     MESH:    Slower (~10-50ms), but reaches anywhere in the house
 * 
 * 
 * BATTERY DEVICES:
 * ~~~~~~~~~~~~~~~~
 * Leaf nodes can sleep. They don't relay for others.
 * When they wake, they send their message and go back to sleep.
 * 
 * Parent nodes CANNOT sleep — they must relay for their children.
 * Your wall-powered devices (lights, controllers) should be parents.
 * Your battery devices (sensors, remotes) should be leaves.
 * 
 * 
 * =============================================================================
 * HOW IT WORKS UNDER THE HOOD
 * =============================================================================
 * 
 * MESH FORMATION (boot sequence):
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * 
 *     1. Root node boots
 *        ├── Connects to your home WiFi router (STA mode)
 *        ├── Starts mesh network (becomes root)
 *        └── Waits for children
 * 
 *     2. Other nodes boot
 *        ├── Scan for mesh networks
 *        ├── Find root's network (by MESH_ID)
 *        ├── Connect to nearest parent (root or another node)
 *        └── Ready to send/receive
 * 
 *     3. Self-healing
 *        ├── Parent dies? Children find new parents automatically
 *        ├── Root dies? Mesh elects new root (if enabled)
 *        └── Node recovers? Rejoins mesh
 * 
 * 
 * MESSAGE ROUTING:
 * ~~~~~~~~~~~~~~~~
 * You don't think about routing — the mesh handles it.
 * 
 *     meshSend(dest_mac, data, len);  // That's it
 *     
 *     Mesh figures out:
 *       - Is dest a direct child? Send down.
 *       - Is dest a sibling? Send up to parent, then down.
 *       - Is dest the root? Send up.
 *       - Is dest unknown? Flood or fail.
 * 
 * 
 * =============================================================================
 * INTEGRATION WITH YOUR ECOSYSTEM
 * =============================================================================
 * 
 * This component plugs into your existing MessageProtocol:
 * 
 *     // In your main.cpp
 *     EspNowManager& espnow = EspNowManager::instance();
 *     EspMeshManager& mesh = EspMeshManager::instance();
 *     MessageProtocol& msg = MessageProtocol::instance();
 *     
 *     // Register both transports
 *     msg.registerTransport(TRANSPORT_ESPNOW, espnow_broadcast, espnow_unicast);
 *     msg.registerTransport(TRANSPORT_MESH, mesh_broadcast, mesh_unicast);
 *     
 *     // Send with fallback
 *     hybrid.sendWithFallback(dest_mac, data, len);
 *     // Tries ESP-NOW first, mesh if ESP-NOW fails
 * 
 * 
 * =============================================================================
 * USAGE EXAMPLES
 * =============================================================================
 * 
 * PREREQUISITE: WiFiManager must be initialized first!
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * ESP-MESH builds on top of the WiFi stack. Your WiFiManager handles:
 *   - esp_netif_init()
 *   - esp_event_loop_create_default()  
 *   - esp_wifi_init()
 *   - esp_wifi_start()
 * 
 * This component adds mesh on top of that.
 * 
 * 
 * ROOT NODE (your main controller):
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *     WiFiManager& wifi = WiFiManager::instance();
 *     wifi.begin();  // Initialize WiFi stack first!
 *     
 *     EspMeshManager& mesh = EspMeshManager::instance();
 *     
 *     MeshConfig cfg;
 *     cfg.is_root = true;
 *     strncpy(cfg.router_ssid, "MyHomeWiFi", sizeof(cfg.router_ssid));
 *     strncpy(cfg.router_pass, "password123", sizeof(cfg.router_pass));
 *     
 *     mesh.setEventCallback(onMeshEvent);
 *     mesh.setReceiveCallback(onMeshReceive);
 *     mesh.begin(cfg);
 * 
 * 
 * NON-ROOT NODE (light, sensor, etc.):
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *     WiFiManager& wifi = WiFiManager::instance();
 *     wifi.begin();  // Initialize WiFi stack first!
 *     
 *     MeshConfig cfg;
 *     cfg.is_root = false;
 *     cfg.allow_leaf_only = true;  // Battery device? Set true.
 *     
 *     mesh.begin(cfg);
 * 
 * 
 * SENDING DATA:
 * ~~~~~~~~~~~~~
 *     // To specific device
 *     mesh.sendTo(dest_mac, data, len);
 *     
 *     // To root (main controller)
 *     mesh.sendToRoot(data, len);
 *     
 *     // Broadcast to all mesh nodes
 *     mesh.broadcast(data, len);
 * 
 * =============================================================================
 */

#ifndef ESP_MESH_MANAGER_H
#define ESP_MESH_MANAGER_H

#include <cstdint>
#include <cstring>
#include <functional>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

/* ─── Constants ──────────────────────────────────────────────────────────── */

#define MESH_MAX_PAYLOAD        1460    /* Max TCP-like payload per packet */
#define MESH_CHANNEL_DEFAULT    0       /* 0 = auto-select */
#define MESH_MAX_LAYER          6       /* Max hops from root */
#define MESH_AP_CONNECTIONS     6       /* Max children per node */
#define MESH_VOTE_PERCENT       100     /* Root election: 100% = no election */
#define MESH_ID_LEN             6

/* ─── Event Types ────────────────────────────────────────────────────────── */

/**
 * Events fired by the mesh manager.
 * 
 * These map to the underlying ESP-MESH events but are simplified
 * for easier handling.
 */
enum class MeshEvent : uint8_t {
    STARTED,            ///< Mesh stack started
    STOPPED,            ///< Mesh stack stopped
    CONNECTED,          ///< Connected to parent (or router if root)
    DISCONNECTED,       ///< Disconnected from parent
    CHILD_CONNECTED,    ///< A child connected to us
    CHILD_DISCONNECTED, ///< A child disconnected from us
    ROOT_OBTAINED,      ///< This node became root
    ROOT_LOST,          ///< This node lost root status
    PARENT_CHANGED,     ///< Switched to a different parent
    LAYER_CHANGED,      ///< Our layer in the tree changed
    NETWORK_STATE,      ///< Network state info (for debugging)
};

/**
 * Additional info provided with some events.
 */
struct MeshEventInfo {
    uint8_t     mac[6];         ///< MAC of relevant device (child, parent)
    uint8_t     layer;          ///< Current layer (1 = root)
    uint8_t     child_count;    ///< Number of connected children
    bool        is_root;        ///< Are we root?
    bool        has_parent;     ///< Do we have a parent?
};

/* ─── Callbacks ──────────────────────────────────────────────────────────── */

/**
 * Called when mesh events occur.
 */
using MeshEventCb = std::function<void(MeshEvent event, const MeshEventInfo* info)>;

/**
 * Called when a message is received over the mesh.
 * 
 * @param src_mac   MAC address of the original sender
 * @param data      Pointer to received data
 * @param len       Length of data
 * @param from_root true if message came from root direction
 */
using MeshReceiveCb = std::function<void(
    const uint8_t src_mac[6], const uint8_t* data, size_t len, bool from_root)>;

/**
 * Called when a mesh send completes.
 * 
 * @param dest_mac  MAC we sent to
 * @param success   true if send succeeded
 */
using MeshSendCb = std::function<void(const uint8_t dest_mac[6], bool success)>;

/* ─── Configuration ──────────────────────────────────────────────────────── */

struct MeshConfig {
    /* ── Network Identity ─────────────────────────────────────────────── */
    
    /**
     * Mesh ID — all nodes with the same ID form one mesh.
     * Like a network name. Default: "SMESH1" (Smart Mesh 1)
     */
    uint8_t     mesh_id[MESH_ID_LEN] = {'S', 'M', 'E', 'S', 'H', '1'};
    
    /**
     * Password for mesh network. All nodes must use the same.
     * Min 8 characters. Used for WPA2 between mesh nodes.
     */
    char        mesh_pass[64] = "smarthome_mesh";
    
    /**
     * WiFi channel for the mesh. 0 = auto-select.
     * If root connects to a router, mesh uses router's channel.
     */
    uint8_t     channel = MESH_CHANNEL_DEFAULT;
    
    /* ── Root Configuration ───────────────────────────────────────────── */
    
    /**
     * Is this node the root (main controller)?
     * 
     * TRUE:  This node will connect to your home WiFi router
     *        and become the mesh root. Only ONE root per mesh.
     * 
     * FALSE: This node will connect to the mesh through
     *        another node (parent). It never talks to the router.
     */
    bool        is_root = false;
    
    /**
     * Home WiFi router credentials (root only).
     * Non-root nodes don't need these.
     */
    char        router_ssid[32] = "";
    char        router_pass[64] = "";
    
    /* ── Topology Options ─────────────────────────────────────────────── */
    
    /**
     * Max children this node can have.
     * 
     * More children = more memory + CPU for routing.
     * Battery devices should set this to 0 (leaf only).
     */
    uint8_t     max_children = MESH_AP_CONNECTIONS;
    
    /**
     * Max layers (hops from root).
     * 
     * More layers = larger coverage area but more latency.
     * 6 is plenty for most homes.
     */
    uint8_t     max_layer = MESH_MAX_LAYER;
    
    /**
     * Force this node to be leaf-only (no children).
     * 
     * TRUE:  This node will never accept children.
     *        Good for battery devices — they can sleep.
     * 
     * FALSE: This node can have children and must route for them.
     *        Must stay awake. Good for wall-powered devices.
     */
    bool        leaf_only = false;
    
    /**
     * Allow root election if current root dies.
     * 
     * TRUE:  If root disappears, nodes vote for a new root.
     * FALSE: If root dies, mesh goes down until root returns.
     * 
     * For your setup (main controller = root), keep FALSE.
     * You want the controller to always be root.
     */
    bool        allow_root_election = false;
};

/* ─── Main Class ─────────────────────────────────────────────────────────── */

class EspMeshManager {
public:
    static EspMeshManager& instance();
    EspMeshManager(const EspMeshManager&) = delete;
    EspMeshManager& operator=(const EspMeshManager&) = delete;

    /* ─── Lifecycle ────────────────────────────────────────────────────── */

    /**
     * @brief Initialize and start the mesh network.
     * 
     * For root: Connects to router first, then starts mesh.
     * For non-root: Scans for mesh and connects to a parent.
     * 
     * @param config  Mesh configuration
     * @return ESP_OK on success
     */
    esp_err_t begin(const MeshConfig& config);

    /**
     * @brief Stop the mesh network.
     * 
     * Disconnects from parent/children and stops the mesh stack.
     */
    esp_err_t end();

    /**
     * @brief Check if mesh is running.
     */
    bool isRunning() const;

    /**
     * @brief Check if connected to the mesh.
     * 
     * For root: connected to router.
     * For non-root: connected to a parent.
     */
    bool isConnected() const;

    /**
     * @brief Check if this node is the root.
     */
    bool isRoot() const;

    /* ─── Sending ──────────────────────────────────────────────────────── */

    /**
     * @brief Send data to a specific mesh node.
     * 
     * The mesh routes it automatically — you don't need to know
     * the path. Just provide the destination MAC.
     * 
     * @param dest_mac  6-byte MAC of destination node
     * @param data      Data to send
     * @param len       Length of data (max MESH_MAX_PAYLOAD)
     * @return ESP_OK if queued for sending
     */
    esp_err_t sendTo(const uint8_t dest_mac[6], const uint8_t* data, size_t len);

    /**
     * @brief Send data to the root node.
     * 
     * Shortcut for sending "up" the tree to the main controller.
     * 
     * @param data  Data to send
     * @param len   Length of data
     * @return ESP_OK if queued for sending
     */
    esp_err_t sendToRoot(const uint8_t* data, size_t len);

    /**
     * @brief Broadcast data to all mesh nodes.
     * 
     * Floods the entire mesh. Use sparingly — it generates traffic
     * at every node.
     * 
     * @param data  Data to send
     * @param len   Length of data
     * @return ESP_OK if queued for sending
     */
    esp_err_t broadcast(const uint8_t* data, size_t len);

    /**
     * @brief Send data down to all children.
     * 
     * Only reaches direct children, not grandchildren.
     * Use broadcast() for full tree.
     * 
     * @param data  Data to send
     * @param len   Length of data
     * @return ESP_OK if queued for sending
     */
    esp_err_t sendToChildren(const uint8_t* data, size_t len);

    /* ─── Callbacks ────────────────────────────────────────────────────── */

    void setEventCallback(MeshEventCb cb);
    void setReceiveCallback(MeshReceiveCb cb);
    void setSendCallback(MeshSendCb cb);

    /* ─── Network Info ─────────────────────────────────────────────────── */

    /**
     * @brief Get this node's layer in the tree.
     * 
     * 1 = root
     * 2 = directly connected to root
     * 3+ = further down
     * 
     * @return Layer number, or 0 if not connected
     */
    uint8_t getLayer() const;

    /**
     * @brief Get the number of connected children.
     */
    uint8_t getChildCount() const;

    /**
     * @brief Get this node's MAC address.
     */
    esp_err_t getOwnMac(uint8_t mac[6]) const;

    /**
     * @brief Get the root node's MAC address.
     * 
     * @param mac  Output buffer (6 bytes)
     * @return ESP_OK if root is known
     */
    esp_err_t getRootMac(uint8_t mac[6]) const;

    /**
     * @brief Get the parent node's MAC address.
     * 
     * @param mac  Output buffer (6 bytes)
     * @return ESP_OK if parent exists, ESP_ERR_NOT_FOUND if root
     */
    esp_err_t getParentMac(uint8_t mac[6]) const;

    /**
     * @brief Get total node count in the mesh.
     * 
     * Only accurate at root. Other nodes may have stale data.
     */
    int getTotalNodes() const;

    /* ─── Utilities ────────────────────────────────────────────────────── */

    /**
     * @brief Format MAC address as string "AA:BB:CC:DD:EE:FF"
     */
    static void macToStr(const uint8_t mac[6], char* buf);

private:
    EspMeshManager();
    ~EspMeshManager();

    /* ─── Internal ─────────────────────────────────────────────────────── */
    static void meshEventHandler(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data);
    static void ipEventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);
    static void rxTaskFunc(void* arg);

    void handleMeshEvent(int32_t event_id, void* event_data);
    void notifyEvent(MeshEvent event, const MeshEventInfo* info = nullptr);

    /* ─── State ────────────────────────────────────────────────────────── */
    bool            _initialized;
    bool            _running;
    bool            _connected;
    bool            _is_root;
    bool            _is_root_fixed;     /* Config says we're root */
    uint8_t         _layer;
    uint8_t         _self_mac[6];

    MeshConfig      _config;
    TaskHandle_t    _rx_task;
    SemaphoreHandle_t _mutex;
    EventGroupHandle_t _event_group;

    MeshEventCb     _event_cb;
    MeshReceiveCb   _recv_cb;
    MeshSendCb      _send_cb;

    /* Event group bits */
    static constexpr uint32_t BIT_CONNECTED = BIT0;
    static constexpr uint32_t BIT_ROOT_GOT  = BIT1;
    static constexpr uint32_t BIT_STARTED   = BIT2;
};

#endif // ESP_MESH_MANAGER_H
