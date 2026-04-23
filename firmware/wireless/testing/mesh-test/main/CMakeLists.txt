idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES mesh esp_wifi esp_netif esp_event nvs_flash
)