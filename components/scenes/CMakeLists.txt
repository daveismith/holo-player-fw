idf_component_register(SRCS "scene_parts.c" "scenes.c" "settings.c" "runner.c"
                       INCLUDE_DIRS "include"
                       REQUIRES espressif__cjson
                       PRIV_REQUIRES video leds holo board_ws128 cmd_fs nvs_flash console esp_timer log events)
