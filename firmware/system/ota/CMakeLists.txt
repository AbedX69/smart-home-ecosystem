idf_component_register(
    SRCS "ota_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES app_update esp_app_format esp_partition esp_timer freertos
)
