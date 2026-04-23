cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "../../communication/mesh"
    "../../communication/wifi"
    "../../communication/esp_now"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mesh-test)