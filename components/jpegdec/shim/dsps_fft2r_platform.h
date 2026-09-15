/*
 * Stand-in for esp-dsp's header of the same name. JPEGDEC's jpeg.inl includes it only to ask
 * whether the ESP32-S3's vector (AES3) instructions are available, and this component needs
 * nothing else from esp-dsp. Private to the jpegdec component.
 */
#pragma once

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3
#define dsps_fft2r_sc16_aes3_enabled 1
#else
#define dsps_fft2r_sc16_aes3_enabled 0
#endif
