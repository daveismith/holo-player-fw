idf_component_register(SRCS "board_api.c" "api_screen.c" "api_leds.c" "api_holo.c" "api_scenes.c" "api_events.c" "api_codes.c"
                       INCLUDE_DIRS "include"
                       REQUIRES api_core scenes espressif__cjson video
                       PRIV_REQUIRES board_ws128 leds holo cmd_fs events esp_timer log)
