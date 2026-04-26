/*
 * =============================================================================
 * FILE:        esp_mesh_manager.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-03-18
 * VERSION:     1.0.0
 * =============================================================================
 */

#include "esp_mesh_manager.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include <cstdio>

static const char* TAG = "MeshMgr";

/* ─── Singleton ──────────────────────────────────────────────────────────── */

EspMeshManager& EspMeshManager::instance() {
    static EspMeshManager inst;
    return inst;
}

EspMeshManager::EspMeshManager()
    : _initialized(false)
    , _running(false)
    , _connected(false)
    , _is_root(false)
    , _is_root_fixed(false)
    , _layer(0)
    , _rx_task(nullptr)
    , _mutex(nullptr)
    , _event_group(nullptr)
    , _event_cb(nullptr)
    , _recv_cb(nullptr)
    , _send_cb(nullptr)
{
    memset(_self_mac, 0, sizeof(_self_mac));
    _config = MeshConfig{};}

EspMeshManager::~EspMeshManager() {
    end();
}

/* ─── Lifecycle ──────────────────────────────────────────────────────────── */

esp_err_t EspMeshManager::begin(const MeshConfig& config) {
    if (_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    _config = config;
    _is_root_fixed = config.is_root;

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  ESP-MESH Manager starting");
    ESP_LOGI(TAG, "  Role: %s", config.is_root ? "ROOT" : "NODE");
    if (config.is_root) {
        ESP_LOGI(TAG, "  Router: %s", config.router_ssid);
    }
    ESP_LOGI(TAG, "  Leaf only: %s", config.leaf_only ? "YES" : "NO");
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    /* ── Create synchronization primitives ─────────────────────────────── */
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    _event_group = xEventGroupCreate();
    if (!_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        vSemaphoreDelete(_mutex);
        return ESP_ERR_NO_MEM;
    }

    /* ── Networking stack assumed initialized by WiFiManager ──────────── */
    /* WiFiManager::begin() already called:
     *   - esp_netif_init()
     *   - esp_event_loop_create_default()
     *   - esp_wifi_init()
     *   - esp_wifi_start()
     * 
     * We just need to create mesh netifs and register our event handlers.
     */

    /* Create mesh-specific network interfaces */
   // ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(nullptr, nullptr));

    /* Register mesh event handlers */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        MESH_EVENT, ESP_EVENT_ANY_ID,
        meshEventHandler, this, nullptr));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        ipEventHandler, this, nullptr));

    /* Get our MAC (WiFi already started by WiFiManager) */
    esp_wifi_get_mac(WIFI_IF_STA, _self_mac);

    /* ── Initialize mesh ───────────────────────────────────────────────── */
    ESP_ERROR_CHECK(esp_mesh_init());

    /* ── Configure mesh ────────────────────────────────────────────────── */
    mesh_cfg_t mesh_cfg = {};
    
    /* Mesh ID */
    memcpy(mesh_cfg.mesh_id.addr, config.mesh_id, MESH_ID_LEN);
    
    /* Channel */
    mesh_cfg.channel = config.channel;
    
    /* Mesh AP settings (how other nodes see us) */
    mesh_cfg.mesh_ap.max_connection = config.leaf_only ? 0 : config.max_children;
    mesh_cfg.mesh_ap.nonmesh_max_connection = 0;  /* No regular WiFi clients */
memcpy(mesh_cfg.mesh_ap.password, config.mesh_pass,
           strlen(config.mesh_pass));

    /* Router settings — ALL nodes need this to find the mesh */
    if (strlen(config.router_ssid) > 0) {
        memcpy(mesh_cfg.router.ssid, config.router_ssid,
               strlen(config.router_ssid));
        mesh_cfg.router.ssid_len = strlen(config.router_ssid);
        memcpy(mesh_cfg.router.password, config.router_pass,
               strlen(config.router_pass));
    }

    ESP_ERROR_CHECK(esp_mesh_set_config(&mesh_cfg));

    /* ── Topology settings ─────────────────────────────────────────────── */
    
    /* Max layers */
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(config.max_layer));
    
    /* Root election: if disabled, only fixed root can be root */
if (!config.allow_root_election) {
    ESP_ERROR_CHECK(esp_mesh_fix_root(true));
}

if (config.is_root) {
    ESP_ERROR_CHECK(esp_mesh_set_type(MESH_ROOT));
} else if (config.leaf_only) {
    esp_mesh_set_type(MESH_LEAF);
}




    /* ── Create receive task ───────────────────────────────────────────── */
    xTaskCreate(rxTaskFunc, "mesh_rx", 4096, this, 5, &_rx_task);

    /* ── Start mesh ────────────────────────────────────────────────────── */
    ESP_ERROR_CHECK(esp_mesh_start());

    _initialized = true;
    _running = true;
    
    ESP_LOGI(TAG, "Mesh started, waiting for connection...");
    
    return ESP_OK;
}

