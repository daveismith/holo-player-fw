/*
 * `jpeg bench <file> [runs] [reference.rgb565]` -- every decoder on the same JPEG, from RAM into
 * the same RGB565 frame, timed per image (setup included), then checked against a reference
 * decode: PSNR, the largest channel error, and pixels more than 32 off.
 *
 * `jpeg show <jpegdec|new|tjpgd> <file>` -- decode and put it on the panel, centred.
 * `jpeg save <jpegdec|new|tjpgd> <file> <out.rgb565>` -- decode and write the frame to storage.
 *
 * The frame is internal, DMA-capable RAM when a block that size is free, else PSRAM; the bench
 * says which, since writing PSRAM is part of what gets timed.
 */
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "board.h"
#include "jpeg_bench.h"
#include "jpeg_bench_priv.h"

#define PATH_LEN     160
#define DEFAULT_RUNS 50
#define MAX_RUNS     1000
#define BAD_ERROR    32     /* a channel this far off is a wrong pixel, not a rounding one */
#define BENCH_PRIO   10     /* above the LCD cycle and the network tasks' usual load */

typedef int (*decode_fn)(const uint8_t *jpg, size_t len, uint16_t *fb, int w, int h);

typedef struct {
    const char *key;
    const char *(*label)(void);
    decode_fn decode;
} decoder_t;

static const decoder_t k_decoders[] = {
    { "jpegdec", bench_jpegdec_label, bench_jpegdec_decode },
    { "new", bench_new_jpeg_label, bench_new_jpeg_decode },
    { "tjpgd", bench_esp_jpeg_label, bench_esp_jpeg_decode },
};
#define DECODER_COUNT (sizeof(k_decoders) / sizeof(k_decoders[0]))

static char s_base[64];

static void resolve(const char *in, char *out, size_t len)
{
    if (in[0] == '/') {
        snprintf(out, len, "%s", in);
    } else {
        snprintf(out, len, "%s/%s", s_base, in);
    }
}

/* The whole file, in memory with `caps`. */
static uint8_t *load(const char *arg, size_t *len_out, uint32_t caps)
{
    char path[PATH_LEN];
    resolve(arg, path, sizeof(path));
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0) {
        printf("jpeg: cannot read %s\n", path);
        return NULL;
    }
    uint8_t *buf = heap_caps_aligned_alloc(16, (size_t)st.st_size, caps);
    if (buf == NULL) {
        printf("jpeg: no memory for %ld bytes\n", (long)st.st_size);
        return NULL;
    }
    int fd = open(path, O_RDONLY);
    size_t got = 0;
    ssize_t n;
    while (fd >= 0 && got < (size_t)st.st_size && (n = read(fd, buf + got, st.st_size - got)) > 0) {
        got += (size_t)n;
    }
    if (fd >= 0) {
        close(fd);
    }
    if (got != (size_t)st.st_size) {
        printf("jpeg: short read of %s\n", path);
        heap_caps_free(buf);
        return NULL;
    }
    *len_out = got;
    return buf;
}

/* Width and height from the first SOF marker, without asking any of the decoders. */
static bool jpeg_size(const uint8_t *p, size_t len, int *w, int *h)
{
    size_t i = 2;
    while (i + 9 < len) {
        if (p[i] != 0xFF) {
            return false;
        }
        const uint8_t m = p[i + 1];
        const size_t seg = ((size_t)p[i + 2] << 8) | p[i + 3];
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            *h = (p[i + 5] << 8) | p[i + 6];
            *w = (p[i + 7] << 8) | p[i + 8];
            return true;
        }
        i += 2 + seg;
    }
    return false;
}

/* 16-byte aligned (esp_new_jpeg requires it): internal and DMA-capable if there is room. */
static uint16_t *alloc_frame(size_t bytes)
{
    uint16_t *fb = heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (fb == NULL) {
        fb = heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_SPIRAM);
    }
    return fb;
}

static const decoder_t *find_decoder(const char *key)
{
    for (size_t d = 0; d < DECODER_COUNT; d++) {
        if (strcmp(key, k_decoders[d].key) == 0) {
            return &k_decoders[d];
        }
    }
    printf("jpeg: decoder is jpegdec, new or tjpgd\n");
    return NULL;
}

static void expand(const uint8_t *be, int *r, int *g, int *b)
{
    const unsigned v = ((unsigned)be[0] << 8) | be[1];
    const unsigned r5 = v >> 11, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
    *r = (int)((r5 << 3) | (r5 >> 2));
    *g = (int)((g6 << 2) | (g6 >> 4));
    *b = (int)((b5 << 3) | (b5 >> 2));
}

