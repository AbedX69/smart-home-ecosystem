idf_component_register(
    SRCS "ota_stage.cpp"
    INCLUDE_DIRS "."
    REQUIRES core_types config_store esp_partition app_update
)
