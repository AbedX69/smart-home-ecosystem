idf_component_register(
    SRCS "ota_bulk_rx.cpp"
    INCLUDE_DIRS "."
    REQUIRES core_types message_protocol auto_pair ota esp_timer
)
