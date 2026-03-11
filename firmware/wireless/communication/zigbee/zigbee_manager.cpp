/*
 * =============================================================================
 * FILE:        zigbee_manager.cpp
 * AUTHOR:      AbedX69
 * CREATED:     2026-02-17
 * VERSION:     1.0.0
 * =============================================================================
 * 
 * IMPORTANT ARCHITECTURAL NOTES:
 * 
 * 1. The Zigbee stack runs its own main loop in a dedicated FreeRTOS task.
 *    All Zigbee API calls must be made from that task or use the scheduler:
 *      esp_zb_scheduler_alarm()
 * 
 * 2. The ZBOSS stack is NOT thread-safe. Never call esp_zb_* from other
 *    tasks unless using the lock: esp_zb_lock_acquire() / release().
 * 
 * 3. The esp-zboss-lib and esp-zigbee-lib are pulled from the ESP Component
 *    Registry at build time via idf_component.yml — they are NOT local files.
 * 
 * 4. The signal handler (esp_zb_app_signal_handler) is the main way the
 *    stack communicates events to the application. It must be implemented
 *    as an extern "C" function with that exact name.
 * 
 * =============================================================================
 */

#include "zigbee_manager.h"
#include "nvs_flash.h"

static const char* TAG = "ZigbeeMgr";

/* =============================================================================
 * SINGLETON
 * ========================================================================== */

ZigbeeManager& ZigbeeManager::instance() {
    static ZigbeeManager inst;
    return inst;
}

ZigbeeManager::ZigbeeManager()
    : _initialized(false)
    , _role(ZBRole::END_DEVICE)
    , _device_type(ZBDeviceType::ON_OFF_LIGHT)
    , _joined(false)
    , _onoff_cb(nullptr)
    , _level_cb(nullptr)
    , _network_cb(nullptr)
{}

ZigbeeManager::~ZigbeeManager() {}

/* =============================================================================
 * SIGNAL HANDLER (extern "C" — required by ZBOSS stack)
 * =============================================================================
 * 
 * The Zigbee stack calls this function when events happen:
 *   - Stack init complete
 *   - Network formed (coordinator)
 *   - Network joined (router/end device)
 *   - Steering failed
 *   - Device joined our network
 *   - Permit join status changed
 * ========================================================================== */

extern "C" void esp_zb_app_signal_handler(esp_zb_app_signal_t* signal_struct) {
    ZigbeeManager::appSignalHandler(signal_struct);
}

