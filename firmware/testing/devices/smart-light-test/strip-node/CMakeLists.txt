cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    "../../../../components/addressable"
    "../../../../components/display/gc9a01"
    "../../../../components/display/shared"
    "../../../../components/touch"
    "../../../../components/encoder"
    "../../../../devices/modules/smart-light"
    "../../../../wireless/esp_now"
    "../../../../system/message_protocol"
    "../../../../system/ota"
    "../../../../system/ota_bulk"
    "../../../../system/auto_pair"
    "../../../../system/config_store"
    "../../../../system/core_types"
        "../../../../system/device_identity"

)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smart_light_strip)