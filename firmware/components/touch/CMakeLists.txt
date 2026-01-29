# ==============================================================================
# CMAKE BUILD CONFIGURATION FOR TOUCH COMPONENT
# ==============================================================================
#
# @file CMakeLists.txt
# @brief Build configuration for the capacitive touch ESP-IDF component.
#
# ==============================================================================

idf_component_register(
    # Source files to compile
    SRCS "touch.cpp"
    
    # Where to find header files
    INCLUDE_DIRS "."
    
    # Dependencies:
    #   - driver: GPIO functions
    #   - esp_timer: Timing for duration tracking
    REQUIRES driver esp_timer
)
