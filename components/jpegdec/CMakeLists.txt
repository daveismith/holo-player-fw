# JPEGDEC by Larry Bank (github.com/bitbank2/JPEGDEC, Apache-2.0), vendored at two versions
# so they can be benchmarked against each other. They define the same symbols, so
# CONFIG_JPEGDEC_VERSION picks the one compiled.
if(CONFIG_JPEGDEC_V1_8_4)
    set(ver "1.8.4")
else()
    set(ver "1.5.0")
endif()
set(dir "v${ver}")

set(srcs "${dir}/JPEGDEC.cpp")
if(CONFIG_JPEGDEC_S3_SIMD)
    list(APPEND srcs "${dir}/s3_simd_420.S" "${dir}/s3_simd_444.S" "${dir}/s3_simd_dequant.S")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "${dir}"
                       PRIV_INCLUDE_DIRS "shim")

target_compile_definitions(${COMPONENT_LIB} PUBLIC JPEGDEC_VERSION_STR="${ver}")
if(CONFIG_JPEGDEC_S3_SIMD)
    # jpeg.inl turns on its ESP32-S3 assembly only in Arduino builds (ARDUINO_ARCH_ESP32), and
    # only once esp-dsp's dsps_fft2r_platform.h says the S3 vector unit is there -- which the
    # Arduino core always provides. shim/ answers that one question without pulling esp-dsp in,
    # so this build decodes the way the Arduino sketch did.
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ARDUINO_ARCH_ESP32)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE NO_SIMD)
endif()
