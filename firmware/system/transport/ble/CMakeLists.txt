idf_component_register(
    SRCS "ble_manager.cpp"
         "ble_server.cpp"
         "ble_client.cpp"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash freertos
)