esp_err_t EspMeshManager::end() {
    if (!_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Stopping mesh...");

    /* Stop receive task */
    if (_rx_task) {
        vTaskDelete(_rx_task);
        _rx_task = nullptr;
    }

    /* Stop mesh */
    esp_mesh_stop();
    esp_mesh_deinit();

    /* Clean up */
    if (_event_group) {
        vEventGroupDelete(_event_group);
        _event_group = nullptr;
    }
    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }

    _initialized = false;
    _running = false;
    _connected = false;

    return ESP_OK;
}

bool EspMeshManager::isRunning() const { return _running; }
bool EspMeshManager::isConnected() const { return _connected; }
bool EspMeshManager::isRoot() const { return _is_root; }

/* ─── Sending ────────────────────────────────────────────────────────────── */

esp_err_t EspMeshManager::sendTo(const uint8_t dest_mac[6], 
                                   const uint8_t* data, size_t len) {
    if (!_connected) {
        ESP_LOGW(TAG, "Not connected, cannot send");
        return ESP_ERR_INVALID_STATE;
    }
    if (len > MESH_MAX_PAYLOAD) {
        ESP_LOGE(TAG, "Payload too large: %zu > %d", len, MESH_MAX_PAYLOAD);
        return ESP_ERR_INVALID_SIZE;
    }

    mesh_addr_t dest;
    memcpy(dest.addr, dest_mac, 6);

    mesh_data_t mesh_data;
    mesh_data.data = (uint8_t*)data;
    mesh_data.size = len;
    mesh_data.proto = MESH_PROTO_BIN;
    mesh_data.tos = MESH_TOS_P2P;  /* Point-to-point, reliable */

    esp_err_t err = esp_mesh_send(&dest, &mesh_data, 
                                   MESH_DATA_P2P,   /* P2P routing */
                                   nullptr,         /* No option */
                                   0);              /* No timeout (block) */

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Send failed: %s", esp_err_to_name(err));
    }

    if (_send_cb) {
        _send_cb(dest_mac, err == ESP_OK);
    }

    return err;
}

