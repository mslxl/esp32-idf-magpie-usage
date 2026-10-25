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
         "wifi_config.c" "magpie_config.c" "settings.c" "config_store.c" "wifi_manager.c" "button_input.c"
         "provisioning_state.c" "ble_protocol.c" "ble_provisioning.c"
         "note4_screen.c" "magpie_client.c" "magpie_transport.c" "magpie_worker.c" "quota_snapshot.c" "quota_order.c" "magpie_poll.c" "account_directory.c" "display_prefs.c"
    INCLUDE_DIRS "."
    REQUIRES ${note4_hello_requires}
        bt esp_wifi esp_netif esp_event esp_hw_support nvs_flash esp_driver_gpio esp-tls mbedtls esp_timer lwip)

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