static void compare(const uint8_t *out, const uint8_t *ref, int pixels, double *psnr, int *max_err, int *bad)
{
    uint64_t sq = 0;
    int worst = 0, wrong = 0;
    for (int i = 0; i < pixels; i++) {
        int r1, g1, b1, r2, g2, b2;
        expand(out + 2 * i, &r1, &g1, &b1);
        expand(ref + 2 * i, &r2, &g2, &b2);
        const int dr = abs(r1 - r2), dg = abs(g1 - g2), db = abs(b1 - b2);
        sq += (uint64_t)(dr * dr + dg * dg + db * db);
        const int m = dr > dg ? (dr > db ? dr : db) : (dg > db ? dg : db);
        worst = m > worst ? m : worst;
        wrong += m > BAD_ERROR;
    }
    const double mse = (double)sq / (3.0 * pixels);
    *psnr = mse > 0 ? 10.0 * log10(255.0 * 255.0 / mse) : INFINITY;
    *max_err = worst;
    *bad = wrong;
}

static int cmd_bench(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: jpeg bench <file.jpg> [runs] [reference.rgb565]\n");
        return 1;
    }
    const int runs = argc > 3 ? atoi(argv[3]) : DEFAULT_RUNS;
    if (runs < 1 || runs > MAX_RUNS) {
        printf("jpeg: runs is 1..%d\n", MAX_RUNS);
        return 1;
    }
    size_t len = 0, ref_len = 0;
    uint8_t *jpg = load(argv[2], &len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (jpg == NULL) {
        return 1;
    }
    int w = 0, h = 0;
    if (!jpeg_size(jpg, len, &w, &h) || w <= 0 || h <= 0 || w > 1024 || h > 1024) {
        printf("jpeg: %s is not a baseline JPEG I can size\n", argv[2]);
        heap_caps_free(jpg);
        return 1;
    }
    const size_t frame = (size_t)w * h * 2;
    uint8_t *ref = argc > 4 ? load(argv[4], &ref_len, MALLOC_CAP_SPIRAM) : NULL;
    if (ref != NULL && ref_len != frame) {
        printf("jpeg: reference is %u bytes, a %dx%d RGB565 frame is %u; ignoring it\n",
               (unsigned)ref_len, w, h, (unsigned)frame);
        heap_caps_free(ref);
        ref = NULL;
    }
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    uint16_t *fb = alloc_frame(frame);
    if (fb == NULL || bench_esp_jpeg_prepare() != 0) {
        printf("jpeg: no memory for a %u byte frame\n", (unsigned)frame);
        heap_caps_free(jpg);
        heap_caps_free(ref);
        heap_caps_free(fb);
        return 1;
    }

    /* The LCD test cycle is SPI traffic and a task switch every second; the bench runs above
     * everything that normally wakes, so min and average are the decoders, not the board. */
    const bool cycling = board_lcd_cycle_running();
    board_lcd_cycle(false);
    const UBaseType_t prio = uxTaskPriorityGet(NULL);

    printf("%s: %dx%d, %u bytes; %d runs each, CPU %d MHz, -O%s; frame in %s "
           "(largest free internal DMA block %u)\n",
           argv[2], w, h, (unsigned)len, runs, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
#if CONFIG_COMPILER_OPTIMIZATION_PERF
           "2",
#elif CONFIG_COMPILER_OPTIMIZATION_SIZE
           "s",
#else
           "g",
#endif
           esp_ptr_internal(fb) ? "internal RAM" : "PSRAM", (unsigned)largest);
    printf("%-38s %7s %7s %7s %7s %8s %4s %6s %9s\n", "decoder", "min ms", "avg ms", "max ms",
           "fps", "PSNR", "max", "bad px", "crc32");

    for (size_t d = 0; d < DECODER_COUNT; d++) {
        const decoder_t *dec = &k_decoders[d];
        int rc = dec->decode(jpg, len, fb, w, h);   /* warm-up: caches, first-use setup */
        if (rc != 0) {
            printf("%-38s failed: %d\n", dec->label(), rc);
            continue;
        }
        vTaskPrioritySet(NULL, BENCH_PRIO);
        int64_t lo = INT64_MAX, hi = 0, total = 0;
        for (int i = 0; i < runs && rc == 0; i++) {
            const int64_t t0 = esp_timer_get_time();
            rc = dec->decode(jpg, len, fb, w, h);
            const int64_t dt = esp_timer_get_time() - t0;
            lo = dt < lo ? dt : lo;
            hi = dt > hi ? dt : hi;
            total += dt;
        }
        vTaskPrioritySet(NULL, prio);
        if (rc != 0) {
            printf("%-38s failed during the runs: %d\n", dec->label(), rc);
            continue;
        }
        /* A frame poisoned first, so anything the decoder leaves unwritten counts against it. */
        memset(fb, 0x55, frame);
        dec->decode(jpg, len, fb, w, h);
        const uint32_t crc = esp_rom_crc32_le(0, (const uint8_t *)fb, frame);
        const double avg = (double)total / runs / 1000.0;
        printf("%-38s %7.2f %7.2f %7.2f %7.1f", dec->label(), lo / 1000.0, avg, hi / 1000.0,
               1000.0 / avg);
        if (ref != NULL) {
            double psnr;
            int max_err, bad;
            compare((const uint8_t *)fb, ref, w * h, &psnr, &max_err, &bad);
            printf(" %6.1fdB %4d %6d", psnr, max_err, bad);
        } else {
            printf(" %8s %4s %6s", "-", "-", "-");
        }
        printf("  %08lx\n", (unsigned long)crc);
    }

    board_lcd_cycle(cycling);
    heap_caps_free(jpg);
    heap_caps_free(ref);
    heap_caps_free(fb);
    return 0;
}

