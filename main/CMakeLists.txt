set(note4_hello_requires
    note4_ui
    zectrix_epd
    esp_driver_gpio)

idf_build_get_property(note4_development NOTE4_DEVELOPMENT)
if(note4_development)
    list(APPEND note4_hello_requires esp_driver_usb_serial_jtag)
    set(note4_dev_sources "dev_capture.c")
endif()

idf_component_register(
    SRCS ${note4_dev_sources} "app_main.c" "display_adapter.c" "note4_presenter.c"
         "wifi_config.c" "magpie_config.c" "settings.c" "config_store.c" "wifi_manager.c" "button_input.c"
         "provisioning_state.c" "ble_protocol.c" "ble_provisioning.c"
         "note4_screen.c" "battery.c" "magpie_client.c" "magpie_transport.c" "magpie_worker.c" "quota_snapshot.c" "quota_order.c" "magpie_poll.c" "account_directory.c" "display_prefs.c" "note4_bytes.c"
    INCLUDE_DIRS "."
    REQUIRES ${note4_hello_requires}
        bt esp_wifi esp_netif esp_event esp_hw_support esp_pm nvs_flash esp_driver_gpio esp-tls mbedtls esp_timer lwip esp_app_format esp_adc)

if(note4_development)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE NOTE4_DEV_CAPTURE=1)
endif()
