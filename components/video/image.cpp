/*
 * An image on the panel: `image show <file>`.
 *
 * A JPEG still is one frame of a clip without the container or the clock, so it goes through
 * the same esp_new_jpeg paths the player uses (jpeg_decode.h) -- 16-line blocks where the
 * dimensions allow it, the whole image otherwise. PNG is its own decoder, in png_image.c, and
 * GIF is bitbank2's AnimatedGIF, in gif_image.cpp.
 *
 * A still has no task and no timing: it is drawn on the console's own task and then simply
 * stays, as a colour does, until something else takes the panel. An animated GIF is the one
 * exception -- it has a clock, so it is handed to the video player's task, which paces clips.
 *
 * Everything that can fail cheaply -- the file, the format, the size, the buffers -- is made
 * to fail before the panel is woken, so a rejected `image show` leaves what was already on the
 * screen alone.
 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "board.h"
#include "gif_image.h"
#include "image.h"
#include "jpeg_decode.h"
#include "png_image.h"
#include "screen_state.h"

namespace {

constexpr size_t PATH_LEN = 160;
constexpr size_t ERR_LEN = 96;
constexpr uint32_t BLOCK_LINES = 16;
/* A JPEG that fits the panel cannot be this big; anything larger is a mistake, most likely
 * `image show` aimed at a clip. Only JPEG is read whole -- PNG and GIF stream from the file,
 * and an animated GIF can reasonably be bigger than this. */
constexpr size_t MAX_JPEG = 2u * 1024 * 1024;

using jpg::can_stream;
using jpg::decode_blocks;
using jpg::decode_frame;
using jpg::jpeg_probe;
using jpg::now_us;

