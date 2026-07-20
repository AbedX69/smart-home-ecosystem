idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES button buzzer ssd1306 max98357 pca9548a encoder mosfet_driver pwm_dimmer relay touch vibration 
)