/* Decode `file` with `dec` into a newly allocated frame. */
static uint16_t *decode_file(const decoder_t *dec, const char *file, int *w, int *h)
{
    size_t len = 0;
    uint8_t *jpg = load(file, &len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (jpg == NULL) {
        return NULL;
    }
    uint16_t *fb = NULL;
    if (!jpeg_size(jpg, len, w, h) || *w <= 0 || *h <= 0 || *w > 1024 || *h > 1024) {
        printf("jpeg: %s is not a baseline JPEG I can size\n", file);
    } else if ((fb = alloc_frame((size_t)*w * *h * 2)) == NULL || bench_esp_jpeg_prepare() != 0) {
        printf("jpeg: no memory for the frame\n");
    } else {
        memset(fb, 0x55, (size_t)*w * *h * 2);
        const int rc = dec->decode(jpg, len, fb, *w, *h);
        if (rc != 0) {
            printf("jpeg: %s failed: %d\n", dec->label(), rc);
            heap_caps_free(fb);
            fb = NULL;
        }
    }
    heap_caps_free(jpg);
    return fb;
}

static int cmd_show(int argc, char **argv)
{
    if (argc < 4) {
        printf("usage: jpeg show <jpegdec|new|tjpgd> <file.jpg>\n");
        return 1;
    }
    const decoder_t *dec = find_decoder(argv[2]);
    int w = 0, h = 0;
    uint16_t *fb = dec ? decode_file(dec, argv[3], &w, &h) : NULL;
    if (fb == NULL) {
        return 1;
    }
    int rc = 1;
    if (w > BOARD_LCD_H_RES || h > BOARD_LCD_V_RES) {
        printf("jpeg: %dx%d is bigger than the panel\n", w, h);
    } else {
        board_lcd_cycle(false);
        board_lcd_fill(0x0000);
        const esp_err_t err = board_lcd_draw((BOARD_LCD_H_RES - w) / 2, (BOARD_LCD_V_RES - h) / 2,
                                             w, h, fb);
        printf("%s: %dx%d on the panel via %s%s (`lcd cycle on` to resume the test cycle)\n",
               argv[3], w, h, dec->label(), err == ESP_OK ? "" : " -- draw failed");
        rc = err == ESP_OK ? 0 : 1;
    }
    heap_caps_free(fb);
    return rc;
}

static int cmd_save(int argc, char **argv)
{
    if (argc < 5) {
        printf("usage: jpeg save <jpegdec|new|tjpgd> <file.jpg> <out.rgb565>\n");
        return 1;
    }
    const decoder_t *dec = find_decoder(argv[2]);
    int w = 0, h = 0;
    uint16_t *fb = dec ? decode_file(dec, argv[3], &w, &h) : NULL;
    if (fb == NULL) {
        return 1;
    }
    char path[PATH_LEN];
    resolve(argv[4], path, sizeof(path));
    const size_t bytes = (size_t)w * h * 2;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    const bool ok = fd >= 0 && write(fd, fb, bytes) == (ssize_t)bytes;
    if (fd >= 0) {
        close(fd);
    }
    printf("%s: %dx%d via %s -> %s%s\n", argv[3], w, h, dec->label(), path, ok ? "" : " -- write failed");
    heap_caps_free(fb);
    return ok ? 0 : 1;
}

static int cmd_jpeg(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "bench") == 0) {
        return cmd_bench(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "show") == 0) {
        return cmd_show(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "save") == 0) {
        return cmd_save(argc, argv);
    }
    printf("usage: jpeg bench <file.jpg> [runs] [reference.rgb565]\n"
           "       jpeg show <jpegdec|new|tjpgd> <file.jpg>\n"
           "       jpeg save <jpegdec|new|tjpgd> <file.jpg> <out.rgb565>\n");
    return 1;
}

void register_jpeg_bench(const char *base_path)
{
    strlcpy(s_base, base_path, sizeof(s_base));
    const esp_console_cmd_t cmd = {
        .command = "jpeg",
        .help = "Benchmark JPEGDEC, esp_new_jpeg and esp_jpeg (TJpgDec) on a JPEG from storage, "
                "show one on the panel, or save a decoded frame",
        .hint = "bench <file> [runs] [ref.rgb565] | show|save <jpegdec|new|tjpgd> <file> [out]",
        .func = cmd_jpeg,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
