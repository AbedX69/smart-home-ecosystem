idf_component_register(
    SRCS "ota_http.cpp"
    INCLUDE_DIRS "."
    REQUIRES ota esp_http_server esp_http_client esp_timer freertos
)