void ZigbeeManager::appSignalHandler(esp_zb_app_signal_t* signal_struct) {
    ZigbeeManager& zb = instance();

    uint32_t* p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)*p_sg_p;

    ZBEventInfo info = {};

    switch (sig_type) {

        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
            /* Stack init done, start commissioning */
            ESP_LOGI(TAG, "Zigbee stack initialized");
            if (zb._network_cb) zb._network_cb(ZBEvent::STACK_READY, nullptr);
            esp_zb_bdb_start_top_level_commissioning(
                (zb._role == ZBRole::COORDINATOR) ?
                ESP_ZB_BDB_MODE_INITIALIZATION : ESP_ZB_BDB_MODE_INITIALIZATION);
            break;

        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT: {
            if (err_status == ESP_OK) {
                ESP_LOGI(TAG, "Device started up in %sfactory-reset mode",
                         esp_zb_bdb_is_factory_new() ? "" : "non-");
                if (esp_zb_bdb_is_factory_new()) {
                    /* First boot: start network formation (ZC) or steering (ZR/ZED) */
                    if (zb._role == ZBRole::COORDINATOR) {
                        ESP_LOGI(TAG, "Starting network formation...");
                        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
                    } else {
                        ESP_LOGI(TAG, "Starting network steering...");
                        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
                    }
                } else {
                    /* Not first boot — already commissioned */
                    ESP_LOGI(TAG, "Device rebooted, already commissioned");
                    zb._joined = true;
                    info.channel = esp_zb_get_current_channel();
                    info.pan_id = esp_zb_get_pan_id();
                    info.short_addr = esp_zb_get_short_address();
                    if (zb._network_cb) zb._network_cb(ZBEvent::NETWORK_JOINED, &info);
                }
            } else {
                ESP_LOGW(TAG, "Initialization failed: %s, retrying...",
                         esp_err_to_name(err_status));
                esp_zb_scheduler_alarm((esp_zb_callback_t)bdbCommissionCb,
                                       ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
            }
            break;
        }

        case ESP_ZB_BDB_SIGNAL_FORMATION: {
            if (err_status == ESP_OK) {
                esp_zb_ieee_addr_t ext_pan;
                esp_zb_get_extended_pan_id(ext_pan);

                info.pan_id = esp_zb_get_pan_id();
                info.channel = esp_zb_get_current_channel();
                info.short_addr = esp_zb_get_short_address();
                memcpy(info.ext_pan_id, ext_pan, 8);

                ESP_LOGI(TAG, "Network formed! PAN:0x%04X CH:%d Addr:0x%04X",
                         info.pan_id, info.channel, info.short_addr);

                zb._joined = true;
                if (zb._network_cb) zb._network_cb(ZBEvent::NETWORK_FORMED, &info);

                /* Open for joining */
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGW(TAG, "Formation failed: %s, retrying...",
                         esp_err_to_name(err_status));
                esp_zb_scheduler_alarm((esp_zb_callback_t)bdbCommissionCb,
                                       ESP_ZB_BDB_MODE_NETWORK_FORMATION, 1000);
            }
            break;
        }

        case ESP_ZB_BDB_SIGNAL_STEERING: {
            if (err_status == ESP_OK) {
                esp_zb_ieee_addr_t ext_pan;
                esp_zb_get_extended_pan_id(ext_pan);

                info.pan_id = esp_zb_get_pan_id();
                info.channel = esp_zb_get_current_channel();
                info.short_addr = esp_zb_get_short_address();
                memcpy(info.ext_pan_id, ext_pan, 8);

                ESP_LOGI(TAG, "Joined network! PAN:0x%04X CH:%d Addr:0x%04X",
                         info.pan_id, info.channel, info.short_addr);

                zb._joined = true;
                if (zb._network_cb) zb._network_cb(ZBEvent::NETWORK_JOINED, &info);
            } else {
                ESP_LOGW(TAG, "Steering failed: %s, retrying in 1s...",
                         esp_err_to_name(err_status));
                if (zb._network_cb) zb._network_cb(ZBEvent::NETWORK_FAILED, nullptr);
                esp_zb_scheduler_alarm((esp_zb_callback_t)bdbCommissionCb,
                                       ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
            }
            break;
        }

        case ESP_ZB_ZDO_SIGNAL_LEAVE: {
            ESP_LOGW(TAG, "Left the network");
            zb._joined = false;
            if (zb._network_cb) zb._network_cb(ZBEvent::NETWORK_LEFT, nullptr);
            break;
        }

        case ESP_ZB_COMMON_SIGNAL_CAN_SLEEP:
            /* End device can enter sleep — we don't implement deep sleep here */
            break;

        case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
            uint8_t* duration = (uint8_t*)esp_zb_app_signal_get_params(p_sg_p);
            if (duration) {
                info.permit_join_open = (*duration > 0);
                info.permit_duration = *duration;
                ESP_LOGI(TAG, "Permit join: %s (%ds)",
                         info.permit_join_open ? "OPEN" : "CLOSED",
                         info.permit_duration);
                if (zb._network_cb) zb._network_cb(ZBEvent::PERMIT_JOIN, &info);
            }
            break;
        }

        default:
            ESP_LOGD(TAG, "Signal: %s (0x%x) status: %s",
                     esp_zb_zdo_signal_to_string(sig_type), sig_type,
                     esp_err_to_name(err_status));
            break;
    }
}

void ZigbeeManager::bdbCommissionCb(uint8_t mode_mask) {
    esp_zb_bdb_start_top_level_commissioning(mode_mask);
}

/* =============================================================================
 * ACTION HANDLER
 * =============================================================================
 * 
 * Called when a remote device changes an attribute on this device.
 * For example, Zigbee2MQTT sends "turn on" → we get SET_ATTR_VALUE for
 * the on_off cluster with value=1.
 * ========================================================================== */

esp_err_t ZigbeeManager::actionHandler(esp_zb_core_action_callback_id_t callback_id,
                                         const void* message) {
    esp_err_t ret = ESP_OK;

    switch (callback_id) {
        case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
            ret = handleSetAttrValue(
                (const esp_zb_zcl_set_attr_value_message_t*)message);
            break;

        case ESP_ZB_CORE_REPORT_ATTR_CB_ID:
            ESP_LOGD(TAG, "Report attribute callback");
            break;

        default:
            ESP_LOGD(TAG, "Action callback: 0x%x", callback_id);
            break;
    }

    return ret;
}

