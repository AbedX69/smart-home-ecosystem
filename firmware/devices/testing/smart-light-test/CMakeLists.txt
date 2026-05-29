cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "../../../components/addressable"
    "../../../components/display/gc9a01"
    "../../../components/display/shared"
    "../../../components/touch"
    "../../../components/encoder"
    "../../modules/smart-light"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_smart_light)