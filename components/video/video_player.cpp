/*
 * MJPEG QuickTime playback: each frame read from storage, decoded with esp_new_jpeg into an
 * RGB565 frame and put in the middle of the panel, on the file's own timing.
 *
 * One task does all three in turn. At 120x120 that is ~5 ms of a 33 ms frame (decode 1.8 ms,
 * the SPI push 2.9 ms), so there is nothing to gain yet from overlapping them.
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

struct Request {
    char path[PATH_LEN];
    bool loop;
};

struct Stats {
    char path[PATH_LEN];
    uint32_t width, height, frames;
    double fps;              /* the file's */
    uint32_t shown;
    uint32_t late;           /* frames that finished after the next one was due */
    uint32_t errors;
    uint32_t loops;
    int64_t read_us, decode_us, draw_us;
    int64_t started_us, ended_us;
    bool frame_in_psram;
};

char s_base[64];
TaskHandle_t s_task;
volatile bool s_stop;
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
    const int64_t wait = due - esp_timer_get_time();
    if (wait < 500) {
        return;
    }
    ulTaskNotifyTake(pdTRUE, 0);   /* nothing stale left over */
    esp_timer_start_once(s_timer, (uint64_t)wait);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

int decode(const uint8_t *jpg, size_t len, uint16_t *fb, uint32_t w, uint32_t h)
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
    io.outbuf = reinterpret_cast<uint8_t *>(fb);   /* 16-byte aligned, as it requires */
    jpeg_dec_header_info_t info;
    err = jpeg_dec_parse_header(dec, &io, &info);
    if (err == JPEG_ERR_OK) {
        err = (info.width == w && info.height == h) ? jpeg_dec_process(dec, &io)
                                                    : JPEG_ERR_INVALID_PARAM;
    }
    jpeg_dec_close(dec);
    return err;
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
    const int64_t end = s.ended_us ? s.ended_us : esp_timer_get_time();
    const double secs = (end - s.started_us) / 1e6;
    const uint32_t n = s.shown ? s.shown : 1;
    printf("%s: %" PRIu32 "x%" PRIu32 ", %" PRIu32 " frames at %.2f fps; shown %" PRIu32
           " in %.1f s (%.2f fps)%s\n",
           s.path, s.width, s.height, s.frames, s.fps, s.shown, secs,
           secs > 0 ? s.shown / secs : 0.0, s.loops > 1 ? " over several loops" : "");
    printf("per frame: read %.2f ms, decode %.2f ms, draw %.2f ms; %" PRIu32 " late, %" PRIu32
           " decode errors%s\n",
           s.read_us / 1000.0 / n, s.decode_us / 1000.0 / n, s.draw_us / 1000.0 / n, s.late,
           s.errors, s.frame_in_psram ? "; frame buffer in PSRAM" : "");
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
    uint8_t *jpg = nullptr;
    uint16_t *fb = nullptr;
    const uint32_t w = qt.Width(), h = qt.Height();
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
        const size_t frame = (size_t)w * h * 2;
        fb = static_cast<uint16_t *>(heap_caps_aligned_alloc(16, frame, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
        if (fb == nullptr) {
            fb = static_cast<uint16_t *>(heap_caps_aligned_alloc(16, frame, MALLOC_CAP_SPIRAM));
        }
        if (jpg == nullptr || fb == nullptr) {
            printf("video: no memory for a %u byte frame\n", (unsigned)frame);
        }
    }
    if (jpg == nullptr || fb == nullptr) {
        heap_caps_free(jpg);
        heap_caps_free(fb);
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
    s.frame_in_psram = !esp_ptr_internal(fb);

    board_lcd_cycle(false);
    board_lcd_fill(0x0000);
    const int x = (BOARD_LCD_H_RES - (int)w) / 2, y = (BOARD_LCD_V_RES - (int)h) / 2;
    printf("video: playing %s (%" PRIu32 "x%" PRIu32 ", %" PRIu32 " frames, %.2f fps)%s\n",
           req.path, w, h, s.frames, s.fps, req.loop ? ", looping" : "");

    s.started_us = esp_timer_get_time();
    const uint32_t scale = qt.TimeScale();
    do {
        /* Each frame is due at the clip's own time for it, from the start of this pass. */
        const int64_t pass_start = esp_timer_get_time();
        uint64_t ticks = 0;
        for (size_t i = 0; i < s.frames && !s_stop; i++) {
            const int64_t t0 = esp_timer_get_time();
            const ssize_t n = qt.GetFrame(i, jpg, qt.MaxFrameSize());
            const int64_t t1 = esp_timer_get_time();
            if (n <= 0) {
                printf("video: cannot read frame %u of %s\n", (unsigned)i, req.path);
                s_stop = true;
                break;
            }
            const int rc = decode(jpg, (size_t)n, fb, w, h);
            const int64_t t2 = esp_timer_get_time();
            if (rc == 0) {
                board_lcd_draw(x, y, (int)w, (int)h, fb);
            } else if (s.errors++ == 0) {
                printf("video: frame %u: decode error %d (showing the frame before)\n", (unsigned)i, rc);
            }
            const int64_t t3 = esp_timer_get_time();
            s.read_us += t1 - t0;
            s.decode_us += t2 - t1;
            s.draw_us += t3 - t2;
            s.shown++;

            ticks += qt.FrameDelta(i);
            const int64_t due = pass_start + (int64_t)(ticks * 1000000ULL / scale);
            if (t3 > due) {
                s.late++;
            }
            sleep_until(due);
        }
        s.loops++;
    } while (req.loop && !s_stop);
    s.ended_us = esp_timer_get_time();

    board_lcd_fill(0x0000);
    report();
    heap_caps_free(jpg);
    heap_caps_free(fb);
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

/* ------------------------------------------------------------------ console */

int cmd_video(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "";
    char path[PATH_LEN];
    if (strcmp(sub, "play") == 0 && argc >= 3) {
        const bool loop = argc > 3 && strcmp(argv[3], "loop") == 0;
        resolve(argv[2], path, sizeof(path));
        return video_play(path, loop) == ESP_OK ? 0 : 1;
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
        const int64_t t0 = esp_timer_get_time();
        quicktime::QuickTimeFile qt(f);
        const int64_t parse_us = esp_timer_get_time() - t0;
        qt.Describe(path);
        if (qt.IsValid()) {
            printf("parsed in %.1f ms\n", parse_us / 1000.0);
        }
        fclose(f);
        return qt.IsValid() ? 0 : 1;
    }
    printf("usage: video play <file> [loop] | stop | status | info <file>\n");
    return 1;
}

}  // namespace

extern "C" esp_err_t video_play(const char *path, bool loop)
{
    video_stop();
    Request *req = new Request{};
    strlcpy(req->path, path, sizeof(req->path));
    req->loop = loop;
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
    cmd.help = "Play a Motion-JPEG QuickTime file on the panel (the Flash_PNG clips), stop it, "
               "or describe one";
    cmd.hint = "play <file> [loop] | stop | status | info <file>";
    cmd.func = cmd_video;
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
