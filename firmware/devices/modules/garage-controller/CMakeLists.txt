idf_component_register(
    SRCS
        "garage_door_device.cpp"
        "garage_door_remote.cpp"
    INCLUDE_DIRS "."
    REQUIRES relay button
)
