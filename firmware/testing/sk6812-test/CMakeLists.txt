cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "../../components/addressable"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sk6812_test)
