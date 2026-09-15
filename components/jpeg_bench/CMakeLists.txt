idf_component_register(SRCS "jpeg_bench.c" "bench_jpegdec.cpp" "bench_new_jpeg.c" "bench_esp_jpeg.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES console esp_timer heap esp_rom jpegdec board_ws128)
