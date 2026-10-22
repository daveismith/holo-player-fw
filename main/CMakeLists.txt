idf_component_register(SRCS "main.c" "console_settings.c" "console_history.c" "holo_servos.c"
                            "wifi_fallback.c"
                       PRIV_REQUIRES console esp_driver_uart esp_driver_usb_serial_jtag
                                     esp_usb_cdc_rom_console nvs_flash esp_netif esp_event
                                     esp_timer vfs espcoredump esp_driver_gpio
                                     cmd_system cmd_wifi cmd_network cmd_nvs cmd_i2c cmd_fs
                                     board_ws128 video leds servo holo
                                     # IDF 6.1.0's i2c_master_probe() writes through a stale
                                     # pointer after a timed-out read. cmd_i2c and servo pull
                                     # the kit's guard in on 6.1.0 anyway; listed here so that
                                     # any other IDF stops at its #error rather than going
                                     # on unguarded with at most a configure warning.
                                     i2c_bus_guard
                                     web_server webui scenes hostlink
                       INCLUDE_DIRS ".")
