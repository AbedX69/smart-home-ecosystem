# =============================================================================
# ESP-MESH Manager + Hybrid Transport Components
# =============================================================================
#
# Place this folder in your components/ directory.
#
# REQUIRES WiFiManager to be initialized first!
# Call WiFiManager::begin() before EspMeshManager::begin()
#
# =============================================================================

idf_component_register(
    SRCS
        "esp_mesh_manager.cpp"
        "hybrid_transport.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        freertos
        esp_timer
        log
    PRIV_REQUIRES
        wifi
        esp_now
)
