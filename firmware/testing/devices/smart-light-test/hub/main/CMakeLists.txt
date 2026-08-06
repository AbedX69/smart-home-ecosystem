idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES smart-light gc9a01 touch encoder addressable esp_now
         message_protocol auto_pair config_store core_types device_identity ota ota_stage
)
