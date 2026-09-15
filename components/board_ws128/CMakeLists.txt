set(srcs "board.c" "lcd.c" "imu.c" "cmd_board.c" "colour.c")
if(CONFIG_BOARD_TOUCH_ENABLE)
    list(APPEND srcs "touch.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_ledc esp_driver_gpio esp_timer console)