/* Just enough of the file to tell what it is, and how big it is on disk. */
bool sniff(const char *path, uint8_t sig[8], size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        printf("image: cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    const size_t n = fread(sig, 1, 8, f);
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fclose(f);
    if (n < 8 || size <= 0) {
        printf("image: %s is not a PNG, JPEG or GIF\n", path);
        return false;
    }
    *len_out = (size_t)size;
    return true;
}

bool is_jpeg_file(const uint8_t *sig)
{
    return sig[0] == 0xFF && sig[1] == 0xD8 && sig[2] == 0xFF;
}

/* The whole file, for the JPEG decoder to work over. PNG and GIF never need this: their
 * decoders read as they go. */
uint8_t *read_file(const char *path, size_t len)
{
    if (len > MAX_JPEG) {
        printf("image: %s is %u bytes; too large to read\n", path, (unsigned)len);
        return nullptr;
    }
    /* Internal for the decoder's sake, but it reads this with the CPU rather than DMA, so
     * PSRAM will do when internal memory is short. */
    uint8_t *buf = static_cast<uint8_t *>(
        heap_caps_aligned_alloc(16, len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (buf == nullptr) {
        buf = static_cast<uint8_t *>(heap_caps_aligned_alloc(16, len, MALLOC_CAP_SPIRAM));
    }
    if (buf == nullptr) {
        printf("image: out of memory for the decode buffers\n");
        return nullptr;
    }
    FILE *f = fopen(path, "rb");
    const size_t n = f != nullptr ? fread(buf, 1, len, f) : 0;
    if (f != nullptr) {
        fclose(f);
    }
    if (n != len) {
        printf("image: %s: short read\n", path);
        heap_caps_free(buf);
        return nullptr;
    }
    return buf;
}

bool fits(const char *path, uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0) {
        printf("image: %s has no pixels\n", path);
        return false;
    }
    if (w > BOARD_LCD_H_RES || h > BOARD_LCD_V_RES) {
        printf("image: %" PRIu32 "x%" PRIu32 " does not fit the %dx%d panel\n", w, h,
               BOARD_LCD_H_RES, BOARD_LCD_V_RES);
        return false;
    }
    return true;
}

/* A JPEG error the decoder has a reason for, worth relaying instead of a bare number. */
const char *jpeg_reason(int err)
{
    switch (err) {
    case JPEG_ERR_UNSUPPORT_STD:
        /* Progressive, or arithmetic coding. */
        return "only baseline JPEG is supported";
    case JPEG_ERR_UNSUPPORT_FMT:
        /* 4:4:4, which ffmpeg picks by default when the source is a still RGB image. */
        return "unsupported colour sampling; re-encode as 4:2:0";
    case JPEG_ERR_BAD_DATA:
        /* A corrupt file, and also how 4:2:2 fails: it parses and then decodes to nothing. */
        return "bad JPEG data: corrupt, or colour sampling the decoder cannot read";
    case JPEG_ERR_NO_MORE_DATA:
        return "truncated";
    default:
        return nullptr;
    }
}

void print_jpeg_error(const char *path, int rc)
{
    const char *why = jpeg_reason(rc);
    if (why != nullptr) {
        printf("image: %s: %s\n", path, why);
    } else {
        printf("image: %s: decode error %d\n", path, rc);
    }
}

/*
 * A JPEG that is already in memory, onto a panel that is already on. `mode` gets the word for
 * how it was decoded, for the line the caller prints.
 */
esp_err_t show_jpeg(const uint8_t *buf, size_t len, uint32_t w, uint32_t h, int x, int y,
                    const char **mode, int *rc_out)
{
    int rc = JPEG_ERR_FAIL;
    bool streamed = false;

    if (can_stream(w, h)) {
        const size_t block_len = (size_t)w * BLOCK_LINES * 2;
        uint8_t *blocks[2] = { nullptr, nullptr };
        for (auto &b : blocks) {
            b = static_cast<uint8_t *>(
                heap_caps_aligned_alloc(16, block_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
        }
        if (blocks[0] != nullptr && blocks[1] != nullptr) {
            int64_t decode_us = 0;
            board_lcd_stream_begin();
            rc = decode_blocks(buf, len, w, h, blocks, block_len, &decode_us,
                [](int) {
                    /* the buffer about to be refilled went out two blocks ago */
                    board_lcd_stream_wait(1);
                },
                [&](uint32_t row, uint32_t lines, const uint16_t *pixels) {
                    board_lcd_stream_block(x, y + (int)row, (int)w, (int)lines, pixels);
                });
            board_lcd_stream_end();
            streamed = rc == JPEG_ERR_OK;
            *mode = "16-line blocks";
        }
        heap_caps_free(blocks[0]);
        heap_caps_free(blocks[1]);
    }
    if (streamed) {
        *rc_out = rc;
        return ESP_OK;
    }

    /*
     * Whole image instead, for a dimension that is not a multiple of 8, when there was no room
     * for two DMA buffers, or when block mode turned the file down -- 4:2:2 decodes whole but
     * not in blocks, and there is no way to know before trying. Anything already half-drawn is
     * simply overwritten. board_lcd_draw() stages PSRAM through its own internal strip, so
     * PSRAM is a real fallback here.
     */
    const size_t frame = (size_t)w * h * 2;
    uint16_t *fb = static_cast<uint16_t *>(
        heap_caps_aligned_alloc(16, frame, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    *mode = "whole image";
    if (fb == nullptr) {
        fb = static_cast<uint16_t *>(heap_caps_aligned_alloc(16, frame, MALLOC_CAP_SPIRAM));
        *mode = "whole image, in PSRAM";
    }
    if (fb == nullptr) {
        printf("image: out of memory for the decode buffers\n");
        *rc_out = JPEG_ERR_OK;
        return ESP_ERR_NO_MEM;
    }
    rc = decode_frame(buf, len, fb, w, h);
    if (rc == JPEG_ERR_OK) {
        board_lcd_draw(x, y, (int)w, (int)h, fb);
    }
    heap_caps_free(fb);
    *rc_out = rc;
    return rc == JPEG_ERR_OK ? ESP_OK : ESP_FAIL;
}

/* How the file says to loop, in words. */
void describe_loops(int loop_count, char *out, size_t len)
{
    if (loop_count < 0) {
        snprintf(out, len, "plays once");
    } else if (loop_count == 0) {
        snprintf(out, len, "loops forever");
    } else {
        snprintf(out, len, "plays %d times", loop_count + 1);
    }
}

/* The header, and everything that can refuse the file, before the panel is touched. */
bool probe_gif(const char *path, GifHeader *hdr)
{
    char err[ERR_LEN] = "";
    const esp_err_t rc = gif_probe(path, hdr, err, sizeof(err));
    if (rc == ESP_ERR_INVALID_SIZE) {
        /* Wider than the decoder takes: the same refusal as any other oversized image. */
        fits(path, hdr->width, hdr->height);
        return false;
    }
    if (rc != ESP_OK) {
        printf("image: %s: %s\n", path, err[0] != '\0' ? err : esp_err_to_name(rc));
        return false;
    }
    return fits(path, hdr->width, hdr->height);
}

/*
 * A GIF: one frame is a still, painted here like any other; more than one is an animation,
 * handed to the video player's task. Either way the decoder and its buffers are ready before
 * the panel is touched.
 */
int show_gif(const char *path)
{
    GifHeader hdr;
    if (!probe_gif(path, &hdr)) {
        return 1;
    }
    char err[ERR_LEN] = "";
    GifFile *g = gif_open(path, err, sizeof(err));
    if (g == nullptr) {
        printf("image: %s: %s\n", path, err);
        return 1;
    }

    if (hdr.frames > 1) {
        char loops[32];
        describe_loops(hdr.loop_count, loops, sizeof(loops));
        if (video_play_gif(g, hdr, path) != ESP_OK) {
            return 1;
        }
        printf("image: playing %s (%" PRIu32 "x%" PRIu32 " GIF, %" PRId32 " frames, %s)\n",
               path, hdr.width, hdr.height, hdr.frames, loops);
        return 0;
    }

    screen_take_panel();
    const int x = (BOARD_LCD_H_RES - (int)hdr.width) / 2;
    const int y = (BOARD_LCD_V_RES - (int)hdr.height) / 2;
    const int64_t t0 = now_us();
    esp_err_t rc = board_lcd_power_on(0x0000);
    if (rc == ESP_OK) {
        int delay_ms;
        bool drew, last;
        int64_t decode_us, paint_start, paint_end;
        rc = gif_next(g, x, y, &delay_ms, &drew, &last, &decode_us, &paint_start, &paint_end,
                      err, sizeof(err));
    }
    const int64_t took_us = now_us() - t0;
    gif_close(g);
    if (rc != ESP_OK) {
        board_lcd_fill(0x0000);
        printf("image: %s: %s\n", path, err[0] != '\0' ? err : esp_err_to_name(rc));
        return 1;
    }
    screen_set_image(path, hdr.width, hdr.height);
    printf("image: showing %s (%" PRIu32 "x%" PRIu32 " GIF, %.1f ms)\n", path, hdr.width,
           hdr.height, took_us / 1000.0);
    return 0;
}

int info_gif(const char *path, size_t len, int64_t t0)
{
    GifHeader hdr;
    char err[ERR_LEN] = "";
    const esp_err_t rc = gif_probe(path, &hdr, err, sizeof(err));
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_SIZE) {
        printf("image: %s: %s\n", path, err[0] != '\0' ? err : esp_err_to_name(rc));
        return 1;
    }
    char loops[32];
    describe_loops(hdr.loop_count, loops, sizeof(loops));
    if (hdr.frames > 1) {
        printf("%s: GIF %" PRIu32 "x%" PRIu32 ", %" PRId32 " frames, %s, %.2f s a pass, "
               "%u bytes\n", path, hdr.width, hdr.height, hdr.frames, loops,
               hdr.duration_ms / 1000.0, (unsigned)len);
    } else {
        printf("%s: GIF %" PRIu32 "x%" PRIu32 ", one frame, %u bytes\n", path, hdr.width,
               hdr.height, (unsigned)len);
    }
    printf("parsed in %.1f ms\n", (now_us() - t0) / 1000.0);
    return 0;
}

int show(const char *path)
{
    uint8_t sig[8];
    size_t len = 0;
    if (!sniff(path, sig, &len)) {
        return 1;
    }
    if (gif_is_gif(sig, sizeof(sig))) {
        return show_gif(path);
    }
    const bool png = png_is_png(sig, sizeof(sig));
    if (!png && !is_jpeg_file(sig)) {
        printf("image: %s is not a PNG, JPEG or GIF\n", path);
        return 1;
    }

    /*
     * The header first, on both paths, and the buffers after it: the size has to be known and
     * accepted before the panel is touched, so a file we will not show leaves whatever is
     * already on the screen alone.
     */
    uint32_t w = 0, h = 0;
    char err[ERR_LEN] = "";
    uint8_t *buf = nullptr;
    if (png) {
        const esp_err_t rc = png_probe(path, &w, &h, nullptr, 0, err, sizeof(err));
        if (rc != ESP_OK) {
            printf("image: %s: %s\n", path, err[0] != '\0' ? err : esp_err_to_name(rc));
            return 1;
        }
        if (!fits(path, w, h)) {
            return 1;
        }
    } else {
        buf = read_file(path, len);
        if (buf == nullptr) {
            return 1;
        }
        const int rc = jpeg_probe(buf, len, &w, &h);
        if (rc != JPEG_ERR_OK) {
            print_jpeg_error(path, rc);
            heap_caps_free(buf);
            return 1;
        }
        if (!fits(path, w, h)) {
            heap_caps_free(buf);
            return 1;
        }
    }

    /* Committed: take the panel from whatever had it, and wake it black so nothing of what was
     * there survives around a smaller image. */
    screen_take_panel();
    const int x = (BOARD_LCD_H_RES - (int)w) / 2, y = (BOARD_LCD_V_RES - (int)h) / 2;
    const char *mode = "16-line blocks";
    int rc = JPEG_ERR_OK;
    const int64_t t0 = now_us();
    esp_err_t drawn = board_lcd_power_on(0x0000);
    if (drawn == ESP_OK) {
        if (png) {
            uint32_t got_w = 0, got_h = 0;
            drawn = png_show(path, x, y, &got_w, &got_h, &mode, err, sizeof(err));
        } else {
            drawn = show_jpeg(buf, len, w, h, x, y, &mode, &rc);
        }
    }
    const int64_t took_us = now_us() - t0;
    heap_caps_free(buf);

    if (drawn != ESP_OK) {
        /* Streaming writes straight to the panel, so a failure partway through leaves part of
         * an image on it. Black, rather than something half true. */
        board_lcd_fill(0x0000);
        if (rc != JPEG_ERR_OK) {
            print_jpeg_error(path, rc);
        } else if (err[0] != '\0') {
            printf("image: %s: %s\n", path, err);
        } else {
            printf("image: %s: %s\n", path, esp_err_to_name(drawn));
        }
        return 1;
    }
    screen_set_image(path, w, h);
    printf("image: showing %s (%" PRIu32 "x%" PRIu32 " %s, %s, %.1f ms)\n", path, w, h,
           png ? "PNG" : "JPEG", mode, took_us / 1000.0);
    return 0;
}

int info(const char *path)
{
    uint8_t sig[8];
    size_t len = 0;
    if (!sniff(path, sig, &len)) {
        return 1;
    }
    const int64_t t0 = now_us();
    if (png_is_png(sig, sizeof(sig))) {
        char desc[96] = "", err[ERR_LEN] = "";
        uint32_t w = 0, h = 0;
        const esp_err_t rc = png_probe(path, &w, &h, desc, sizeof(desc), err, sizeof(err));
        if (rc != ESP_OK) {
            printf("image: %s: %s\n", path, err[0] != '\0' ? err : esp_err_to_name(rc));
            return 1;
        }
        printf("%s: %s, %u bytes\n", path, desc, (unsigned)len);
        printf("parsed in %.1f ms\n", (now_us() - t0) / 1000.0);
        return 0;
    }
    if (gif_is_gif(sig, sizeof(sig))) {
        return info_gif(path, len, t0);
    }
    if (!is_jpeg_file(sig)) {
        printf("image: %s is not a PNG, JPEG or GIF\n", path);
        return 1;
    }
    uint8_t *buf = read_file(path, len);
    if (buf == nullptr) {
        return 1;
    }
    uint32_t w = 0, h = 0;
    const int rc = jpeg_probe(buf, len, &w, &h);
    heap_caps_free(buf);
    if (rc != JPEG_ERR_OK) {
        print_jpeg_error(path, rc);
        return 1;
    }
    printf("%s: JPEG %" PRIu32 "x%" PRIu32 " baseline, %s, %u bytes\n", path, w, h,
           can_stream(w, h) ? "16-line blocks" : "whole image", (unsigned)len);
    printf("parsed in %.1f ms\n", (now_us() - t0) / 1000.0);
    return 0;
}

int cmd_image(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "";
    char path[PATH_LEN];
    if (strcmp(sub, "show") == 0 && argc >= 3) {
        screen_resolve_path(argv[2], path, sizeof(path));
        return show(path);
    }
    if (strcmp(sub, "info") == 0 && argc >= 3) {
        screen_resolve_path(argv[2], path, sizeof(path));
        return info(path);
    }
    printf("usage: image show <file> | info <file>\n");
    return 1;
}

}  // namespace

extern "C" void register_image_command(void)
{
    esp_console_cmd_t cmd = {};
    cmd.command = "image";
    cmd.help = "Show a PNG, baseline JPEG or GIF on the panel, centred, where it stays until "
               "something else takes the screen -- an animated GIF plays, on the video "
               "player's task; or describe one without showing it";
    cmd.hint = "show <file> | info <file>";
    cmd.func = cmd_image;
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
