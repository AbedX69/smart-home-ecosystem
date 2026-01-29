# ==============================================================================
# CMAKE BUILD CONFIGURATION FOR BUTTON COMPONENT
# ==============================================================================
#
# @file CMakeLists.txt
# @brief Build configuration for the button driver ESP-IDF component.
#
# ==============================================================================

idf_component_register(
    # Source files to compile
    SRCS "button.cpp"
    
    # Where to find header files
    INCLUDE_DIRS "."
    
    # Dependencies:
    #   - driver: GPIO functions
    #   - esp_timer: Timing for debounce
    REQUIRES driver esp_timer
)
