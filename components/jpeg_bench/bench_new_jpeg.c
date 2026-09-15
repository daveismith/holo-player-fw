/*
 * esp_new_jpeg: Espressif's current software JPEG codec (prebuilt, with S3 SIMD).
 * A handle per image -- open, parse, decode, close -- so its cost is per frame like the others.
 */
#include <stdio.h>
#include "esp_jpeg_dec.h"
#include "esp_jpeg_version.h"
#include "jpeg_bench_priv.h"

int bench_new_jpeg_decode(const uint8_t *jpg, size_t len, uint16_t *fb, int w, int h)
{
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;
    jpeg_dec_handle_t dec = NULL;
    jpeg_error_t err = jpeg_dec_open(&cfg, &dec);
    if (err != JPEG_ERR_OK) {
        return err;
    }
    /* outbuf must be 16-byte aligned; the caller's frame is. */
    jpeg_dec_io_t io = { .inbuf = (uint8_t *)jpg, .inbuf_len = (int)len, .outbuf = (uint8_t *)fb };
    jpeg_dec_header_info_t info;
    err = jpeg_dec_parse_header(dec, &io, &info);
    if (err == JPEG_ERR_OK) {
        err = (info.width == w && info.height == h) ? jpeg_dec_process(dec, &io)
                                                    : JPEG_ERR_INVALID_PARAM;
    }
    jpeg_dec_close(dec);
    return err;
}

const char *bench_new_jpeg_label(void)
{
    static char label[40];
    snprintf(label, sizeof(label), "esp_new_jpeg %s", esp_jpeg_get_version());
    return label;
}
