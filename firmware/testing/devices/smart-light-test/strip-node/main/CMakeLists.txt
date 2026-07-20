idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES smart-light gc9a01 touch encoder addressable esp_now
)
