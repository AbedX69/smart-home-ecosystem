cmake_minimum_required(VERSION 3.16.0)
set(EXTRA_COMPONENT_DIRS
    "../../../components/audio/max98357"
    "../../../components/button"
    "../../../components/buzzer"
    "../../../components/display/ssd1306"
    "../../../components/encoder"
    "../../../components/i2c/pca9548a"
    "../../../components/mosfet_driver"
    "../../../components/pwm_dimmer"
    "../../../components/relay"
    "../../../components/touch"
    "../../../components/vibration"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(compined-test)
