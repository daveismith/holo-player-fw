idf_component_register(SRCS "hostlink.c" "hl_line.c" "hl_events.c" "hl_uart.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES api_core events console nvs_flash esp_driver_uart esp_driver_gpio
                                     esp_timer esp_app_format log espressif__cjson)
