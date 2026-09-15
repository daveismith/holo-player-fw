/*
 * MJPEG QuickTime playback: each frame read from storage, decoded with esp_new_jpeg and put in
 * the middle of the panel, on the file's own timing.
 *
 * Two ways to get a frame onto the panel:
 *
 *   streamed (default) -- esp_new_jpeg's block mode decodes 16 lines at a time into one of two
 *     small DMA buffers, and each block is sent while the next is decoded. Decoding and the SPI
 *     push overlap, and no whole frame is ever held: a 240x240 frame is 115 KB, more than the
 *     largest free internal block, so the other way keeps it in PSRAM.
 *
 *   whole frame -- decode the frame, then send it. Needed when a dimension is not a multiple of
 *     8 (block mode's requirement), and kept for comparison: `video play <file> frame`.
 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board.h"
#include "quicktime.h"
#include "video_player.h"

namespace {

constexpr size_t PATH_LEN = 160;
constexpr uint32_t TASK_STACK = 8192;
constexpr UBaseType_t TASK_PRIO = 5;
/* Core 1: away from Wi-Fi and the console, which run on core 0. */
constexpr BaseType_t TASK_CORE = 1;
/* Block mode's tallest block: one row of 4:2:0 MCUs. */
constexpr uint32_t BLOCK_LINES = 16;

struct Request {
    char path[PATH_LEN];
    bool loop;
    bool whole_frame;
};

struct Stats {
    char path[PATH_LEN];
    uint32_t width, height, frames;
    double fps;              /* the file's */
    bool streamed;
    bool frame_in_psram;     /* whole-frame mode only */
    uint32_t shown;
    uint32_t late;           /* frames that finished after the next one was due */
    uint32_t errors;
    uint32_t loops;
    int64_t read_us;
    int64_t decode_us;       /* inside the decoder */
    int64_t render_us;       /* decode and draw together: what the frame costs */
    int64_t paint_us;        /* first pixels sent to last pixels out: how long the panel is
                                mid-update, which is what tearing depends on */
    int64_t paint_max_us;
    int64_t started_us, ended_us;
};

char s_base[64];
TaskHandle_t s_task;
volatile bool s_stop;
/* The clip is being stopped for something else to be shown (another clip, a colour), so it
 * leaves the panel on rather than blinking it off and back on. */
volatile bool s_replacing;
esp_timer_handle_t s_timer;
Stats s_stats;               /* the current playback, or the last */

int64_t now_us() { return esp_timer_get_time(); }

void timer_cb(void *)
{
    TaskHandle_t task = s_task;
    if (task != nullptr) {
        xTaskNotifyGive(task);
    }
}

/*
 * Sleep until esp_timer time `due` (us). A one-shot timer rather than vTaskDelay: with a
 * 10 ms tick, 30 fps frames would come out alternately 30 and 40 ms apart.
 */
