idf_component_register(
    SRCS "garage_door_device.cpp"
    INCLUDE_DIRS "."
    REQUIRES button driver esp_timer
)