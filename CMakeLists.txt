cmake_minimum_required(VERSION 3.22)

list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/note4_ui"
    "${CMAKE_CURRENT_LIST_DIR}/components/zectrix_epd")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(magpie_note4)
