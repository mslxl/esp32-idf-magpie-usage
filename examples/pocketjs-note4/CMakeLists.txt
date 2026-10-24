cmake_minimum_required(VERSION 3.22)

set(pocketjs_root "${CMAKE_CURRENT_LIST_DIR}/deps/pocketjs")
set(note4_root "${CMAKE_CURRENT_LIST_DIR}/deps/note4")

if(NOT EXISTS "${pocketjs_root}/hosts/esp-idf/components/pocketjs_guest")
    message(FATAL_ERROR "Missing pinned PocketJS checkout; run tools/fetch_deps.py")
endif()
if(NOT EXISTS "${note4_root}/components/zectrix_epd")
    message(FATAL_ERROR "Missing pinned NOTE4 checkout; run tools/fetch_deps.py")
endif()

set(pocketjs_components "${pocketjs_root}/hosts/esp-idf/components")
list(APPEND EXTRA_COMPONENT_DIRS
    "${pocketjs_components}/pocketjs_package"
    "${pocketjs_components}/pocketjs_guest"
    "${pocketjs_components}/pocketjs_ui_core"
    "${pocketjs_components}/pocketjs_ui_qjs"
    "${pocketjs_components}/pocketjs_render_rgb565"
    "${note4_root}/components/zectrix_epd")

set(POCKETJS_RUST_FROM_SOURCE ON CACHE BOOL
    "Build PocketJS native Rust archives from the pinned source checkout")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(pocketjs_note4_hello)
