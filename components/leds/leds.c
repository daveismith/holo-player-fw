/*
 * The NeoPixel strip: Espressif's led_strip on the RMT, and the Flash_PNG sketch's patterns.
 */
#include <math.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "leds.h"

static const char *TAG = "leds";

#define N CONFIG_LEDS_COUNT

/* The RMT's DMA buffer holds the whole frame twice over, so a frame never waits on a refill:
 * a refill is an interrupt, and while flash is being written (a console history save, say)
 * interrupts outside IRAM wait, and the RMT would send stale symbols meanwhile. 24 symbols
 * an LED, plus the reset. */
#define RMT_SYMBOLS      ((2 * (24 * N + 8) + 63) / 64 * 64)

#define TASK_STACK       3072
#define TASK_PRIO        3      /* under the video player's 5 */
#define FRAME_MS         10     /* the rainbow's step; one tick at CONFIG_FREERTOS_HZ=100 */

static led_strip_handle_t s_strip;
static SemaphoreHandle_t s_lock;   /* the strip, and the state below */
static TaskHandle_t s_task;
static volatile bool s_stop;
static leds_mode_t s_mode = LEDS_OFF;
static uint8_t s_rgb[3];
static bool s_loop;
static int s_brightness = CONFIG_LEDS_BRIGHTNESS;
static uint8_t s_gamma[256];

static uint8_t dim(uint8_t v)
{
    return (uint8_t)((v * s_brightness + 50) / 100);
}

static void put(int i, uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(s_strip, i, dim(r), dim(g), dim(b));
}

static esp_err_t fill_locked(const uint8_t rgb[3])
{
    for (int i = 0; i < N; i++) {
        put(i, rgb[0], rgb[1], rgb[2]);
    }
    return led_strip_refresh(s_strip);
}

/* Adafruit_NeoPixel::ColorHSV() at full saturation and value: a 16-bit hue round a wheel of
 * 1530 steps, red to green to blue and back. */
static void wheel(uint16_t hue16, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const uint32_t hue = ((uint32_t)hue16 * 1530u + 32768u) / 65536u;
    if (hue < 510) {
        *b = 0;
        if (hue < 255) {
            *r = 255;
            *g = (uint8_t)hue;
        } else {
            *r = (uint8_t)(510 - hue);
            *g = 255;
        }
    } else if (hue < 1020) {
        *r = 0;
        if (hue < 765) {
            *g = 255;
            *b = (uint8_t)(hue - 510);
        } else {
            *g = (uint8_t)(1020 - hue);
            *b = 255;
        }
    } else if (hue < 1530) {
        *g = 0;
        if (hue < 1275) {
            *r = (uint8_t)(hue - 1020);
            *b = 255;
        } else {
            *r = 255;
            *b = (uint8_t)(1530 - hue);
        }
    } else {
        *r = 255;
        *g = *b = 0;
    }
}

/* Sleep `ms`, or less if told to stop. */
static void pause_ms(int ms)
{
    for (int t = 0; t < ms && !s_stop; t += FRAME_MS) {
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
    }
}

static void wipe_pass(void)
{
    /* Flash_PNG's first pattern: each LED to the colour in turn, 250 ms apart. */
    for (int i = 0; i < N && !s_stop; i++) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        put(i, s_rgb[0], s_rgb[1], s_rgb[2]);
        led_strip_refresh(s_strip);
        xSemaphoreGive(s_lock);
        pause_ms(250);
    }
}

static void rainbow_pass(void)
{
    /* Flash_PNG's second, Adafruit's rainbow: the first LED's hue goes five times round the
     * wheel, 256/65536 of a turn a frame at 10 ms a frame (12.8 s), the others spread once
     * round the strip after it, each gamma-corrected as gamma32() did. */
    TickType_t wake = xTaskGetTickCount();
    for (uint32_t first = 0; first < 5 * 65536 && !s_stop; first += 256) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < N; i++) {
            uint8_t r, g, b;
            wheel((uint16_t)(first + (uint32_t)i * 65536u / N), &r, &g, &b);
            put(i, s_gamma[r], s_gamma[g], s_gamma[b]);
        }
        led_strip_refresh(s_strip);
        xSemaphoreGive(s_lock);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(FRAME_MS));
    }
}

