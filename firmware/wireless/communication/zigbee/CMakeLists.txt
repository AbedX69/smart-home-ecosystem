idf_component_register(
    SRCS "zigbee_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp-zigbee-lib esp-zboss-lib nvs_flash freertos
)
