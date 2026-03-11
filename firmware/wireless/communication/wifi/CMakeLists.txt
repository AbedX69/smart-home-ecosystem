idf_component_register(
    SRCS "wifi_manager.cpp"
         "wifi_http_server.cpp"
         "wifi_http_client.cpp"
         "wifi_services.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server esp_http_client
             mdns esp_https_ota app_update freertos lwip
)
