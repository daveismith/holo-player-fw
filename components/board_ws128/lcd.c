/*
 * GC9A01 round LCD: panel bring-up, power (sleep mode and the backlight), whole-screen fills,
 * blocks, and frames streamed a block at a time.
 *
 * The panel sleeps with the backlight off until something is shown: board_lcd_power_on()
 * wakes it, board_lcd_power_off() puts it back.
 */
#include <string.h>
#include <sys/param.h>
#include "board.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "lcd";

/* A fill is sent as strips of this many lines from one DMA buffer: 240x40 RGB565 is
 * 19.2 KB of internal RAM rather than a 115 KB frame. */
#define STRIP_LINES      40
#define STRIP_PIXELS     (BOARD_LCD_H_RES * STRIP_LINES)
#define STRIPS_PER_FRAME ((BOARD_LCD_V_RES + STRIP_LINES - 1) / STRIP_LINES)

#define BL_LEDC_TIMER    LEDC_TIMER_0
#define BL_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BL_LEDC_RES      LEDC_TIMER_10_BIT
#define BL_FREQ_HZ       5000

/* After Sleep Out the controller wants 120 ms before Sleep In, and its supplies take about as
 * long to settle; after Sleep In, 5 ms before anything else. */
#define SLEEP_OUT_MS     120
#define SLEEP_IN_MS      5
/* Display On takes effect at the panel's next refresh (about 60 Hz). The light waits out two,
 * so by the time anything can be seen the panel is already scanning out the blank fill -- and
 * a caller that starts drawing on return (the player's first frame) is drawing on a screen
 * that is visibly on. */
#define DISPLAY_ON_MS    35

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_strip;
static SemaphoreHandle_t s_lock;         /* one user of the panel at a time */
static SemaphoreHandle_t s_strip_done;   /* the bus has finished with a block */
static int s_backlight = 100;            /* the level while the panel is on, percent */
static bool s_powered;
static void (*s_off_hook)(void *ctx);
static void *s_off_hook_ctx;

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata,
                          void *ctx)
{
    (void)io;
    (void)edata;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_strip_done, &woken);
    /* Yield here rather than trusting the return value: the SPI driver's post-transaction
     * hook is void, so a "woken" returned through it goes nowhere, and the filling task then
     * sat until the next tick -- 10 ms at CONFIG_FREERTOS_HZ=100, per strip. That was ~40 of
     * the ~52 ms a fill took, and the reason a colour change visibly swept down the screen. */
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
    return woken == pdTRUE;
}

static esp_err_t backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BL_LEDC_RES,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    const ledc_channel_config_t channel = {
        .gpio_num = BOARD_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_LEDC_CHANNEL,
        .timer_sel = BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&channel);
}

static esp_err_t apply_backlight(int percent)
{
    /* 2^res is fully on for LEDC; scaling to it rather than 2^res - 1 makes 100% steady. */
    const uint32_t duty = ((1u << BL_LEDC_RES) * (uint32_t)percent) / 100u;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, duty), TAG, "duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

esp_err_t board_lcd_set_backlight(int percent)
{
    s_backlight = MAX(0, MIN(100, percent));
    return s_powered ? apply_backlight(s_backlight) : ESP_OK;
}

int board_lcd_get_backlight(void)
{
    return s_backlight;
}