void sleep_until(int64_t due)
{
    const int64_t wait = due - now_us();
    if (wait < 500) {
        return;
    }
    ulTaskNotifyTake(pdTRUE, 0);   /* nothing stale left over */
    esp_timer_start_once(s_timer, (uint64_t)wait);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

bool is_jpeg(uint32_t codec)
{
    /* ffmpeg's -c:v mjpeg writes 'jpeg'; Motion-JPEG A frames are plain JPEGs too */
    return codec == quicktime::FourCC('j', 'p', 'e', 'g') ||
           codec == quicktime::FourCC('m', 'j', 'p', 'a');
}

bool can_stream(uint32_t w, uint32_t h)
{
    return w % 8 == 0 && h % 8 == 0;
}

/* The whole frame into `fb` (w x h, 16-byte aligned). */
int decode_frame(const uint8_t *jpg, size_t len, uint16_t *fb, uint32_t w, uint32_t h)
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

void resolve(const char *in, char *out, size_t len)
{
    if (in[0] == '/') {
        snprintf(out, len, "%s", in);
    } else {
        snprintf(out, len, "%s/%s", s_base, in);
    }
}

void report(void)
{
    const Stats &s = s_stats;
    const int64_t end = s.ended_us ? s.ended_us : now_us();
    const double secs = (end - s.started_us) / 1e6;
    const uint32_t n = s.shown ? s.shown : 1;
    printf("%s: %" PRIu32 "x%" PRIu32 ", %" PRIu32 " frames at %.2f fps; shown %" PRIu32
           " in %.1f s (%.2f fps)%s\n",
           s.path, s.width, s.height, s.frames, s.fps, s.shown, secs,
           secs > 0 ? s.shown / secs : 0.0, s.loops > 1 ? " over several loops" : "");
    printf("per frame: read %.2f ms, decode %.2f ms, decode+draw %.2f ms of a %.1f ms frame; "
           "paint %.2f ms (max %.2f)\n",
           s.read_us / 1000.0 / n, s.decode_us / 1000.0 / n, s.render_us / 1000.0 / n,
           s.fps > 0 ? 1000.0 / s.fps : 0.0, s.paint_us / 1000.0 / n, s.paint_max_us / 1000.0);
    printf("%" PRIu32 " late, %" PRIu32 " decode errors; %s\n", s.late, s.errors,
           s.streamed ? "streamed in 16-line blocks"
           : s.frame_in_psram ? "whole frame, in PSRAM" : "whole frame, in internal RAM");
}

void play(const Request &req)
{
    FILE *f = fopen(req.path, "rb");
    if (f == nullptr) {
        printf("video: cannot open %s: %s\n", req.path, strerror(errno));
        return;
    }
    /* Frames are read whole, straight into the decoder's input: stdio's buffer would only
     * add a copy. */
    setvbuf(f, nullptr, _IONBF, 0);

    quicktime::QuickTimeFile qt(f);
    const uint32_t w = qt.Width(), h = qt.Height();
    const bool streamed = !req.whole_frame && can_stream(w, h);
    uint8_t *jpg = nullptr;
    uint16_t *fb = nullptr;
    uint8_t *blocks[2] = { nullptr, nullptr };
    const size_t block_len = (size_t)w * BLOCK_LINES * 2;
    bool ok = false;
    if (!qt.IsValid()) {
        printf("video: %s: %s\n", req.path, qt.Error());
    } else if (!is_jpeg(qt.Codec())) {
        printf("video: %s is not Motion-JPEG\n", req.path);
    } else if (w == 0 || h == 0 || w > BOARD_LCD_H_RES || h > BOARD_LCD_V_RES) {
        printf("video: %" PRIu32 "x%" PRIu32 " does not fit the %dx%d panel\n", w, h,
               BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    } else {
        jpg = static_cast<uint8_t *>(heap_caps_aligned_alloc(16, qt.MaxFrameSize(),
                                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (streamed) {
            for (auto &b : blocks) {
                b = static_cast<uint8_t *>(heap_caps_aligned_alloc(16, block_len,
                                                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
            }
            ok = jpg && blocks[0] && blocks[1];
        } else {
            const size_t frame = (size_t)w * h * 2;
            fb = static_cast<uint16_t *>(heap_caps_aligned_alloc(16, frame, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
            if (fb == nullptr) {
                fb = static_cast<uint16_t *>(heap_caps_aligned_alloc(16, frame, MALLOC_CAP_SPIRAM));
            }
            ok = jpg && fb;
        }
        if (!ok) {
            printf("video: out of memory for the decode buffers\n");
        }
    }
    if (!ok) {
        heap_caps_free(jpg);
        heap_caps_free(fb);
        heap_caps_free(blocks[0]);
        heap_caps_free(blocks[1]);
        fclose(f);
        return;
    }

    Stats &s = s_stats;
    s = {};
    strlcpy(s.path, req.path, sizeof(s.path));
    s.width = w;
    s.height = h;
    s.frames = (uint32_t)qt.FrameCount();
    s.fps = qt.DurationUs() ? s.frames * 1e6 / qt.DurationUs() : 0;
    s.streamed = streamed;
    s.frame_in_psram = fb != nullptr && !esp_ptr_internal(fb);

    /* Blanked, then on -- the panel lit and showing black around the clip's square -- before
     * the first frame is drawn or the clip's clock starts, so frame 0 is seen. */
    board_lcd_power_on(0x0000);
    const int x = (BOARD_LCD_H_RES - (int)w) / 2, y = (BOARD_LCD_V_RES - (int)h) / 2;
    printf("video: playing %s (%" PRIu32 "x%" PRIu32 ", %" PRIu32 " frames, %.2f fps, %s)%s\n",
           req.path, w, h, s.frames, s.fps, streamed ? "streamed" : "whole frame",
           req.loop ? ", looping" : "");

    s.started_us = now_us();
    const uint32_t scale = qt.TimeScale();
    do {
        /* Each frame is due at the clip's own time for it, from the start of this pass. */
        const int64_t pass_start = now_us();
        uint64_t ticks = 0;
        for (size_t i = 0; i < s.frames && !s_stop; i++) {
            const int64_t t0 = now_us();
            const ssize_t n = qt.GetFrame(i, jpg, qt.MaxFrameSize());
            const int64_t t1 = now_us();
            if (n <= 0) {
                printf("video: cannot read frame %u of %s\n", (unsigned)i, req.path);
                s_stop = true;
                break;
            }

            int rc;
            int64_t paint_start = 0, paint_end = 0;
            if (streamed) {
                board_lcd_stream_begin();
                rc = decode_blocks(jpg, (size_t)n, w, h, blocks, block_len, &s.decode_us,
                    [](int) {
                        /* the buffer about to be refilled went out two blocks ago */
                        board_lcd_stream_wait(1);
                    },
                    [&](uint32_t row, uint32_t lines, const uint16_t *pixels) {
                        if (paint_start == 0) {
                            paint_start = now_us();
                        }
                        board_lcd_stream_block(x, y + (int)row, (int)w, (int)lines, pixels);
                    });
                board_lcd_stream_end();
                paint_end = now_us();
            } else {
                const int64_t d0 = now_us();
                rc = decode_frame(jpg, (size_t)n, fb, w, h);
                s.decode_us += now_us() - d0;
                if (rc == 0) {
                    paint_start = now_us();
                    board_lcd_draw(x, y, (int)w, (int)h, fb);
                    paint_end = now_us();
                }
            }
            const int64_t t2 = now_us();
            if (rc != 0 && s.errors++ == 0) {
                printf("video: frame %u: decode error %d\n", (unsigned)i, rc);
            }
            s.read_us += t1 - t0;
            s.render_us += t2 - t1;
            if (paint_start != 0) {
                const int64_t paint = paint_end - paint_start;
                s.paint_us += paint;
                s.paint_max_us = paint > s.paint_max_us ? paint : s.paint_max_us;
            }
            s.shown++;

            ticks += qt.FrameDelta(i);
            const int64_t due = pass_start + (int64_t)(ticks * 1000000ULL / scale);
            if (t2 > due) {
                s.late++;
            }
            sleep_until(due);
        }
        s.loops++;
    } while (req.loop && !s_stop);
    s.ended_us = now_us();

    /* Nothing showing any more: panel asleep, backlight off -- unless something else is about
     * to be shown in its place. */
    if (!s_replacing) {
        board_lcd_power_off();
    }
    report();
    heap_caps_free(jpg);
    heap_caps_free(fb);
    heap_caps_free(blocks[0]);
    heap_caps_free(blocks[1]);
    fclose(f);
}

void player_task(void *arg)
{
    Request *req = static_cast<Request *>(arg);
    play(*req);
    delete req;
    s_task = nullptr;
    vTaskDelete(nullptr);
}

/*
 * `video verify <file> [step]`: decode every step-th frame both ways -- in blocks, gathered
 * into a frame, and whole -- and compare them byte for byte. Block mode should change how the
 * pixels arrive, never what they are.
 */
int verify(const char *path, int step)
{
    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        printf("video: cannot open %s: %s\n", path, strerror(errno));
        return 1;
    }
    quicktime::QuickTimeFile qt(f);
    const uint32_t w = qt.Width(), h = qt.Height();
    if (!qt.IsValid() || !is_jpeg(qt.Codec()) || !can_stream(w, h) || w > 1024 || h > 1024) {
        printf("video: %s: %s\n", path, qt.IsValid() ? "not a Motion-JPEG clip block mode can decode"
                                                     : qt.Error());
        fclose(f);
        return 1;
    }
    const size_t frame = (size_t)w * h * 2, block_len = (size_t)w * BLOCK_LINES * 2;
    uint8_t *jpg = static_cast<uint8_t *>(heap_caps_aligned_alloc(16, qt.MaxFrameSize(), MALLOC_CAP_8BIT));
    uint8_t *whole = static_cast<uint8_t *>(heap_caps_aligned_alloc(16, frame, MALLOC_CAP_8BIT));
    uint8_t *gathered = static_cast<uint8_t *>(heap_caps_aligned_alloc(16, frame, MALLOC_CAP_8BIT));
    uint8_t *blocks[2] = {
        static_cast<uint8_t *>(heap_caps_aligned_alloc(16, block_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<uint8_t *>(heap_caps_aligned_alloc(16, block_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
    };
    int checked = 0, differ = 0, failed = 0;
    if (jpg && whole && gathered && blocks[0] && blocks[1]) {
        for (size_t i = 0; i < (size_t)qt.FrameCount(); i += (size_t)step) {
            const ssize_t n = qt.GetFrame(i, jpg, qt.MaxFrameSize());
            int64_t unused = 0;
            memset(whole, 0x55, frame);
            memset(gathered, 0xAA, frame);   /* different poison: unwritten rows cannot match */
            const int rc1 = n > 0 ? decode_frame(jpg, (size_t)n, reinterpret_cast<uint16_t *>(whole), w, h) : -1;
            const int rc2 = n > 0 ? decode_blocks(jpg, (size_t)n, w, h, blocks, block_len, &unused,
                [](int) {},
                [&](uint32_t row, uint32_t lines, const uint16_t *pixels) {
                    memcpy(gathered + (size_t)row * w * 2, pixels, (size_t)lines * w * 2);
                }) : -1;
            checked++;
            if (rc1 != 0 || rc2 != 0) {
                failed++;
                printf("frame %u: decode failed (whole %d, blocks %d)\n", (unsigned)i, rc1, rc2);
            } else if (memcmp(whole, gathered, frame) != 0) {
                differ++;
                uint32_t first = 0;
                while (first < h && memcmp(whole + first * w * 2, gathered + first * w * 2, w * 2) == 0) {
                    first++;
                }
                printf("frame %u: differs from row %" PRIu32 "\n", (unsigned)i, first);
            }
        }
        printf("%s: %d frames checked (every %d), %d differ, %d failed to decode\n", path, checked,
               step, differ, failed);
    } else {
        printf("video: out of memory\n");
        failed = 1;
    }
    heap_caps_free(jpg);
    heap_caps_free(whole);
    heap_caps_free(gathered);
    heap_caps_free(blocks[0]);
    heap_caps_free(blocks[1]);
    fclose(f);
    return differ || failed ? 1 : 0;
}

/* ------------------------------------------------------------------ the screen */

/* The colour on show, if one is (and no clip). */
bool s_colour_shown;
uint8_t s_colour_rgb[3];

uint16_t rgb565(int r, int g, int b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/*
 * A colour: a name, #RRGGBB (or RRGGBB), R,G,B in decimal as the Flash_PNG sketch's RGB command
 * took it, or 0x followed by a raw RGB565 value. The 8-bit form is kept for `screen` to report.
 */
bool parse_colour(const char *s, uint16_t *out, uint8_t rgb[3])
{
    static const struct {
        const char *name;
        uint8_t r, g, b;
    } names[] = {
        { "black", 0, 0, 0 },       { "white", 255, 255, 255 }, { "red", 255, 0, 0 },
        { "green", 0, 255, 0 },     { "blue", 0, 0, 255 },      { "yellow", 255, 255, 0 },
        { "cyan", 0, 255, 255 },    { "magenta", 255, 0, 255 }, { "orange", 255, 128, 0 },
        { "purple", 128, 0, 255 },  { "pink", 255, 105, 180 },  { "grey", 128, 128, 128 },
        { "gray", 128, 128, 128 },
    };
    for (const auto &n : names) {
        if (strcasecmp(s, n.name) == 0) {
            rgb[0] = n.r;
            rgb[1] = n.g;
            rgb[2] = n.b;
            *out = rgb565(n.r, n.g, n.b);
            return true;
        }
    }
    unsigned r, g, b;
    char tail;
    if (sscanf(s, "%u,%u,%u%c", &r, &g, &b, &tail) == 3 && r < 256 && g < 256 && b < 256) {
        rgb[0] = (uint8_t)r;
        rgb[1] = (uint8_t)g;
        rgb[2] = (uint8_t)b;
        *out = rgb565((int)r, (int)g, (int)b);
        return true;
    }
    if (strncasecmp(s, "0x", 2) == 0) {
        char *end = nullptr;
        const unsigned long v = strtoul(s, &end, 16);
        if (*end == '\0' && v <= 0xFFFF) {
            *out = (uint16_t)v;
            rgb[0] = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
            rgb[1] = (uint8_t)(((v >> 5) & 0x3F) * 255 / 63);
            rgb[2] = (uint8_t)((v & 0x1F) * 255 / 31);
            return true;
        }
    }
    const char *hex = s[0] == '#' ? s + 1 : s;
    if (strlen(hex) == 6 && strspn(hex, "0123456789abcdefABCDEF") == 6) {
        const unsigned long v = strtoul(hex, nullptr, 16);
        rgb[0] = (uint8_t)(v >> 16);
        rgb[1] = (uint8_t)(v >> 8);
        rgb[2] = (uint8_t)v;
        *out = rgb565(rgb[0], rgb[1], rgb[2]);
        return true;
    }
    return false;
}

int cmd_screen(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "";
    if (argc == 1 || strcmp(sub, "status") == 0) {
        if (video_playing()) {
            printf("playing %s (backlight %d%%)\n", s_stats.path, board_lcd_get_backlight());
        } else if (s_colour_shown && board_lcd_powered()) {
            printf("showing #%02x%02x%02x (backlight %d%%)\n", s_colour_rgb[0], s_colour_rgb[1],
                   s_colour_rgb[2], board_lcd_get_backlight());
        } else {
            printf("off: panel asleep, backlight off\n");
        }
        return 0;
    }
    if ((strcmp(sub, "colour") == 0 || strcmp(sub, "color") == 0) && argc >= 3) {
        /* "255 128 0" as three words is the same as "255,128,0" */
        char joined[32];
        if (argc == 5) {
            snprintf(joined, sizeof(joined), "%s,%s,%s", argv[2], argv[3], argv[4]);
        } else {
            strlcpy(joined, argv[2], sizeof(joined));
        }
        uint16_t c;
        uint8_t rgb[3];
        if (!parse_colour(joined, &c, rgb)) {
            printf("screen: a colour is a name (red, orange, ...), #RRGGBB, R,G,B or 0xRGB565\n");
            return 1;
        }
        const esp_err_t err = screen_show_colour(c);
        if (err == ESP_OK) {
            memcpy(s_colour_rgb, rgb, sizeof(s_colour_rgb));
        }
        return err == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "clear") == 0 || strcmp(sub, "off") == 0) {
        return screen_clear() == ESP_OK ? 0 : 1;
    }
    printf("usage: screen [colour <name|#RRGGBB|R,G,B|0xRGB565> | clear]\n");
    return 1;
}

/* ------------------------------------------------------------------ console */

int cmd_video(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "";
    char path[PATH_LEN];
    if (strcmp(sub, "play") == 0 && argc >= 3) {
        bool loop = false, whole = false;
        for (int i = 3; i < argc; i++) {
            loop |= strcmp(argv[i], "loop") == 0;
            whole |= strcmp(argv[i], "frame") == 0;
        }
        resolve(argv[2], path, sizeof(path));
        return video_play(path, loop, whole) == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "stop") == 0) {
        const bool was = video_playing();
        video_stop();
        printf(was ? "stopped\n" : "nothing playing\n");
        return 0;
    }
    if (strcmp(sub, "status") == 0) {
        if (s_stats.started_us == 0) {
            printf("nothing played yet\n");
        } else {
            printf("%s\n", video_playing() ? "playing:" : "last played:");
            report();
        }
        return 0;
    }
    if (strcmp(sub, "info") == 0 && argc >= 3) {
        resolve(argv[2], path, sizeof(path));
        FILE *f = fopen(path, "rb");
        if (f == nullptr) {
            printf("video: cannot open %s: %s\n", path, strerror(errno));
            return 1;
        }
        const int64_t t0 = now_us();
        quicktime::QuickTimeFile qt(f);
        const int64_t parse_us = now_us() - t0;
        qt.Describe(path);
        if (qt.IsValid()) {
            printf("parsed in %.1f ms\n", parse_us / 1000.0);
        }
        fclose(f);
        return qt.IsValid() ? 0 : 1;
    }
    if (strcmp(sub, "verify") == 0 && argc >= 3) {
        const int step = argc > 3 ? atoi(argv[3]) : 50;
        resolve(argv[2], path, sizeof(path));
        return verify(path, step > 0 ? step : 50);
    }
    printf("usage: video play <file> [loop] [frame] | stop | status | info <file> | verify <file> [step]\n");
    return 1;
}

}  // namespace

extern "C" esp_err_t screen_show_colour(uint16_t rgb)
{
    /* Over a clip, the panel stays on; from sleep, it wakes with the colour already in place. */
    s_replacing = true;
    video_stop();
    s_replacing = false;
    const esp_err_t err = board_lcd_power_on(rgb);
    s_colour_shown = err == ESP_OK;
    if (s_colour_shown) {
        s_colour_rgb[0] = (uint8_t)(((rgb >> 11) & 0x1F) * 255 / 31);
        s_colour_rgb[1] = (uint8_t)(((rgb >> 5) & 0x3F) * 255 / 63);
        s_colour_rgb[2] = (uint8_t)((rgb & 0x1F) * 255 / 31);
    }
    return err;
}

extern "C" esp_err_t screen_clear(void)
{
    video_stop();
    s_colour_shown = false;
    return board_lcd_power_off();
}

extern "C" esp_err_t video_play(const char *path, bool loop, bool whole_frame)
{
    s_replacing = true;
    video_stop();
    s_replacing = false;
    s_colour_shown = false;
    Request *req = new Request{};
    strlcpy(req->path, path, sizeof(req->path));
    req->loop = loop;
    req->whole_frame = whole_frame;
    s_stop = false;
    if (xTaskCreatePinnedToCore(player_task, "video", TASK_STACK, req, TASK_PRIO, &s_task,
                                TASK_CORE) != pdPASS) {
        delete req;
        s_task = nullptr;
        printf("video: cannot start the playback task\n");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

extern "C" void video_stop(void)
{
    if (s_task == nullptr) {
        return;
    }
    s_stop = true;
    for (int i = 0; i < 200 && s_task != nullptr; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

extern "C" bool video_playing(void)
{
    return s_task != nullptr;
}

extern "C" void register_video_commands(const char *base_path)
{
    strlcpy(s_base, base_path, sizeof(s_base));
    const esp_timer_create_args_t timer_args = {
        .callback = timer_cb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "video",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_timer));

    esp_console_cmd_t cmd = {};
    cmd.command = "video";
    cmd.help = "Play a Motion-JPEG QuickTime file on the panel (streamed in blocks, or `frame` "
               "for whole frames), stop it, describe one, or check block decoding against whole";
    cmd.hint = "play <file> [loop] [frame] | stop | status | info <file> | verify <file> [step]";
    cmd.func = cmd_video;
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    esp_console_cmd_t screen = {};
    screen.command = "screen";
    screen.help = "What is on the screen: a solid colour, or clear it (panel asleep, backlight "
                  "off); alone, what is showing. The screen is off whenever nothing is.";
    screen.hint = "[colour <name|#RRGGBB|R,G,B|0xRGB565> | clear]";
    screen.func = cmd_screen;
    ESP_ERROR_CHECK(esp_console_cmd_register(&screen));
}
