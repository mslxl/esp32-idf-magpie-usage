set(note4_hello_requires
    pocketjs_package
    pocketjs_guest
    pocketjs_ui_core
    pocketjs_ui_qjs
    pocketjs_render_rgb565
    zectrix_epd
    esp_driver_gpio)

idf_component_register(
    SRCS "app_main.c" "display_adapter.c" "note4_presenter.c"
    INCLUDE_DIRS "."
    REQUIRES ${note4_hello_requires})

set(POCKETJS_NOTE4_PACKAGE "" CACHE FILEPATH
    "Use a prebuilt .pocket package instead of invoking the PocketJS CLI")

if(NOT POCKETJS_NOTE4_PACKAGE AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../note4-hello.pocket")
    set(POCKETJS_NOTE4_PACKAGE "${CMAKE_CURRENT_LIST_DIR}/../note4-hello.pocket")
endif()

if(POCKETJS_NOTE4_PACKAGE)
    pocketjs_embed_package(
        TARGET ${COMPONENT_LIB}
        NAME note4_hello
        PACKAGE "${POCKETJS_NOTE4_PACKAGE}"
        HOST_PROFILE "../pocket.host.json")
else()
    pocketjs_compile_app(
        TARGET ${COMPONENT_LIB}
        NAME note4_hello
        MANIFEST "../pocket.json"
        HOST_PROFILE "../pocket.host.json"
        PROJECT_ROOT "..")
endif()
