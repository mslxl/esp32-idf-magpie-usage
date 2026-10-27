cmake_minimum_required(VERSION 3.22)

option(NOTE4_DEVELOPMENT "Build development-only USB screenshot transport" OFF)

list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/note4_ui"
    "${CMAKE_CURRENT_LIST_DIR}/components/zectrix_epd")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
idf_build_set_property(NOTE4_DEVELOPMENT ${NOTE4_DEVELOPMENT})
project(magpie_note4)

# Bonding without NVS persistence silently loses the pairing whenever setup
# stops. Fail instead of producing firmware that repeatedly prompts or fails to
# reconnect when an existing sdkconfig predates sdkconfig.defaults.
if(NOT CONFIG_BT_NIMBLE_NVS_PERSIST)
    message(FATAL_ERROR
        "CONFIG_BT_NIMBLE_NVS_PERSIST must be enabled for BLE bond persistence. "
        "Enable it in menuconfig, or delete the generated sdkconfig so it is "
        "recreated from sdkconfig.defaults, then rebuild.")
endif()
