/*
 * esp_jpeg: Espressif's wrapper around TJpgDec -- the copy in the S3's ROM (CONFIG_JD_USE_ROM),
 * or one compiled from source with a choice of optimisation level (CONFIG_JD_FASTDECODE).
 */
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "sdkconfig.h"
#include "jpeg_decoder.h"
#include "jpeg_bench_priv.h"

/* TJpgDec's scratch: 3.1 KB, or ~65 KB with the table-driven Huffman decoder. */
#if !CONFIG_JD_USE_ROM && CONFIG_JD_FASTDECODE == 2
#define WORK_SIZE (66 * 1024)
#else
#define WORK_SIZE 3200
#endif

static void *s_work;

int bench_esp_jpeg_prepare(void)
{
    if (s_work == NULL) {
        s_work = heap_caps_malloc(WORK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (s_work == NULL) {
            s_work = heap_caps_malloc(WORK_SIZE, MALLOC_CAP_8BIT);   /* PSRAM, then */
        }
    }
    return s_work ? 0 : -1;
}

int bench_esp_jpeg_decode(const uint8_t *jpg, size_t len, uint16_t *fb, int w, int h)
{
    esp_jpeg_image_cfg_t cfg = {
        .indata = (uint8_t *)jpg,
        .indata_size = (uint32_t)len,
        .outbuf = (uint8_t *)fb,
        .outbuf_size = (uint32_t)(w * h * 2),
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = { .swap_color_bytes = 1 },   /* big-endian, as the panel wants */
        .advanced = { .working_buffer = s_work, .working_buffer_size = WORK_SIZE },
    };
    esp_jpeg_image_output_t out;
    esp_err_t err = esp_jpeg_decode(&cfg, &out);
    if (err == ESP_OK && (out.width != w || out.height != h)) {
        err = ESP_ERR_INVALID_SIZE;
    }
    return err;
}

const char *bench_esp_jpeg_label(void)
{
    static char label[48];
#if CONFIG_JD_USE_ROM
    snprintf(label, sizeof(label), "esp_jpeg (ROM TJpgDec)");
#else
    snprintf(label, sizeof(label), "esp_jpeg (TJpgDec, fastdecode %d)", CONFIG_JD_FASTDECODE);
#endif
    if (s_work != NULL && !esp_ptr_internal(s_work)) {
        snprintf(label + strlen(label), sizeof(label) - strlen(label), " [work in PSRAM]");
    }
    return label;
}
