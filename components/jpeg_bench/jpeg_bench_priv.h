/*
 * One entry point per decoder, each in its own file so the decoders' headers never meet.
 * Every one decodes a whole JPEG from RAM into `fb`, a `w` x `h` RGB565 frame stored
 * big-endian (as the SPI panel wants it), and returns 0 or a decoder-specific error.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int bench_jpegdec_decode(const uint8_t *jpg, size_t len, uint16_t *fb, int w, int h);
const char *bench_jpegdec_label(void);

int bench_new_jpeg_decode(const uint8_t *jpg, size_t len, uint16_t *fb, int w, int h);
const char *bench_new_jpeg_label(void);

/* esp_jpeg wants a scratch buffer; allocated once, so the timing is decoding alone. */
int bench_esp_jpeg_prepare(void);
int bench_esp_jpeg_decode(const uint8_t *jpg, size_t len, uint16_t *fb, int w, int h);
const char *bench_esp_jpeg_label(void);

#ifdef __cplusplus
}
#endif
