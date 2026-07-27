idf_component_register(
    SRCS "auto_pair.cpp"
    INCLUDE_DIRS "."
    REQUIRES core_types device_identity message_protocol config_store
             freertos esp_timer esp_common esp_hw_support
)
