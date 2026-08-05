cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "../../../system/ota" "../../../system/ota_http")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "1.3.0")
project(ota-test)
