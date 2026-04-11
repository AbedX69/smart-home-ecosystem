idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES touch encoder ssd1306 gc9a01 pca9548a
)