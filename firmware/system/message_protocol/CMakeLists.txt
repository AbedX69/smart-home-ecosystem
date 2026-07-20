idf_component_register(
    SRCS "message_protocol.cpp"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_common esp_hw_support
)
