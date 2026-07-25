idf_component_register(
    SRCS        "device_identity.cpp"
    INCLUDE_DIRS "."
    REQUIRES    core_types
                config_store
                esp_hw_support
)
