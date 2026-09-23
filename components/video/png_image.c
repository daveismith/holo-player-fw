/*
 * PNG stills, decoded with libpng and pushed to the panel in the same 16-line blocks the clip
 * player uses.
 *
 * Two ways out of the decoder, for the same reason the clip player has two:
 *
 *   streamed (the usual one) -- one row at a time out of libpng, converted into one of two
 *     small DMA buffers, and each full block of 16 lines sent while the next is filled.
 *
 *   whole image, in PSRAM -- Adam7 interlacing cannot be read row by row: each of the seven
 *     passes writes only its own pixels into rows the later passes fill in, so libpng needs
 *     every row resident at once. 240x240 of RGB is 173 KB, which is nothing in PSRAM and has
 *     no deadline to miss; the same block loop then feeds off it.
 *
 * libpng reports errors by longjmp, which is why this file is C and not C++: a jump out of a
 * C++ frame skips every destructor on the way. The jump target is set before libpng is given
 * anything at all, and every local the cleanup reads is volatile -- otherwise the compiler may
 * keep it in a register that longjmp restores to the value it had at the setjmp, and the
 * cleanup frees a stale pointer or leaks a live one. GCC does not warn about this by default.
 */
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "png.h"
#include "board.h"
#include "png_image.h"

/* One MCU row's worth, to match the clip player's blocks and the panel's strip buffer. */
#define BLOCK_LINES 16
/* A single ancillary chunk bigger than this is a colour profile nobody asked for. */
#define CHUNK_MALLOC_MAX 65536

/* What the error handler longjmps back to, and what it leaves behind. */
typedef struct {
    jmp_buf jump;
    char *err;
    size_t err_len;
} png_ctx_t;

static void on_error(png_structp png, png_const_charp msg)
{
    png_ctx_t *ctx = (png_ctx_t *)png_get_error_ptr(png);
    if (ctx->err != NULL && ctx->err_len > 0) {
        snprintf(ctx->err, ctx->err_len, "%s", msg);
    }
    longjmp(ctx->jump, 1);
}

/*
 * Nothing. libpng's default warning handler writes to stderr, which here is the console: an
 * ordinary Photoshop export would print "iCCP: known incorrect sRGB profile" over the prompt.
 */
static void on_warning(png_structp png, png_const_charp msg)
{
    (void)png;
    (void)msg;
}

bool png_is_png(const uint8_t *buf, size_t len)
{
    return len >= 8 && png_sig_cmp((png_const_bytep)buf, 0, 8) == 0;
}

/* One pixel, straight out as the two bytes the panel wants: big-endian RGB565. */
static inline void put_rgb565(uint8_t *dst, uint8_t r, uint8_t g, uint8_t b)
{
    dst[0] = (uint8_t)((r & 0xF8) | (g >> 5));
    dst[1] = (uint8_t)(((g & 0x1C) << 3) | (b >> 3));
}

/*
 * Open `path` and get libpng as far as ready to read it. The caller's jump target must already
 * be set: png_create_read_struct() itself can fail through the error handler.
 *
 * The file is left with stdio's buffer in place, unlike a clip: libpng reads 8-byte chunk
 * headers and 4-byte CRCs constantly, and buffering turns dozens of tiny reads into one.
 */
