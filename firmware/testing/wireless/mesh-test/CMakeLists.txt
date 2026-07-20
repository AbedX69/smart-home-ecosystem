cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    "../../../wireless/mesh"
    "../../../wireless/wifi"
    "../../../wireless/esp_now"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mesh-test)