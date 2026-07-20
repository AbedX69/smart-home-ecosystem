cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    "../../../components/button"
    "../../../components/buzzer"
    "../../../devices/modules/garage-controller"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_garage_controller)