static esp_err_t begin_read(const char *path, png_ctx_t *ctx, png_structp *png, png_infop *info,
                            FILE **f)
{
    *f = fopen(path, "rb");
    if (*f == NULL) {
        snprintf(ctx->err, ctx->err_len, "cannot open %s: %s", path, strerror(errno));
        return ESP_ERR_NOT_FOUND;
    }
    *png = png_create_read_struct(PNG_LIBPNG_VER_STRING, ctx, on_error, on_warning);
    if (*png == NULL) {
        snprintf(ctx->err, ctx->err_len, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    *info = png_create_info_struct(*png);
    if (*info == NULL) {
        snprintf(ctx->err, ctx->err_len, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    png_set_chunk_malloc_max(*png, CHUNK_MALLOC_MAX);
    /* A bad CRC or an out-of-spec chunk is a warning rather than a refusal, so a file that
     * every other viewer opens still shows here. Truncation is not one of these: it surfaces
     * as a read error and still fails, which is the case that matters after a transfer. */
    png_set_benign_errors(*png, 1);
    png_init_io(*png, *f);
    return ESP_OK;
}

esp_err_t png_probe(const char *path, uint32_t *w_out, uint32_t *h_out, char *desc,
                    size_t desc_len, char *err, size_t err_len)
{
    static const char *const names[] = {
        "grey", "?", "RGB", "palette", "grey+alpha", "?", "RGBA"
    };
    png_ctx_t ctx = { .err = err, .err_len = err_len };
    /* Written before setjmp, read after a longjmp: their addresses escape into begin_read(),
     * so the compiler cannot cache them, but volatile says so out loud. */
    png_structp volatile png = NULL;
    png_infop volatile info = NULL;
    FILE *volatile f = NULL;
    volatile esp_err_t ret = ESP_FAIL;

    if (setjmp(ctx.jump) != 0) {
        /* libpng jumped out of something below, having left its reason in ctx.err. Set the
         * result here: by this point `ret` may be the ESP_OK that opening the file returned. */
        ret = ESP_FAIL;
        goto done;
    }
    ret = begin_read(path, &ctx, (png_structp *)&png, (png_infop *)&info, (FILE **)&f);
    if (ret != ESP_OK) {
        goto done;
    }

    png_read_info(png, info);
    png_uint_32 w = 0, h = 0;
    int depth = 0, colour = 0, interlace = 0;
    png_get_IHDR(png, info, &w, &h, &depth, &colour, &interlace, NULL, NULL);
    *w_out = w;
    *h_out = h;
    if (desc != NULL) {
        snprintf(desc, desc_len, "PNG %ux%u, %d-bit %s, %s", (unsigned)w, (unsigned)h, depth,
                 colour >= 0 && colour <= 6 ? names[colour] : "?",
                 interlace == PNG_INTERLACE_NONE ? "non-interlaced" : "interlaced");
    }
    ret = ESP_OK;

done:
    if (png != NULL) {
        png_destroy_read_struct((png_structp *)&png, (png_infop *)&info, NULL);
    }
    if (f != NULL) {
        fclose((FILE *)f);
    }
    return ret;
}

esp_err_t png_show(const char *path, int x, int y, uint32_t *w_out, uint32_t *h_out,
                   const char **mode, char *err, size_t err_len)
{
    png_ctx_t ctx = { .err = err, .err_len = err_len };
    png_structp volatile png = NULL;
    png_infop volatile info = NULL;
    FILE *volatile f = NULL;
    uint8_t *volatile row = NULL;            /* one row of RGB out of libpng */
    uint8_t *volatile block0 = NULL;
    uint8_t *volatile block1 = NULL;
    uint8_t *volatile whole = NULL;          /* interlaced only: every row at once */
    png_bytep *volatile rows = NULL;
    volatile bool streaming = false;
    volatile esp_err_t ret = ESP_FAIL;

    *mode = "16-line blocks";

    if (setjmp(ctx.jump) != 0) {
        /* libpng jumped out of something below, having left its reason in ctx.err. Set the
         * result here: by this point `ret` may be the ESP_OK that opening the file returned. */
        ret = ESP_FAIL;
        goto done;
    }
    ret = begin_read(path, &ctx, (png_structp *)&png, (png_infop *)&info, (FILE **)&f);
    if (ret != ESP_OK) {
        goto done;
    }

    png_read_info(png, info);
    png_uint_32 w = 0, h = 0;
    int depth = 0, colour = 0, interlace = 0;
    png_get_IHDR(png, info, &w, &h, &depth, &colour, &interlace, NULL, NULL);
    *w_out = w;
    *h_out = h;
    if (w == 0 || h == 0) {
        snprintf(err, err_len, "no pixels");
        ret = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    /* Before anything is sized, so an absurd header can never make us allocate. The caller
     * prints the size, which it has from *w_out and *h_out. */
    if (w > BOARD_LCD_H_RES || h > BOARD_LCD_V_RES) {
        ret = ESP_ERR_INVALID_SIZE;
        goto done;
    }

    /*
     * Whatever it is, out as 8-bit RGB. png_set_expand() covers palette, low-bit grey and tRNS
     * in one call; scale_16 rescales a 16-bit sample rather than chopping its low byte; and the
     * background composite is what stops a transparent pixel arriving as whatever RGB happens
     * to sit under it, which for most exports is white. Black is also what the panel wakes to.
     *
     * No gamma is set, so libpng builds no gamma tables -- at 16 bits they are tens of KB.
     */
    if (depth == 16) {
        png_set_scale_16(png);
    }
    png_set_expand(png);
    png_color_16 black;
    memset(&black, 0, sizeof(black));
    png_set_background(png, &black, PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
    if (colour == PNG_COLOR_TYPE_GRAY || colour == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    /* Unconditional: 1 pass for a plain image, 7 for Adam7, and png_read_image() insists the
     * transform is in place before the row loop is set up. */
    const int passes = png_set_interlace_handling(png);
    png_read_update_info(png, info);

    /* If any of that failed to normalise, the row buffer below would be the wrong size. */
    if (png_get_bit_depth(png, info) != 8 || png_get_color_type(png, info) != PNG_COLOR_TYPE_RGB ||
        png_get_channels(png, info) != 3 || png_get_rowbytes(png, info) != (size_t)w * 3) {
        snprintf(err, err_len, "unsupported PNG format (%d-bit, colour type %d)", depth, colour);
        ret = ESP_ERR_NOT_SUPPORTED;
        goto done;
    }

    const size_t row_len = (size_t)w * 3;
    const size_t block_len = (size_t)w * BLOCK_LINES * 2;
    block0 = heap_caps_aligned_alloc(16, block_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    block1 = heap_caps_aligned_alloc(16, block_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (block0 == NULL || block1 == NULL) {
        snprintf(err, err_len, "out of memory for the decode buffers");
        ret = ESP_ERR_NO_MEM;
        goto done;
    }
    if (passes > 1) {
        /* Zeroed: a decode that dies partway can then only ever show black, never whatever
         * the PSRAM held before. */
        whole = heap_caps_calloc(h, row_len, MALLOC_CAP_SPIRAM);
        rows = heap_caps_calloc(h, sizeof(png_bytep), MALLOC_CAP_SPIRAM);
        if (whole == NULL || rows == NULL) {
            snprintf(err, err_len, "out of memory for the decode buffers");
            ret = ESP_ERR_NO_MEM;
            goto done;
        }
        for (png_uint_32 i = 0; i < h; i++) {
            rows[i] = (png_bytep)whole + (size_t)i * row_len;
        }
        *mode = "whole image, in PSRAM";
        png_read_image(png, (png_bytepp)rows);
    } else {
        row = heap_caps_malloc(row_len, MALLOC_CAP_INTERNAL);
        if (row == NULL) {
            snprintf(err, err_len, "out of memory for the decode buffers");
            ret = ESP_ERR_NO_MEM;
            goto done;
        }
    }

    /*
     * Rows into 16-line blocks, each sent while the next is filled. The buffer about to be
     * refilled went out two blocks ago, so waiting for one still pending is enough -- the same
     * interlock the clip player uses.
     */
    board_lcd_stream_begin();
    streaming = true;
    png_uint_32 drawn = 0;
    int which = 0;
    while (drawn < h) {
        png_uint_32 lines = h - drawn;
        if (lines > BLOCK_LINES) {
            lines = BLOCK_LINES;
        }
        board_lcd_stream_wait(1);
        uint8_t *const buf = which == 0 ? block0 : block1;
        uint8_t *dst = buf;
        for (png_uint_32 line = 0; line < lines; line++) {
            const uint8_t *src;
            if (whole != NULL) {
                src = whole + (size_t)(drawn + line) * row_len;
            } else {
                png_read_row(png, row, NULL);
                src = row;
            }
            for (png_uint_32 px = 0; px < w; px++) {
                put_rgb565(dst + (size_t)px * 2, src[px * 3], src[px * 3 + 1], src[px * 3 + 2]);
            }
            dst += (size_t)w * 2;
        }
        board_lcd_stream_block(x, y + (int)drawn, (int)w, (int)lines, (const uint16_t *)buf);
        drawn += lines;
        which ^= 1;
    }
    board_lcd_stream_end();
    streaming = false;
    ret = ESP_OK;

done:
    /* A jump out of the row loop leaves the panel locked, and half an image on it. */
    if (streaming) {
        board_lcd_stream_end();
    }
    if (png != NULL) {
        png_destroy_read_struct((png_structp *)&png, (png_infop *)&info, NULL);
    }
    if (f != NULL) {
        fclose((FILE *)f);
    }
    heap_caps_free(row);
    heap_caps_free(block0);
    heap_caps_free(block1);
    heap_caps_free(whole);
    heap_caps_free(rows);
    return ret;
}