esp_err_t ZigbeeManager::handleSetAttrValue(
    const esp_zb_zcl_set_attr_value_message_t* msg) {

    if (!msg || msg->info.status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "Set attr value: bad status");
        return ESP_ERR_INVALID_ARG;
    }

    ZigbeeManager& zb = instance();

    ESP_LOGI(TAG, "Attr changed: ep=%d cluster=0x%04X attr=0x%04X",
             msg->info.dst_endpoint, msg->info.cluster, msg->attribute.id);

    /* ── On/Off Cluster ────────────────────────────────────────────── */
    if (msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
        if (msg->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID &&
            msg->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
            bool on = *(bool*)msg->attribute.data.value;
            ESP_LOGI(TAG, "On/Off → %s", on ? "ON" : "OFF");
            if (zb._onoff_cb) zb._onoff_cb(on);
        }
    }

    /* ── Level Control Cluster ─────────────────────────────────────── */
    else if (msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL) {
        if (msg->attribute.id == ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID &&
            msg->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U8) {
            uint8_t level = *(uint8_t*)msg->attribute.data.value;
            ESP_LOGI(TAG, "Level → %d (%.0f%%)", level, level * 100.0f / 255);
            if (zb._level_cb) zb._level_cb(level);
        }
    }

    return ESP_OK;
}

/* =============================================================================
 * ENDPOINT CREATION
 * =============================================================================
 * 
 * Each device type creates a different set of clusters.
 * The ESP Zigbee SDK provides helper functions that create complete
 * HA-standard endpoints with all required clusters and attributes.
 * ========================================================================== */

void ZigbeeManager::createOnOffLightEndpoint() {
    esp_zb_on_off_light_cfg_t light_cfg = ESP_ZB_DEFAULT_ON_OFF_LIGHT_CONFIG();
    esp_zb_ep_list_t* ep_list = esp_zb_on_off_light_ep_create(
        _config.endpoint, &light_cfg);

    esp_zb_device_register(ep_list);
    ESP_LOGI(TAG, "Registered On/Off Light on endpoint %d", _config.endpoint);
}

void ZigbeeManager::createDimmableLightEndpoint() {
    esp_zb_color_dimmable_light_cfg_t light_cfg =
        ESP_ZB_DEFAULT_COLOR_DIMMABLE_LIGHT_CONFIG();
    esp_zb_ep_list_t* ep_list = esp_zb_color_dimmable_light_ep_create(
        _config.endpoint, &light_cfg);

    esp_zb_device_register(ep_list);
    ESP_LOGI(TAG, "Registered Dimmable Light on endpoint %d", _config.endpoint);
}

void ZigbeeManager::createTemperatureSensorEndpoint() {
    esp_zb_temperature_sensor_cfg_t sensor_cfg =
        ESP_ZB_DEFAULT_TEMPERATURE_SENSOR_CONFIG();
    esp_zb_ep_list_t* ep_list = esp_zb_temperature_sensor_ep_create(
        _config.endpoint, &sensor_cfg);

    esp_zb_device_register(ep_list);
    ESP_LOGI(TAG, "Registered Temperature Sensor on endpoint %d", _config.endpoint);
}

/* =============================================================================
 * ZIGBEE TASK
 * =============================================================================
 * 
 * The Zigbee stack requires its own dedicated task that runs the
 * main loop forever. This is fundamentally different from WiFi/BLE
 * where the stack runs in the background.
 * 
 * The task does:
 *   1. Configure the stack (role, radio, host)
 *   2. Init the stack
 *   3. Create endpoint(s)
 *   4. Register handlers
 *   5. Start the stack
 *   6. Run main loop iteration forever
 * ========================================================================== */

void ZigbeeManager::zbTask(void* pvParameters) {
    ZigbeeManager& zb = instance();

    /* ── Configure radio and host ──────────────────────────────────── */
    esp_zb_platform_config_t platform_config = {};
    platform_config.radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG();
    platform_config.host_config = ESP_ZB_DEFAULT_HOST_CONFIG();
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_config));

    /* ── Configure stack role ──────────────────────────────────────── */
    esp_zb_cfg_t zb_cfg = {};
    switch (zb._role) {
        case ZBRole::COORDINATOR:
            zb_cfg.esp_zb_role = ESP_ZB_COORDINATOR;
            zb_cfg.install_code_policy = false;
            zb_cfg.nwk_cfg.zczr_cfg.max_children = 10;
            break;
        case ZBRole::ROUTER:
            zb_cfg.esp_zb_role = ESP_ZB_ROUTER;
            zb_cfg.install_code_policy = false;
            zb_cfg.nwk_cfg.zczr_cfg.max_children = 10;
            break;
        case ZBRole::END_DEVICE:
            zb_cfg.esp_zb_role = ESP_ZB_END_DEVICE;
            zb_cfg.install_code_policy = false;
            zb_cfg.nwk_cfg.zed_cfg.ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN;
            zb_cfg.nwk_cfg.zed_cfg.keep_alive = 3000;  // ms
            break;
    }

    /* ── Optionally erase NVRAM for factory reset ──────────────────── */
    if (zb._config.erase_nvram) {
        esp_zb_nvram_erase_at_start(true);
    }

    /* ── Init stack ────────────────────────────────────────────────── */
    esp_zb_init(&zb_cfg);

    /* ── Create device endpoint ────────────────────────────────────── */
    switch (zb._device_type) {
        case ZBDeviceType::ON_OFF_LIGHT:
            zb.createOnOffLightEndpoint();
            break;
        case ZBDeviceType::DIMMABLE_LIGHT:
            zb.createDimmableLightEndpoint();
            break;
        case ZBDeviceType::TEMPERATURE_SENSOR:
            zb.createTemperatureSensorEndpoint();
            break;
    }

    /* ── Register action handler ───────────────────────────────────── */
    esp_zb_core_action_handler_register(actionHandler);

    /* ── Set channel mask ──────────────────────────────────────────── */
    esp_zb_set_primary_network_channel_set(zb._config.channel_mask);

    /* ── Start stack ───────────────────────────────────────────────── */
    ESP_ERROR_CHECK(esp_zb_start(false));

    ESP_LOGI(TAG, "Zigbee stack started, entering main loop");

    /* ── Main loop (runs forever) ──────────────────────────────────── */
    esp_zb_stack_main_loop();

    /* Never reached */
    vTaskDelete(nullptr);
}

