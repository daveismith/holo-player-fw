/*
 * GIFs: decoded with bitbank2's AnimatedGIF, painted in the same 16-line blocks as everything
 * else on the panel.
 *
 * A GIF frame is not a picture on its own. Most are a rectangle drawn over the frames before
 * them, some with holes where the old picture shows through, and some erase their area when
 * the next frame comes. So the decoder keeps a canvas -- AnimatedGIF's "cooked" mode, with a
 * full frame buffer and no draw callback -- and composites every frame onto it, and this file
 * paints the part of the canvas that changed.
 *
 * The frame buffer is the canvas twice: 8-bit palette indices, then the same picture as
 * big-endian RGB565 ready for the panel. 173 KB at 240x240, in PSRAM. It has to be the full
 * size. With a draw callback the decoder hands out one line at a time from a single shared
 * line buffer, and a transparent pixel is simply not written -- so it would arrive carrying
 * the line above's colour. And disposal method 2 writes straight into the RGB565 half, which
 * the smaller buffer allocFrameBuf() makes does not have.
 *
 * What changed is this frame's rectangle, plus the previous frame's when that one was disposed
 * to background. The decoder keeps the previous disposal to itself, so the union of the two is
 * painted every time: never less than what changed, and small for the small rectangles an
 * optimised GIF is made of.
 */
#include <errno.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include "AnimatedGIF.h"
#include "esp_heap_caps.h"
#include "board.h"
#include "gif_image.h"
#include "jpeg_decode.h"

using jpg::now_us;

