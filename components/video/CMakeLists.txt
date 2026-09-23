idf_component_register(SRCS "image.cpp" "png_image.c" "quicktime.cpp" "video_player.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES console esp_timer heap board_ws128)
