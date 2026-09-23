# bitbank2/AnimatedGIF, from the submodule at external/AnimatedGIF, as an ESP-IDF component.
#
# Upstream is an Arduino library with no IDF packaging, and it is used here unmodified: this
# file is the packaging. The one thing it lacks off Arduino is millis() and delay(), which
# AnimatedGIF::playFrame() calls unless it is built for Linux or macOS; shim/idf_shim.h
# supplies them, forced in ahead of the library's own source.
set(upstream "${CMAKE_CURRENT_LIST_DIR}/../../external/AnimatedGIF/src")

idf_component_register(SRCS "${upstream}/AnimatedGIF.cpp"
                       INCLUDE_DIRS "${upstream}"
                       PRIV_INCLUDE_DIRS "shim"
                       PRIV_REQUIRES esp_timer freertos)

# Upstream declares a few static helpers it only uses on other platforms.
target_compile_options(${COMPONENT_LIB} PRIVATE
                       -include "${CMAKE_CURRENT_LIST_DIR}/shim/idf_shim.h"
                       -Wno-unused-function)
