cmake_minimum_required(VERSION 3.16.0)

# https://docs.platformio.org/en/latest/frameworks/espidf.html#id17
# Since src_dir is also passed to CMake as an extra component, you should only append to EXTRA_COMPONENT_DIRS variable in order not to override the default package.

list(APPEND EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/components/backoffAlgorithm"
        "${CMAKE_CURRENT_LIST_DIR}/components/coreMQTT"
        "${CMAKE_CURRENT_LIST_DIR}/components/common/posix_compat"
    )
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESP32_MQTT_DHT11_AWSGREENGRASSV2)
