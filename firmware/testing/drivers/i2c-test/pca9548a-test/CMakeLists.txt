cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    "../../../../components"
    "../../../../components/i2c"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pca9548a-test)
