/*
 * JPEG decoding with esp_new_jpeg, shared by the clip player and the still-image viewer.
 *
 * Private to this component: it is next to the sources rather than under include/, because
 * nothing outside components/video decodes anything.
 *
 * decode_blocks() is a template so the caller's per-block work inlines into the decode loop --
 * for a clip that is the double-buffer interlock against the panel, and a still uses the same
 * one. That is why these live in a header at all.
 */
#pragma once

#include <stdint.h>
#include <stdio.h>
#include "esp_jpeg_dec.h"
#include "esp_timer.h"

namespace jpg {

inline int64_t now_us() { return esp_timer_get_time(); }

/* Block mode decodes a whole MCU row at a time, so both dimensions must be multiples of 8. */
inline bool can_stream(uint32_t w, uint32_t h)
{
    return w % 8 == 0 && h % 8 == 0;
}

/* The whole frame into `fb` (w x h, 16-byte aligned). */
inline int decode_frame(const uint8_t *jpg, size_t len, uint16_t *fb, uint32_t w, uint32_t h)
{
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;   /* the panel's byte order */
    jpeg_dec_handle_t dec = nullptr;
    jpeg_error_t err = jpeg_dec_open(&cfg, &dec);
    if (err != JPEG_ERR_OK) {
        return err;
    }
    jpeg_dec_io_t io = {};
    io.inbuf = const_cast<uint8_t *>(jpg);
    io.inbuf_len = (int)len;
    io.outbuf = reinterpret_cast<uint8_t *>(fb);
    jpeg_dec_header_info_t info;
    err = jpeg_dec_parse_header(dec, &io, &info);
    if (err == JPEG_ERR_OK) {
        err = (info.width == w && info.height == h) ? jpeg_dec_process(dec, &io)
                                                    : JPEG_ERR_INVALID_PARAM;
    }
    jpeg_dec_close(dec);
    return err;
}

/*
 * The frame a block at a time, alternating between bufs[0] and bufs[1] (each `buf_len` bytes,
 * 16-byte aligned): before(i) runs before block i is decoded into bufs[i & 1] -- the moment to
 * make sure that buffer is free -- and after(row, lines, pixels) once it is. Returns the
 * decoder's error; *decode_us gets the time spent inside it.
 */
template <typename Before, typename After>
int decode_blocks(const uint8_t *jpg, size_t len, uint32_t w, uint32_t h, uint8_t *const bufs[2],
                  size_t buf_len, int64_t *decode_us, Before before, After after)
{
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;
    cfg.block_enable = true;
    jpeg_dec_handle_t dec = nullptr;
    jpeg_error_t err = jpeg_dec_open(&cfg, &dec);
    if (err != JPEG_ERR_OK) {
        return err;
    }
    jpeg_dec_io_t io = {};
    io.inbuf = const_cast<uint8_t *>(jpg);
    io.inbuf_len = (int)len;
    jpeg_dec_header_info_t info;
    int blocks = 0, block_len = 0;
    err = jpeg_dec_parse_header(dec, &io, &info);
    if (err == JPEG_ERR_OK && (info.width != w || info.height != h)) {
        err = JPEG_ERR_INVALID_PARAM;
    }
    if (err == JPEG_ERR_OK) {
        err = jpeg_dec_get_process_count(dec, &blocks);
    }
    if (err == JPEG_ERR_OK) {
        err = jpeg_dec_get_outbuf_len(dec, &block_len);
    }
    if (err == JPEG_ERR_OK && (blocks <= 0 || block_len <= 0 || (size_t)block_len > buf_len)) {
        err = JPEG_ERR_NO_MEM;
    }
    uint32_t row = 0;
    for (int i = 0; i < blocks && err == JPEG_ERR_OK && row < h; i++) {
        before(i);
        io.outbuf = bufs[i & 1];
        const int64_t t0 = now_us();
        err = jpeg_dec_process(dec, &io);
        *decode_us += now_us() - t0;
        if (err != JPEG_ERR_OK) {
            break;
        }
        /* Rows are the image's width, unpadded; the last block of a 120-line frame is 8 lines
         * of a 16-line MCU row, and the count clips anything beyond the image. */
        uint32_t lines = (uint32_t)io.out_size / (w * 2);
        if (lines > h - row) {
            lines = h - row;
        }
        if (lines > 0) {
            after(row, lines, reinterpret_cast<const uint16_t *>(io.outbuf));
            row += lines;
        }
    }
    jpeg_dec_close(dec);
    return err;
}

/*
 * Width and height from the JPEG header, without decoding anything. A clip knows its frames'
 * size from the QuickTime sample description before it reads one; a still has only the file,
 * so it has to ask the decoder before it can size a buffer. Returns the decoder's error, which
 * is what separates "not a JPEG" from "progressive" from "truncated".
 */
inline int jpeg_probe(const uint8_t *jpg, size_t len, uint32_t *w, uint32_t *h)
{
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;
    jpeg_dec_handle_t dec = nullptr;
    jpeg_error_t err = jpeg_dec_open(&cfg, &dec);
    if (err != JPEG_ERR_OK) {
        return err;
    }
    jpeg_dec_io_t io = {};
    io.inbuf = const_cast<uint8_t *>(jpg);
    io.inbuf_len = (int)len;
    jpeg_dec_header_info_t info;
    err = jpeg_dec_parse_header(dec, &io, &info);
    if (err == JPEG_ERR_OK) {
        *w = info.width;
        *h = info.height;
    }
    jpeg_dec_close(dec);
    return err;
}

}  // namespace jpg
