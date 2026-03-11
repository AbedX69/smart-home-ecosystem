idf_component_register(
    SRCS "ota_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES app_update esp_http_server esp_http_client esp_partition
             nvs_flash freertos esp_timer
)