esp_err_t EspMeshManager::sendToRoot(const uint8_t* data, size_t len) {
    if (!_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    if (_is_root) {
        /* We are root, can't send to ourselves */
        ESP_LOGW(TAG, "We are root, cannot sendToRoot");
        return ESP_ERR_INVALID_STATE;
    }
    if (len > MESH_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    mesh_data_t mesh_data;
    mesh_data.data = (uint8_t*)data;
    mesh_data.size = len;
    mesh_data.proto = MESH_PROTO_BIN;
    mesh_data.tos = MESH_TOS_P2P;

    /* nullptr destination = send to root */
    esp_err_t err = esp_mesh_send(nullptr, &mesh_data,
                                   MESH_DATA_TODS,  /* To DS (root direction) */
                                   nullptr, 0);

    return err;
}

esp_err_t EspMeshManager::broadcast(const uint8_t* data, size_t len) {
    if (!_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > MESH_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    mesh_data_t mesh_data;
    mesh_data.data = (uint8_t*)data;
    mesh_data.size = len;
    mesh_data.proto = MESH_PROTO_BIN;
    mesh_data.tos = MESH_TOS_P2P;

    /* Group address for broadcast */
    mesh_addr_t group;
    memset(group.addr, 0xFF, 6);  /* Broadcast address */

    esp_err_t err = esp_mesh_send(&group, &mesh_data,
                                   MESH_DATA_GROUP,  /* Group/broadcast */
                                   nullptr, 0);

    return err;
}

esp_err_t EspMeshManager::sendToChildren(const uint8_t* data, size_t len) {
    if (!_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > MESH_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    mesh_data_t mesh_data;
    mesh_data.data = (uint8_t*)data;
    mesh_data.size = len;
    mesh_data.proto = MESH_PROTO_BIN;
    mesh_data.tos = MESH_TOS_P2P;

    esp_err_t err = esp_mesh_send(nullptr, &mesh_data,
                                   MESH_DATA_FROMDS,  /* From DS (down to children) */
                                   nullptr, 0);

    return err;
}

/* ─── Callbacks ──────────────────────────────────────────────────────────── */

void EspMeshManager::setEventCallback(MeshEventCb cb) { _event_cb = cb; }
void EspMeshManager::setReceiveCallback(MeshReceiveCb cb) { _recv_cb = cb; }
void EspMeshManager::setSendCallback(MeshSendCb cb) { _send_cb = cb; }

/* ─── Network Info ───────────────────────────────────────────────────────── */

uint8_t EspMeshManager::getLayer() const { return _layer; }

uint8_t EspMeshManager::getChildCount() const {
    int count = esp_mesh_get_routing_table_size();    /* Routing table includes us + all descendants */
    /* Child count is harder to get directly; this is approximate */
    mesh_addr_t children[MESH_AP_CONNECTIONS];
    int child_count = 0;
    esp_mesh_get_subnet_nodes_list(nullptr, children, MESH_AP_CONNECTIONS);
    /* Actually, let's use the simpler approach */
    return count > 0 ? count - 1 : 0;  /* Subtract ourselves */
}

esp_err_t EspMeshManager::getOwnMac(uint8_t mac[6]) const {
    memcpy(mac, _self_mac, 6);
    return ESP_OK;
}

esp_err_t EspMeshManager::getRootMac(uint8_t mac[6]) const {
    if (!_connected) return ESP_ERR_INVALID_STATE;
    
    mesh_addr_t root;
    esp_mesh_get_parent_bssid(&root);
    
    /* If we're root, return our own MAC */
    if (_is_root) {
        memcpy(mac, _self_mac, 6);
    } else {
        /* Walk up to root - for simplicity, return parent */
        /* Full implementation would traverse to root */
        memcpy(mac, root.addr, 6);
    }
    return ESP_OK;
}

esp_err_t EspMeshManager::getParentMac(uint8_t mac[6]) const {
    if (!_connected) return ESP_ERR_INVALID_STATE;
    if (_is_root) return ESP_ERR_NOT_FOUND;  /* Root has no parent */
    
    mesh_addr_t parent;
    esp_mesh_get_parent_bssid(&parent);
    memcpy(mac, parent.addr, 6);
    return ESP_OK;
}

int EspMeshManager::getTotalNodes() const {
    return esp_mesh_get_routing_table_size();
}

/* ─── Utilities ──────────────────────────────────────────────────────────── */

void EspMeshManager::macToStr(const uint8_t mac[6], char* buf) {
    snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ─── Event Handlers ─────────────────────────────────────────────────────── */

void EspMeshManager::meshEventHandler(void* arg, esp_event_base_t event_base,
                                        int32_t event_id, void* event_data) {
    EspMeshManager* self = static_cast<EspMeshManager*>(arg);
    self->handleMeshEvent(event_id, event_data);
}

void EspMeshManager::ipEventHandler(void* arg, esp_event_base_t event_base,
                                      int32_t event_id, void* event_data) {
    EspMeshManager* self = static_cast<EspMeshManager*>(arg);
    
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Root got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void EspMeshManager::handleMeshEvent(int32_t event_id, void* event_data) {
    MeshEventInfo info = {};
    memcpy(info.mac, _self_mac, 6);

    switch (event_id) {
        
        case MESH_EVENT_STARTED: {
            ESP_LOGI(TAG, "Mesh started");
            xEventGroupSetBits(_event_group, BIT_STARTED);
            notifyEvent(MeshEvent::STARTED);
            break;
        }

        case MESH_EVENT_STOPPED: {
            ESP_LOGI(TAG, "Mesh stopped");
            _running = false;
            _connected = false;
            notifyEvent(MeshEvent::STOPPED);
            break;
        }

        case MESH_EVENT_PARENT_CONNECTED: {
            mesh_event_connected_t* connected = 
                (mesh_event_connected_t*)event_data;
            
            _connected = true;
            _layer = esp_mesh_get_layer();
            _is_root = esp_mesh_is_root();

            char mac_str[18];
            macToStr(connected->connected.bssid, mac_str);
            ESP_LOGI(TAG, "Connected to parent: %s (layer %d)", 
                     mac_str, _layer);

            xEventGroupSetBits(_event_group, BIT_CONNECTED);

            /* If we're root and connected, we got router connection */
            if (_is_root) {
                esp_mesh_post_toDS_state(true);
                xEventGroupSetBits(_event_group, BIT_ROOT_GOT);
            }

            info.layer = _layer;
            info.is_root = _is_root;
            info.has_parent = !_is_root;
            memcpy(info.mac, connected->connected.bssid, 6);
            
            notifyEvent(MeshEvent::CONNECTED, &info);
            break;
        }

        case MESH_EVENT_PARENT_DISCONNECTED: {
            mesh_event_disconnected_t* disc = 
                (mesh_event_disconnected_t*)event_data;
            
            ESP_LOGW(TAG, "Disconnected from parent (reason: %d)", 
                     disc->reason);
            
            _connected = false;
            xEventGroupClearBits(_event_group, BIT_CONNECTED);
            
            notifyEvent(MeshEvent::DISCONNECTED);
            break;
        }

        case MESH_EVENT_CHILD_CONNECTED: {
            mesh_event_child_connected_t* child = 
                (mesh_event_child_connected_t*)event_data;
            
            char mac_str[18];
            macToStr(child->mac, mac_str);
            ESP_LOGI(TAG, "Child connected: %s", mac_str);
            
            memcpy(info.mac, child->mac, 6);
            info.child_count = getChildCount();
            
            notifyEvent(MeshEvent::CHILD_CONNECTED, &info);
            break;
        }

        case MESH_EVENT_CHILD_DISCONNECTED: {
            mesh_event_child_disconnected_t* child = 
                (mesh_event_child_disconnected_t*)event_data;
            
            char mac_str[18];
            macToStr(child->mac, mac_str);
            ESP_LOGW(TAG, "Child disconnected: %s", mac_str);
            
            memcpy(info.mac, child->mac, 6);
            info.child_count = getChildCount();
            
            notifyEvent(MeshEvent::CHILD_DISCONNECTED, &info);
            break;
        }

  /*      case MESH_EVENT_ROOT_GOT_IP: {
            mesh_event_root_got_ip_t* got_ip = 
                (mesh_event_root_got_ip_t*)event_data;
            
            ESP_LOGI(TAG, "Root got IP: " IPSTR, 
                     IP2STR(&got_ip->ip_info.ip));
            
            xEventGroupSetBits(_event_group, BIT_ROOT_GOT);
            break;
        }

        case MESH_EVENT_ROOT_LOST_IP: {
            ESP_LOGW(TAG, "Root lost IP");
            xEventGroupClearBits(_event_group, BIT_ROOT_GOT);
            break;
        }
*/
        case MESH_EVENT_LAYER_CHANGE: {
            mesh_event_layer_change_t* layer_change = 
                (mesh_event_layer_change_t*)event_data;
            
            _layer = layer_change->new_layer;
            ESP_LOGI(TAG, "Layer changed: %d", _layer);
            
            info.layer = _layer;
            notifyEvent(MeshEvent::LAYER_CHANGED, &info);
            break;
        }

        case MESH_EVENT_ROOT_ADDRESS: {
            mesh_event_root_address_t* root_addr = 
                (mesh_event_root_address_t*)event_data;
            
            char mac_str[18];
            macToStr(root_addr->addr, mac_str);
            ESP_LOGI(TAG, "Root address: %s", mac_str);
            break;
        }

        case MESH_EVENT_TODS_STATE: {
            mesh_event_toDS_state_t* toDS = 
                (mesh_event_toDS_state_t*)event_data;
            ESP_LOGI(TAG, "toDS state: %s", 
                     *toDS == MESH_TODS_REACHABLE ? "reachable" : "unreachable");
            break;
        }

        default:
            ESP_LOGD(TAG, "Mesh event: %ld", event_id);
            break;
    }
}

void EspMeshManager::notifyEvent(MeshEvent event, const MeshEventInfo* info) {
    if (_event_cb) {
        _event_cb(event, info);
    }
}

/* ─── Receive Task ───────────────────────────────────────────────────────── */

void EspMeshManager::rxTaskFunc(void* arg) {
    EspMeshManager* self = static_cast<EspMeshManager*>(arg);
    
    /* Buffer for receiving */
    uint8_t rx_buf[MESH_MAX_PAYLOAD + sizeof(mesh_addr_t)];
    mesh_addr_t from;
    mesh_data_t data;
    int flag = 0;

    data.data = rx_buf;
    data.size = sizeof(rx_buf);

    ESP_LOGI(TAG, "Receive task started, waiting for mesh...");

    /* Wait for mesh to actually start before receiving */
    while (!self->_connected) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "Mesh connected, receiving...");
    while (true) {
        data.size = sizeof(rx_buf);
        
        esp_err_t err = esp_mesh_recv(&from, &data, portMAX_DELAY,
                                       &flag, nullptr, 0);
        
        if (err != ESP_OK) {
            if (err == ESP_ERR_MESH_NOT_START) {
                /* Mesh stopped, exit task */
                break;
            }
            ESP_LOGW(TAG, "Receive error: %s", esp_err_to_name(err));
            continue;
        }

        /* Got a message */
        bool from_root = (flag & MESH_DATA_FROMDS);
        
        char mac_str[18];
        macToStr(from.addr, mac_str);
        ESP_LOGD(TAG, "Received %d bytes from %s (root dir: %d)",
                 data.size, mac_str, from_root);

        /* Call user callback */
        if (self->_recv_cb) {
            self->_recv_cb(from.addr, data.data, data.size, from_root);
        }
    }

    ESP_LOGI(TAG, "Receive task ended");
    vTaskDelete(nullptr);
}