namespace {

constexpr int BLOCK_LINES = 16;
constexpr uint8_t GIF_TRAILER = 0x3B;   /* ';', the last byte of every complete GIF */

/* ------------------------------------------------------------------ file access */

/* The decoder reads through these, from LittleFS, with stdio's buffer left on: it reads a
 * 255-byte chunk at a time, and small reads are what buffering is for. */
void *file_open(const char *path, int32_t *size)
{
    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    *size = (int32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    return f;
}

void file_close(void *handle)
{
    if (handle != nullptr) {
        fclose(static_cast<FILE *>(handle));
    }
}

int32_t file_read(GIFFILE *file, uint8_t *buf, int32_t len)
{
    int32_t n = len;
    if (file->iSize - file->iPos < n) {
        n = file->iSize - file->iPos;
    }
    if (n <= 0) {
        return 0;
    }
    n = (int32_t)fread(buf, 1, (size_t)n, static_cast<FILE *>(file->fHandle));
    file->iPos += n;
    return n;
}

int32_t file_seek(GIFFILE *file, int32_t pos)
{
    if (pos < 0) {
        pos = 0;
    } else if (pos > file->iSize) {
        pos = file->iSize;
    }
    fseek(static_cast<FILE *>(file->fHandle), pos, SEEK_SET);
    file->iPos = pos;
    return pos;
}

/*
 * The frames, the length of a pass, and whether the file is whole -- by walking the GIF's
 * blocks, not decoding them. The decoder has an info walk of its own, but it rounds short
 * delays differently from the way it plays them, so it would report a length the panel does
 * not keep to; this uses gif_frame_delay_ms(), as the player does.
 *
 * Whole means the walk reaches the trailer. The decoder carries on through LZW data that runs
 * out partway, so a file cut short in transfer would otherwise show its last frame half made
 * of whatever the buffer held.
 */
struct Walk {
    int32_t frames;
    int32_t duration_ms;
    bool whole;
};

Walk walk(const char *path)
{
    Walk w = {};
    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        return w;
    }
    auto skip_sub_blocks = [f]() {
        for (int len = fgetc(f); len > 0; len = fgetc(f)) {
            fseek(f, len, SEEK_CUR);
        }
    };
    uint8_t lsd[13];
    if (fread(lsd, 1, sizeof(lsd), f) == sizeof(lsd)) {
        if (lsd[10] & 0x80) {
            fseek(f, 3L << ((lsd[10] & 7) + 1), SEEK_CUR);   /* global colour table */
        }
        int delay_ms = 0;
        for (int c = fgetc(f); c != EOF; c = fgetc(f)) {
            if (c == GIF_TRAILER) {
                w.whole = true;
                break;
            }
            if (c == 0x21) {                                  /* an extension */
                const int label = fgetc(f);
                if (label == 0xF9) {                          /* graphic control: the delay */
                    uint8_t gce[5];
                    if (fread(gce, 1, sizeof(gce), f) != sizeof(gce)) {
                        break;
                    }
                    delay_ms = (gce[2] | (gce[3] << 8)) * 10;
                }
                skip_sub_blocks();
            } else if (c == 0x2C) {                           /* an image: one frame */
                uint8_t desc[9];
                if (fread(desc, 1, sizeof(desc), f) != sizeof(desc)) {
                    break;
                }
                if (desc[8] & 0x80) {
                    fseek(f, 3L << ((desc[8] & 7) + 1), SEEK_CUR);  /* local colour table */
                }
                fgetc(f);                                     /* LZW minimum code size */
                skip_sub_blocks();
                w.frames++;
                w.duration_ms += gif_frame_delay_ms(delay_ms);
                delay_ms = 0;
            } else {
                break;                                        /* not a block: corrupt */
            }
        }
    }
    fclose(f);
    return w;
}

const char *reason(int err)
{
    switch (err) {
    case GIF_DECODE_ERROR:
        return "decode error";
    case GIF_TOO_WIDE:
        return "too wide";
    case GIF_UNSUPPORTED_FEATURE:
        return "uses a feature the decoder does not support";
    case GIF_EARLY_EOF:
        return "truncated";
    case GIF_BAD_FILE:
        return "not a valid GIF";
    case GIF_ERROR_MEMORY:
        return "out of memory";
    default:
        return "decode error";
    }
}

/* The decoder's state is 24 KB, too much for any task's stack. Internal RAM for speed -- it is
 * the LZW tables and the read buffer -- with PSRAM as the fallback. */
AnimatedGIF *new_decoder()
{
    void *mem = heap_caps_malloc(sizeof(AnimatedGIF), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (mem == nullptr) {
        mem = heap_caps_malloc(sizeof(AnimatedGIF), MALLOC_CAP_SPIRAM);
    }
    return mem != nullptr ? new (mem) AnimatedGIF() : nullptr;
}

/* Closes the file too. Once only: close() does not forget the handle it closed. */
void delete_decoder(AnimatedGIF *dec)
{
    if (dec != nullptr) {
        dec->close();
        dec->~AnimatedGIF();
        heap_caps_free(dec);
    }
}

}  // namespace

/* ------------------------------------------------------------------ the file */

struct GifFile {
    AnimatedGIF *dec;
    uint8_t *frame_buf;     /* w*h palette indices, then w*h big-endian RGB565 */
    uint8_t *blocks[2];     /* w x 16 lines each, internal DMA */
    uint32_t w, h;
    bool repaint_all;       /* the first frame of a pass paints the whole canvas */
    int prev_x, prev_y, prev_w, prev_h;
};

bool gif_is_gif(const uint8_t *buf, size_t len)
{
    return len >= 6 && (memcmp(buf, "GIF87a", 6) == 0 || memcmp(buf, "GIF89a", 6) == 0);
}

esp_err_t gif_probe(const char *path, GifHeader *hdr, char *err, size_t err_len)
{
    *hdr = {};
    AnimatedGIF *dec = new_decoder();
    if (dec == nullptr) {
        snprintf(err, err_len, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    dec->begin(GIF_PALETTE_RGB565_BE);
    esp_err_t ret = ESP_OK;
    if (!dec->open(path, file_open, file_close, file_read, file_seek, nullptr)) {
        const int e = dec->getLastError();
        /* Too wide for the decoder is still a size worth naming. */
        hdr->width = (uint32_t)dec->getCanvasWidth();
        hdr->height = (uint32_t)dec->getCanvasHeight();
        if (e == GIF_TOO_WIDE) {
            ret = ESP_ERR_INVALID_SIZE;
        } else {
            snprintf(err, err_len, "%s", e == GIF_FILE_NOT_OPEN ? strerror(errno) : reason(e));
            ret = ESP_FAIL;
        }
        delete_decoder(dec);   /* closes the file, if it got as far as opening it */
        return ret;
    }
    hdr->width = (uint32_t)dec->getCanvasWidth();
    hdr->height = (uint32_t)dec->getCanvasHeight();
    hdr->loop_count = dec->getLoopCount();
    delete_decoder(dec);

    const Walk w = walk(path);
    if (!w.whole) {
        snprintf(err, err_len, "truncated: it ends before the GIF trailer");
        return ESP_ERR_INVALID_STATE;
    }
    if (w.frames == 0) {
        snprintf(err, err_len, "no frames");
        return ESP_ERR_INVALID_SIZE;
    }
    hdr->frames = w.frames;
    hdr->duration_ms = w.duration_ms;
    return ESP_OK;
}

GifFile *gif_open(const char *path, char *err, size_t err_len)
{
    GifFile *g = static_cast<GifFile *>(heap_caps_calloc(1, sizeof(GifFile), MALLOC_CAP_8BIT));
    if (g == nullptr) {
        snprintf(err, err_len, "out of memory");
        return nullptr;
    }
    g->dec = new_decoder();
    if (g->dec == nullptr) {
        snprintf(err, err_len, "out of memory for the decoder");
        gif_close(g);
        return nullptr;
    }
    g->dec->begin(GIF_PALETTE_RGB565_BE);
    if (!g->dec->open(path, file_open, file_close, file_read, file_seek, nullptr)) {
        const int e = g->dec->getLastError();
        snprintf(err, err_len, "%s", e == GIF_FILE_NOT_OPEN ? strerror(errno) : reason(e));
        gif_close(g);
        return nullptr;
    }
    g->w = (uint32_t)g->dec->getCanvasWidth();
    g->h = (uint32_t)g->dec->getCanvasHeight();

    /* Zeroed: the canvas starts black, which is what a GIF's first frame is drawn over here --
     * the same black transparency composites onto in a PNG. */
    const size_t canvas = (size_t)g->w * g->h;
    const size_t frame_buf = canvas * 3 + (size_t)g->w * 2;
    g->frame_buf = static_cast<uint8_t *>(heap_caps_calloc(1, frame_buf, MALLOC_CAP_SPIRAM));
    if (g->frame_buf == nullptr) {
        g->frame_buf = static_cast<uint8_t *>(heap_caps_calloc(1, frame_buf, MALLOC_CAP_8BIT));
    }
    const size_t block_len = (size_t)g->w * BLOCK_LINES * 2;
    for (auto &b : g->blocks) {
        b = static_cast<uint8_t *>(
            heap_caps_aligned_alloc(16, block_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    }
    if (g->frame_buf == nullptr || g->blocks[0] == nullptr || g->blocks[1] == nullptr) {
        snprintf(err, err_len, "out of memory for the decode buffers");
        gif_close(g);
        return nullptr;
    }
    g->dec->setDrawType(GIF_DRAW_COOKED);
    g->dec->setFrameBuf(g->frame_buf);
    g->repaint_all = true;
    return g;
}

/*
 * Paint a rectangle of the canvas: rows copied out of the PSRAM canvas into 16-line blocks,
 * each sent while the next is filled -- the interlock the clip player uses.
 */
static void paint(GifFile *g, int panel_x, int panel_y, int rx, int ry, int rw, int rh)
{
    const uint8_t *rgb = g->frame_buf + (size_t)g->w * g->h;
    const size_t row_bytes = (size_t)rw * 2;
    board_lcd_stream_begin();
    int which = 0;
    for (int done = 0; done < rh; done += BLOCK_LINES) {
        const int lines = rh - done < BLOCK_LINES ? rh - done : BLOCK_LINES;
        board_lcd_stream_wait(1);
        uint8_t *dst = g->blocks[which];
        for (int line = 0; line < lines; line++) {
            memcpy(dst + (size_t)line * row_bytes,
                   rgb + ((size_t)(ry + done + line) * g->w + rx) * 2, row_bytes);
        }
        board_lcd_stream_block(panel_x + rx, panel_y + ry + done, rw, lines,
                               reinterpret_cast<const uint16_t *>(g->blocks[which]));
        which ^= 1;
    }
    board_lcd_stream_end();
}

esp_err_t gif_next(GifFile *g, int x, int y, int *delay_ms, bool *drew, bool *last,
                   int64_t *decode_us, int64_t *paint_start, int64_t *paint_end,
                   char *err, size_t err_len)
{
    *delay_ms = 0;
    *drew = false;
    *last = false;
    *paint_start = *paint_end = 0;

    const int64_t t0 = now_us();
    const int rc = g->dec->playFrame(false, delay_ms, nullptr);
    *decode_us = now_us() - t0;
    const int e = g->dec->getLastError();
    if (rc < 0 || (e != GIF_SUCCESS && e != GIF_EMPTY_FRAME)) {
        snprintf(err, err_len, "%s", reason(e));
        return ESP_FAIL;
    }
    if (e == GIF_EMPTY_FRAME) {
        /* Past the last frame: trailing data, or the trailer itself. Nothing was drawn. */
        *last = true;
        return ESP_OK;
    }
    *drew = true;
    *last = rc == 0;

    /* This frame's rectangle, kept inside the canvas whatever the file says. */
    int fx = g->dec->getFrameXOff(), fy = g->dec->getFrameYOff();
    int fw = g->dec->getFrameWidth(), fh = g->dec->getFrameHeight();
    if (fx < 0) { fw += fx; fx = 0; }
    if (fy < 0) { fh += fy; fy = 0; }
    if (fx + fw > (int)g->w) { fw = (int)g->w - fx; }
    if (fy + fh > (int)g->h) { fh = (int)g->h - fy; }

    int rx, ry, rw, rh;
    if (g->repaint_all || g->prev_w <= 0 || g->prev_h <= 0) {
        rx = 0, ry = 0, rw = (int)g->w, rh = (int)g->h;
        g->repaint_all = false;
    } else {
        rx = fx < g->prev_x ? fx : g->prev_x;
        ry = fy < g->prev_y ? fy : g->prev_y;
        const int x2 = fx + fw > g->prev_x + g->prev_w ? fx + fw : g->prev_x + g->prev_w;
        const int y2 = fy + fh > g->prev_y + g->prev_h ? fy + fh : g->prev_y + g->prev_h;
        rw = x2 - rx;
        rh = y2 - ry;
    }
    g->prev_x = fx, g->prev_y = fy, g->prev_w = fw, g->prev_h = fh;

    if (rw > 0 && rh > 0) {
        *paint_start = now_us();
        paint(g, x, y, rx, ry, rw, rh);
        *paint_end = now_us();
    }
    return ESP_OK;
}

void gif_rewind(GifFile *g)
{
    g->dec->reset();
    memset(g->frame_buf, 0, (size_t)g->w * g->h * 3);
    g->repaint_all = true;
    g->prev_w = g->prev_h = 0;
}

void gif_close(GifFile *g)
{
    if (g == nullptr) {
        return;
    }
    delete_decoder(g->dec);
    heap_caps_free(g->frame_buf);
    heap_caps_free(g->blocks[0]);
    heap_caps_free(g->blocks[1]);
    heap_caps_free(g);
}