static void pattern_task(void *arg)
{
    (void)arg;
    do {
        if (s_mode == LEDS_WIPE) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            led_strip_clear(s_strip);   /* each pass starts from dark */
            xSemaphoreGive(s_lock);
            wipe_pass();
        } else {
            rainbow_pass();
        }
        pause_ms(1000);   /* the sketch's delay(1000) after each */
    } while (s_loop && !s_stop);

    /* Played out (or stopped): off, as the sketch's task left it. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    led_strip_clear(s_strip);
    s_mode = LEDS_OFF;
    xSemaphoreGive(s_lock);
    s_task = NULL;
    vTaskDelete(NULL);
}

static void stop_pattern(void)
{
    if (s_task == NULL) {
        return;
    }
    s_stop = true;
    for (int i = 0; i < 200 && s_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t leds_solid(uint8_t r, uint8_t g, uint8_t b)
{
    ESP_RETURN_ON_FALSE(s_strip != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    stop_pattern();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_rgb[0] = r;
    s_rgb[1] = g;
    s_rgb[2] = b;
    s_mode = LEDS_SOLID;
    const esp_err_t err = fill_locked(s_rgb);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t leds_off(void)
{
    ESP_RETURN_ON_FALSE(s_strip != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    stop_pattern();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_mode = LEDS_OFF;
    const esp_err_t err = led_strip_clear(s_strip);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t leds_play(leds_mode_t pattern, uint8_t r, uint8_t g, uint8_t b, bool loop)
{
    ESP_RETURN_ON_FALSE(s_strip != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(pattern == LEDS_WIPE || pattern == LEDS_RAINBOW, ESP_ERR_INVALID_ARG,
                        TAG, "not a pattern");
    stop_pattern();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_mode = pattern;
    s_rgb[0] = r;
    s_rgb[1] = g;
    s_rgb[2] = b;
    s_loop = loop;
    s_stop = false;
    xSemaphoreGive(s_lock);
    if (xTaskCreate(pattern_task, "leds", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        s_task = NULL;
        s_mode = LEDS_OFF;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void leds_set_brightness(int percent)
{
    percent = percent < 1 ? 1 : percent > 100 ? 100 : percent;
    if (s_lock == NULL) {
        s_brightness = percent;
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_brightness = percent;
    if (s_mode == LEDS_SOLID) {
        fill_locked(s_rgb);
    }
    xSemaphoreGive(s_lock);
}

int leds_get_brightness(void)
{
    return s_brightness;
}

leds_mode_t leds_mode(uint8_t rgb[3], bool *loop)
{
    if (rgb != NULL) {
        memcpy(rgb, s_rgb, 3);
    }
    if (loop != NULL) {
        *loop = s_loop;
    }
    return s_mode;
}

esp_err_t leds_init(void)
{
    if (s_strip != NULL) {
        return ESP_OK;
    }
    /* Adafruit's gamma table: gamma 2.6 */
    for (int i = 0; i < 256; i++) {
        s_gamma[i] = (uint8_t)(powf(i / 255.0f, 2.6f) * 255.0f + 0.5f);
    }
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "lock");

    const led_strip_config_t strip_cfg = {
        .strip_gpio_num = CONFIG_LEDS_GPIO,
        .max_leds = N,
#if CONFIG_LEDS_SPEED_400K
        .led_model = LED_MODEL_WS2811,
#else
        .led_model = LED_MODEL_WS2812,
#endif
#if CONFIG_LEDS_ORDER_GRB
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
#else
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
#endif
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = RMT_SYMBOLS,
        .flags = { .with_dma = true },
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    bool dma = true;
    if (err != ESP_OK) {
        /* No DMA channel to spare: the RMT's own memory, refilled from its interrupt. */
        ESP_LOGW(TAG, "RMT with DMA: %s; trying without", esp_err_to_name(err));
        rmt_cfg.flags.with_dma = false;
        rmt_cfg.mem_block_symbols = 0;   /* the driver's default */
        dma = false;
        err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    }
    ESP_RETURN_ON_ERROR(err, TAG, "strip");

    /* Off, whatever the LEDs latched when they powered up. */
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "%d LEDs on GPIO%d, %s, %s%s", N, CONFIG_LEDS_GPIO,
             CONFIG_LEDS_SPEED_400K ? "400 kHz" : "800 kHz",
             CONFIG_LEDS_ORDER_GRB ? "GRB" : "RGB", dma ? ", RMT DMA" : "");
    return ESP_OK;
}