/* =============================================================================
 * LIFECYCLE
 * ========================================================================== */

esp_err_t ZigbeeManager::begin(ZBRole role, ZBDeviceType device_type,
                                 const ZBConfig& config) {
    if (_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    _role = role;
    _device_type = device_type;
    _config = config;

    const char* role_str = (role == ZBRole::COORDINATOR) ? "Coordinator" :
                            (role == ZBRole::ROUTER) ? "Router" : "End Device";
    const char* type_str = (device_type == ZBDeviceType::ON_OFF_LIGHT) ? "On/Off Light" :
                            (device_type == ZBDeviceType::DIMMABLE_LIGHT) ? "Dimmable Light" :
                            "Temperature Sensor";

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  Zigbee Manager starting");
    ESP_LOGI(TAG, "  Role:   %s", role_str);
    ESP_LOGI(TAG, "  Device: %s", type_str);
    ESP_LOGI(TAG, "  EP:     %d", _config.endpoint);
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    /* Create the Zigbee task
     * Stack size 4096 is the minimum. The ZBOSS stack needs RAM. */
    xTaskCreate(zbTask, "zigbee_main", 4096, nullptr, 5, nullptr);

    _initialized = true;
    return ESP_OK;
}

void ZigbeeManager::factoryReset() {
    ESP_LOGW(TAG, "Factory reset: erasing Zigbee NVRAM...");
    esp_zb_factory_reset();
}

esp_err_t ZigbeeManager::permitJoin(uint8_t duration_s) {
    if (!_joined) return ESP_ERR_INVALID_STATE;
    if (_role == ZBRole::END_DEVICE) {
        ESP_LOGW(TAG, "End devices cannot open permit join");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (duration_s > 0) {
        return esp_zb_bdb_open_network(duration_s);
    } else {
        return esp_zb_bdb_close_network();
    }
}

bool ZigbeeManager::isJoined() const { return _joined; }

/* =============================================================================
 * TEMPERATURE SENSOR
 * =============================================================================
 * 
 * ZCL temperature is stored as int16 in units of 0.01°C.
 * So 23.50°C = 2350 in the attribute.
 * 
 * We update the attribute, then the reporting mechanism
 * sends it to bound devices automatically (if configured).
 * For immediate push, we use esp_zb_zcl_set_attribute_val().
 * ========================================================================== */

esp_err_t ZigbeeManager::reportTemperature(float temp_celsius) {
    if (_device_type != ZBDeviceType::TEMPERATURE_SENSOR) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!_joined) return ESP_ERR_INVALID_STATE;

    /* Convert to ZCL format: int16 in units of 0.01°C */
    int16_t temp_zcl = (int16_t)(temp_celsius * 100);

    ESP_LOGI(TAG, "Reporting temperature: %.2f°C (ZCL: %d)", temp_celsius, temp_zcl);

    /* Update the attribute — must be called from Zigbee task or with lock */
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(
        _config.endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
        &temp_zcl,
        false);
    esp_zb_lock_release();

    return ESP_OK;
}

/* =============================================================================
 * CALLBACKS & GETTERS
 * ========================================================================== */

void ZigbeeManager::setOnOffCallback(ZBOnOffCb cb) { _onoff_cb = cb; }
void ZigbeeManager::setLevelCallback(ZBLevelCb cb) { _level_cb = cb; }
void ZigbeeManager::setNetworkCallback(ZBNetworkCb cb) { _network_cb = cb; }

uint16_t ZigbeeManager::getShortAddr() const {
    return esp_zb_get_short_address();
}

uint8_t ZigbeeManager::getChannel() const {
    return esp_zb_get_current_channel();
}

uint16_t ZigbeeManager::getPanId() const {
    return esp_zb_get_pan_id();
}
