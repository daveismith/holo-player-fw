idf_component_register(SRCS "main.c" "console_settings.c"
                       PRIV_REQUIRES console esp_driver_uart esp_driver_usb_serial_jtag
                                     esp_usb_cdc_rom_console nvs_flash esp_netif esp_event
                                     esp_timer vfs espcoredump
                                     cmd_system cmd_wifi cmd_network cmd_nvs cmd_i2c cmd_fs
                                     board_ws128
                       INCLUDE_DIRS ".")
