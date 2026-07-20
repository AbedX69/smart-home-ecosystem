idf_component_register(
    SRCS "lora_sx1262.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver freertos esp_timer
)
