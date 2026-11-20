# The Cyphal node: libcanard, the committed Nunavut headers in dsdl/ (tools/gen_dsdl.py
# regenerates them), and the board's CAN controller through canbus. Nothing else of this board's.
idf_component_register(SRCS "cyphal.c" "cmd_cyphal.c" "cyphal_util.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "dsdl"
                       PRIV_REQUIRES canbus libcanard console nvs_flash esp_timer esp_app_format esp_hw_support
                                     heap freertos log)
