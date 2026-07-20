idf_component_register(
    SRCS "config_store.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_common
)
