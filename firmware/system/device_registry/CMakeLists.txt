idf_component_register(
    SRCS "device_registry.cpp"
    INCLUDE_DIRS "."
    REQUIRES core_types freertos esp_timer esp_common esp_hw_support
)