static esp_err_t fill_locked(uint16_t rgb565)
{
    /* The bus sends each pixel high byte first; the buffer is little-endian memory. */
    const uint16_t px = (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
    for (size_t i = 0; i < STRIP_PIXELS; i++) {
        s_strip[i] = px;
    }
    /* Every strip sends the same buffer, so they are queued back to back: esp_lcd itself holds
     * each strip's window commands until the previous strip's pixels are out, which keeps the
     * bus busy without a round trip through this task. Only the next fill -- which rewrites
     * the buffer -- has to wait for the last of them. */
    int queued = 0;
    esp_err_t err = ESP_OK;
    for (int y = 0; y < BOARD_LCD_V_RES; y += STRIP_LINES) {
        const int y_end = MIN(y + STRIP_LINES, BOARD_LCD_V_RES);
        err = esp_lcd_panel_draw_bitmap(s_panel, 0, y, BOARD_LCD_H_RES, y_end, s_strip);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "draw: %s", esp_err_to_name(err));
            break;
        }
        queued++;
    }
    for (int i = 0; i < queued; i++) {
        if (xSemaphoreTake(s_strip_done, pdMS_TO_TICKS(200)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
    }
    return err;
}

esp_err_t board_lcd_fill(uint16_t rgb565)
{
    ESP_RETURN_ON_FALSE(s_panel != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = fill_locked(rgb565);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t board_lcd_power_on(uint16_t rgb565)
{
    ESP_RETURN_ON_FALSE(s_panel != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    const bool waking = !s_powered;
    if (waking) {
        /* Out of sleep with the display still off, so whatever its memory held from before --
         * the last frame of the last clip, say -- is never seen: the fill goes in first. */
        err = esp_lcd_panel_io_tx_param(s_io, LCD_CMD_SLPOUT, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(SLEEP_OUT_MS));
    }
    if (err == ESP_OK) {
        err = fill_locked(rgb565);
    }
    if (err == ESP_OK && waking) {
        err = esp_lcd_panel_disp_on_off(s_panel, true);
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(DISPLAY_ON_MS));
            s_powered = true;
            err = apply_backlight(s_backlight);
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t board_lcd_power_off(void)
{
    ESP_RETURN_ON_FALSE(s_panel != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    const bool was_on = s_powered;
    if (was_on) {
        /* Light first, so the display going off is never seen as a flash. */
        apply_backlight(0);
        err = esp_lcd_panel_disp_on_off(s_panel, false);
        if (err == ESP_OK) {
            err = esp_lcd_panel_io_tx_param(s_io, LCD_CMD_SLPIN, NULL, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(SLEEP_IN_MS));
        s_powered = false;
    }
    xSemaphoreGive(s_lock);
    /* Outside the lock, so the hook may take its time or use the panel itself. */
    if (was_on && s_off_hook != NULL) {
        s_off_hook(s_off_hook_ctx);
    }
    return err;
}

void board_lcd_set_off_hook(void (*hook)(void *ctx), void *ctx)
{
    s_off_hook_ctx = ctx;
    s_off_hook = hook;
}

bool board_lcd_powered(void)
{
    return s_powered;
}

esp_err_t board_lcd_draw(int x, int y, int w, int h, const uint16_t *pixels)
{
    ESP_RETURN_ON_FALSE(s_panel != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(w > 0 && w <= BOARD_LCD_H_RES && h > 0, ESP_ERR_INVALID_ARG, TAG, "size");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (esp_ptr_dma_capable(pixels)) {
        /* esp_lcd splits a block bigger than one SPI transfer and signals after the last. */
        err = esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, pixels);
        if (err == ESP_OK && xSemaphoreTake(s_strip_done, pdMS_TO_TICKS(500)) != pdTRUE) {
            err = ESP_ERR_TIMEOUT;
        }
    } else {
        /* PSRAM and the like: through the DMA strip, as many rows at a time as fit. */
        const int rows = STRIP_PIXELS / w;
        for (int row = 0; row < h && err == ESP_OK; row += rows) {
            const int n = MIN(rows, h - row);
            memcpy(s_strip, pixels + (size_t)row * w, (size_t)n * w * sizeof(uint16_t));
            err = esp_lcd_panel_draw_bitmap(s_panel, x, y + row, x + w, y + row + n, s_strip);
            if (err == ESP_OK && xSemaphoreTake(s_strip_done, pdMS_TO_TICKS(200)) != pdTRUE) {
                err = ESP_ERR_TIMEOUT;
            }
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

/* Blocks queued by board_lcd_stream_block() and not yet reported done by on_color_done(). */
static int s_stream_pending;

esp_err_t board_lcd_stream_begin(void)
{
    ESP_RETURN_ON_FALSE(s_panel != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    while (xSemaphoreTake(s_strip_done, 0) == pdTRUE) {
        /* nothing should be left over, but a stale count would release a buffer early */
    }
    s_stream_pending = 0;
    return ESP_OK;
}

esp_err_t board_lcd_stream_block(int x, int y, int w, int h, const uint16_t *pixels)
{
    ESP_RETURN_ON_FALSE(esp_ptr_dma_capable(pixels), ESP_ERR_INVALID_ARG, TAG,
                        "a streamed block must be in DMA-capable memory");
    /* esp_lcd sends this block's window commands only once the previous block's pixels are
     * out, so blocks go back to back; each one fits a single SPI transfer, so each completes
     * with exactly one on_color_done(). */
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, pixels);
    if (err == ESP_OK) {
        s_stream_pending++;
    }
    return err;
}

esp_err_t board_lcd_stream_wait(int max_pending)
{
    while (s_stream_pending > max_pending) {
        if (xSemaphoreTake(s_strip_done, pdMS_TO_TICKS(200)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        s_stream_pending--;
    }
    return ESP_OK;
}

esp_err_t board_lcd_stream_end(void)
{
    const esp_err_t err = board_lcd_stream_wait(0);
    s_stream_pending = 0;
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t board_lcd_bench(int frames, int64_t *us_per_frame)
{
    static const uint16_t colours[] = { 0xF800, 0x07E0, 0x001F, 0xFFFF };
    ESP_RETURN_ON_FALSE(s_panel != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    frames = MAX(1, frames);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int64_t t0 = esp_timer_get_time();
    esp_err_t err = ESP_OK;
    for (int i = 0; i < frames && err == ESP_OK; i++) {
        err = fill_locked(colours[i % (sizeof(colours) / sizeof(colours[0]))]);
    }
    const int64_t elapsed = esp_timer_get_time() - t0;
    xSemaphoreGive(s_lock);
    if (us_per_frame != NULL) {
        *us_per_frame = elapsed / frames;
    }
    return err;
}

esp_err_t board_lcd_init(void)
{
    if (s_panel != NULL) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    s_strip_done = xSemaphoreCreateCounting(STRIPS_PER_FRAME, 0);
    s_strip = heap_caps_malloc(STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_lock && s_strip_done && s_strip, ESP_ERR_NO_MEM, TAG, "alloc");

    /* Off from here on: GPIO2's pull-down has held it off since reset. */
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

    const spi_bus_config_t bus = {
        .sclk_io_num = BOARD_LCD_SCLK,
        .mosi_io_num = BOARD_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = STRIP_PIXELS * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BOARD_LCD_CS,
        .dc_gpio_num = BOARD_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = BOARD_LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST,
                                                 &io_cfg, &s_io), TAG, "panel io");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9a01(s_io, &panel_cfg, &s_panel), TAG, "gc9a01");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    /* The IPS panel needs inversion on for colours to come out as sent. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false), TAG, "mirror");

    /* The init sequence woke the controller; nothing is shown yet, so straight back to sleep.
     * Its Sleep Out was at least 100 ms ago, as Sleep In requires. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, false), TAG, "display off");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, LCD_CMD_SLPIN, NULL, 0), TAG, "sleep");
    s_powered = false;
    ESP_LOGI(TAG, "GC9A01 %dx%d at %d MHz; asleep until something is shown", BOARD_LCD_H_RES,
             BOARD_LCD_V_RES, BOARD_LCD_PCLK_HZ / 1000000);
    return ESP_OK;
}
