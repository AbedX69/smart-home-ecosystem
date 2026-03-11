idf_component_register(
    SRCS "esp_now_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event nvs_flash esp_netif freertos
)
