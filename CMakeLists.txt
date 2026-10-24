cmake_minimum_required(VERSION 3.22)

set(pocketjs_root "${CMAKE_CURRENT_LIST_DIR}/third_party/pocketjs")
set(pocketjs_components "${pocketjs_root}/hosts/esp-idf/components")
set(pocketjs_overlay "${CMAKE_CURRENT_LIST_DIR}/.pocket-build/pocketjs-overlay")
set(note4_component "${CMAKE_CURRENT_LIST_DIR}/components/zectrix_epd")

if(NOT EXISTS "${pocketjs_root}/hosts/esp-idf/components/pocketjs_guest")
    message(FATAL_ERROR "Missing PocketJS submodule; run git submodule update --init --recursive")
endif()
if(NOT EXISTS "${note4_component}/CMakeLists.txt")
    message(FATAL_ERROR "Missing tracked NOTE4 display component")
endif()
if(NOT EXISTS "${pocketjs_overlay}/pocketjs_guest/CMakeLists.txt")
    message(FATAL_ERROR "Missing prepared PocketJS overlay; run tools/prepare_pocketjs.py")
endif()

list(APPEND EXTRA_COMPONENT_DIRS
    "${pocketjs_components}/pocketjs_package"
    "${pocketjs_overlay}/pocketjs_guest"
    "${pocketjs_components}/pocketjs_ui_core"
    "${pocketjs_components}/pocketjs_ui_qjs"
    "${pocketjs_components}/pocketjs_render_rgb565"
    "${note4_component}")

set(POCKETJS_RUST_FROM_SOURCE ON CACHE BOOL
    "Build PocketJS native Rust archives from the pinned source checkout")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(pocketjs_note4_hello)
