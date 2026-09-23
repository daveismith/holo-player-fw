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
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board.h"
#include "image.h"
#include "jpeg_decode.h"
#include "quicktime.h"
#include "screen_state.h"
#include "video_player.h"

namespace {

using jpg::can_stream;
using jpg::decode_blocks;
using jpg::decode_frame;
using jpg::now_us;

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

/*
 * What the panel is showing, and the detail `screen` needs to name it. Only one of these can
 * be true at a time: each way of drawing takes the panel from the last.
 */
enum class Showing { Nothing, Colour, Calibration, Image };
Showing s_showing = Showing::Nothing;
uint8_t s_colour_rgb[3];         /* Showing::Colour */
char s_image_path[PATH_LEN];     /* Showing::Image */
uint32_t s_image_w, s_image_h;

uint16_t rgb565(int r, int g, int b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/*
 * A colour's words: anything board_parse_rgb_args() takes (a name, #RRGGBB, R,G,B or R G B),
 * or -- the panel's own form -- 0x and a raw RGB565 value. The 8-bit form is kept for
 * `screen` to report.
 */
bool parse_colour(int argc, char **argv, uint16_t *out, uint8_t rgb[3])
{
    if (argc == 1 && strncasecmp(argv[0], "0x", 2) == 0) {
        char *end = nullptr;
        const unsigned long v = strtoul(argv[0], &end, 16);
        if (*end != '\0' || v > 0xFFFF) {
            return false;
        }
        *out = (uint16_t)v;
        rgb[0] = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
        rgb[1] = (uint8_t)(((v >> 5) & 0x3F) * 255 / 63);
        rgb[2] = (uint8_t)((v & 0x1F) * 255 / 31);
        return true;
    }
    if (!board_parse_rgb_args(argc, argv, rgb)) {
        return false;
    }
    *out = rgb565(rgb[0], rgb[1], rgb[2]);
    return true;
}

/*
 * The calibration pattern: a crosshair and, just above the crossing, an arrowhead pointing up.
 * It is for lining the panel up behind a dome's lens -- centred on the opening, and the right
 * way round -- so what matters is that the lines are exactly centred and the arrow unmistakable.
 *
 * The panel is 240 pixels across, so its centre falls on the seam between pixels 119 and 120
 * rather than on a pixel. Everything here is an even width straddling that seam, which is the
 * only way the pattern is symmetric about the real centre.
 */
constexpr int CALIB_LINE = 2;        /* line thickness, even */
constexpr int CALIB_ARROW_W = 36;    /* arrowhead, even for the same reason */
constexpr int CALIB_ARROW_H = 34;
constexpr int CALIB_ARROW_GAP = 4;   /* blank rows between the arrowhead's base and the line */
constexpr uint16_t CALIB_BG = 0x0000;
constexpr uint16_t CALIB_FG = 0xFFFF;

/* The panel takes pixels high byte first; these buffers are little-endian memory. */
constexpr uint16_t be16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

esp_err_t draw_calibration()
{
    /* The pixel just past the centre seam: the line runs from mid - 1 to mid. */
    constexpr int mid = BOARD_LCD_H_RES / 2;
    constexpr size_t line_px = (size_t)BOARD_LCD_H_RES * CALIB_LINE;
    constexpr size_t arrow_px = (size_t)CALIB_ARROW_W * CALIB_ARROW_H;
    constexpr size_t buf_px = line_px > arrow_px ? line_px : arrow_px;

    /* Internal DMA memory, so board_lcd_draw() sends it straight out. Under 3 KB. */
    uint16_t *buf = (uint16_t *)heap_caps_malloc(buf_px * sizeof(uint16_t),
                                                 MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buf == nullptr) {
        printf("screen: out of memory\n");
        return ESP_ERR_NO_MEM;
    }

    /* Black over the whole panel first -- it wakes with it already in place -- so nothing of
     * what was showing survives around the lines. */
    esp_err_t err = board_lcd_power_on(CALIB_BG);

    for (size_t i = 0; i < line_px; i++) {
        buf[i] = be16(CALIB_FG);
    }
    if (err == ESP_OK) {
        err = board_lcd_draw(0, mid - CALIB_LINE / 2, BOARD_LCD_H_RES, CALIB_LINE, buf);
    }
    if (err == ESP_OK) {
        err = board_lcd_draw(mid - CALIB_LINE / 2, 0, CALIB_LINE, BOARD_LCD_V_RES, buf);
    }

    /* A filled triangle, apex row first: each row is as wide as its distance from the apex,
     * out to the full width at the base. It sits astride the vertical line, which reads as
     * the arrow's shaft. */
    for (size_t i = 0; i < arrow_px; i++) {
        buf[i] = be16(CALIB_BG);
    }
    for (int row = 0; row < CALIB_ARROW_H; row++) {
        int half = (row + 1) * (CALIB_ARROW_W / 2) / CALIB_ARROW_H;
        if (half < 1) {
            half = 1;
        }
        for (int x = CALIB_ARROW_W / 2 - half; x < CALIB_ARROW_W / 2 + half; x++) {
            buf[(size_t)row * CALIB_ARROW_W + x] = be16(CALIB_FG);
        }
    }
    if (err == ESP_OK) {
        err = board_lcd_draw(mid - CALIB_ARROW_W / 2,
                             mid - CALIB_LINE / 2 - CALIB_ARROW_GAP - CALIB_ARROW_H,
                             CALIB_ARROW_W, CALIB_ARROW_H, buf);
    }

    heap_caps_free(buf);
    if (err != ESP_OK) {
        printf("screen: %s\n", esp_err_to_name(err));
    }
    return err;
}

int cmd_screen(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "";
    if (argc == 1 || strcmp(sub, "status") == 0) {
        if (video_playing()) {
            printf("playing %s (backlight %d%%)\n", s_stats.path, board_lcd_get_backlight());
        } else if (s_showing == Showing::Calibration && board_lcd_powered()) {
            printf("showing the calibration pattern (backlight %d%%)\n",
                   board_lcd_get_backlight());
        } else if (s_showing == Showing::Colour && board_lcd_powered()) {
            printf("showing #%02x%02x%02x (backlight %d%%)\n", s_colour_rgb[0], s_colour_rgb[1],
                   s_colour_rgb[2], board_lcd_get_backlight());
        } else if (s_showing == Showing::Image && board_lcd_powered()) {
            printf("showing %s (%" PRIu32 "x%" PRIu32 ", backlight %d%%)\n", s_image_path,
                   s_image_w, s_image_h, board_lcd_get_backlight());
        } else {
            printf("off: panel asleep, backlight off\n");
        }
        return 0;
    }
    if ((strcmp(sub, "colour") == 0 || strcmp(sub, "color") == 0) && argc >= 3) {
        uint16_t c;
        uint8_t rgb[3];
        if (!parse_colour(argc - 2, argv + 2, &c, rgb)) {
            printf("screen: a colour is a name (red, orange, ...), #RRGGBB, R,G,B or 0xRGB565\n");
            return 1;
        }
        const esp_err_t err = screen_show_colour(c);
        if (err == ESP_OK) {
            memcpy(s_colour_rgb, rgb, sizeof(s_colour_rgb));
        }
        return err == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "calibration") == 0 || strcmp(sub, "calib") == 0) {
        return screen_show_calibration() == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "clear") == 0 || strcmp(sub, "off") == 0) {
        return screen_clear() == ESP_OK ? 0 : 1;
    }
    printf("usage: screen [colour <name|#RRGGBB|R,G,B|0xRGB565> | calibration | clear]\n");
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

extern "C" void screen_take_panel(void)
{
    /* Over a clip the panel stays on, so what replaces it appears in its place rather than
     * after a blink; from sleep the caller wakes the panel itself. */
    s_replacing = true;
    video_stop();
    s_replacing = false;
    s_showing = Showing::Nothing;
}

extern "C" void screen_set_image(const char *path, uint32_t w, uint32_t h)
{
    strlcpy(s_image_path, path, sizeof(s_image_path));
    s_image_w = w;
    s_image_h = h;
    s_showing = Showing::Image;
}

extern "C" void screen_resolve_path(const char *in, char *out, size_t len)
{
    resolve(in, out, len);
}

extern "C" esp_err_t screen_show_colour(uint16_t rgb)
{
    /* Over a clip, the panel stays on; from sleep, it wakes with the colour already in place. */
    screen_take_panel();
    const esp_err_t err = board_lcd_power_on(rgb);
    s_showing = err == ESP_OK ? Showing::Colour : Showing::Nothing;
    if (s_showing == Showing::Colour) {
        s_colour_rgb[0] = (uint8_t)(((rgb >> 11) & 0x1F) * 255 / 31);
        s_colour_rgb[1] = (uint8_t)(((rgb >> 5) & 0x3F) * 255 / 63);
        s_colour_rgb[2] = (uint8_t)((rgb & 0x1F) * 255 / 31);
    }
    return err;
}

extern "C" esp_err_t screen_show_calibration(void)
{
    /* As for a colour: over a clip the panel stays on, and from sleep it wakes already black. */
    screen_take_panel();
    const esp_err_t err = draw_calibration();
    s_showing = err == ESP_OK ? Showing::Calibration : Showing::Nothing;
    return err;
}

extern "C" esp_err_t screen_clear(void)
{
    video_stop();
    s_showing = Showing::Nothing;
    return board_lcd_power_off();
}

extern "C" esp_err_t video_play(const char *path, bool loop, bool whole_frame)
{
    screen_take_panel();
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
    screen.help = "What is on the screen: a solid colour, the alignment crosshair, or clear it "
                  "(panel asleep, backlight off); alone, what is showing, including a clip or "
                  "an image. The screen is off whenever nothing is.";
    screen.hint = "[colour <name|#RRGGBB|R,G,B|0xRGB565> | calibration | clear]";
    screen.func = cmd_screen;
    ESP_ERROR_CHECK(esp_console_cmd_register(&screen));

    register_image_command();
}
