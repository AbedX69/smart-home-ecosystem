cmake_minimum_required(VERSION 3.16)

# Pull in components from the firmware tree.
# Adjust paths if you move this project.
set(EXTRA_COMPONENT_DIRS
    "../../../components"
    "../../../components/display"
    "../../modules/smart_light"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_smart_light)
