set(note4_hello_requires
    note4_ui
    zectrix_epd
    esp_driver_gpio)

idf_component_register(
    SRCS "app_main.c" "display_adapter.c" "note4_presenter.c"
         "wifi_config.c" "magpie_config.c" "settings.c" "config_store.c" "wifi_manager.c" "button_input.c"
         "provisioning_state.c" "ble_protocol.c" "ble_provisioning.c"
         "note4_screen.c" "magpie_client.c" "magpie_transport.c" "magpie_worker.c" "quota_snapshot.c" "quota_order.c" "magpie_poll.c" "account_directory.c" "display_prefs.c"
    INCLUDE_DIRS "."
    REQUIRES ${note4_hello_requires}
        bt esp_wifi esp_netif esp_event esp_hw_support nvs_flash esp_driver_gpio esp-tls mbedtls esp_timer lwip esp_app_format)
