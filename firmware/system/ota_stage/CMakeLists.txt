idf_component_register(
    SRCS "ota_stage.cpp"
    INCLUDE_DIRS "."
    REQUIRES core_types config_store bootloader_support esp_app_format esp_partition app_update
)
