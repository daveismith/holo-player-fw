/*
 * Waveshare ESP32-S3-Touch-LCD-1.28 (Rev3): pins and board bring-up.
 *
 * Pins are from the Rev3 schematic netlist; Waveshare's docs, TFT_eSPI Setup302 and the
 * NuttX esp32s3-ws-lcd128 board agree. USB-C goes only to a CH343P UART bridge on UART0
 * (GPIO43/44) -- the S3's native USB pins (GPIO19/20) are not connected.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GC9A01A, 240x240 round IPS, 4-wire SPI. MISO is routed to the panel but unused. */
#define BOARD_LCD_SPI_HOST      SPI2_HOST
#define BOARD_LCD_SCLK          10
#define BOARD_LCD_MOSI          11
#define BOARD_LCD_MISO          12
#define BOARD_LCD_CS            9
#define BOARD_LCD_DC            8
#define BOARD_LCD_RST           14
#define BOARD_LCD_H_RES         240
#define BOARD_LCD_V_RES         240
/* GPIO10 is not SPI2's IOMUX clock pin, so the signals go through the GPIO matrix. On the
 * S3 that limits reads (MISO timing), not writes, and the panel is only ever written. */
#define BOARD_LCD_PCLK_HZ       (CONFIG_BOARD_LCD_PCLK_MHZ * 1000 * 1000)

/* Backlight: GPIO2 drives an N-FET (active high), pulled down, so off at reset. */
#define BOARD_LCD_BL            2

/* One I2C bus for the touch controller and the IMU. No pull-ups on the board. */
#define BOARD_I2C_PORT          0
#define BOARD_I2C_SDA           6
#define BOARD_I2C_SCL           7
#define BOARD_I2C_HZ            400000

/* CST816S. It only answers on I2C while the screen is being touched. */
#define BOARD_TOUCH_ADDR        0x15
#define BOARD_TOUCH_INT         5
#define BOARD_TOUCH_RST         13

/* QMI8658A. SA0 is tied low; Waveshare's own headers disagree about which address that
 * gives, so the other is probed too. */
#define BOARD_IMU_ADDR          0x6B
#define BOARD_IMU_ADDR_ALT      0x6A
#define BOARD_IMU_INT1          4
#define BOARD_IMU_INT2          3

/* Battery through a 200k/100k divider: Vbat = 3 x Vadc (ADC1_CH0). */
#define BOARD_BAT_ADC_GPIO      1

/* The shared I2C bus. Everything else is started by its own module. */
esp_err_t board_init(void);
i2c_master_bus_handle_t board_i2c_bus(void);

/* LCD (lcd.c). Colours are RGB565 in the usual order; the driver byte-swaps for the bus.
 * board_lcd_init() leaves the panel asleep with the backlight off. */
esp_err_t board_lcd_init(void);
/*
 * Power. board_lcd_power_on() wakes the panel, fills the whole screen with `rgb565` while the
 * display is still off -- so nothing its memory held from before is ever seen -- then turns the
 * display and backlight on; if the panel is already on it is just the fill.
 * board_lcd_power_off() turns the backlight and display off and puts the controller to sleep.
 */
esp_err_t board_lcd_power_on(uint16_t rgb565);
esp_err_t board_lcd_power_off(void);
bool board_lcd_powered(void);
/* Called after the panel goes from on to off, from whichever task turned it off (the video
 * player's, when a clip ends by itself). Nothing is showing then, so it is a good moment for
 * work that would disturb playback, such as a flash write. */
void board_lcd_set_off_hook(void (*hook)(void *ctx), void *ctx);

/* Colours from the console (colour.c), for every command that shows one: a name (red,
 * orange, ...), #RRGGBB or RRGGBB, or R,G,B in decimal. board_parse_rgb_args() takes the
 * colour's words -- one, or three for "R G B". */
bool board_parse_rgb(const char *s, uint8_t rgb[3]);
bool board_parse_rgb_args(int argc, char **argv, uint8_t rgb[3]);
/* The backlight level while the panel is on (default 100%), applied at once if it is. */
esp_err_t board_lcd_set_backlight(int percent);
int board_lcd_get_backlight(void);
esp_err_t board_lcd_fill(uint16_t rgb565);
/* A w x h block of big-endian RGB565 at (x, y). Any memory will do -- what is not DMA-capable
 * is staged through the driver's own strip buffer -- and it returns once the panel has it. */
esp_err_t board_lcd_draw(int x, int y, int w, int h, const uint16_t *pixels);

/*
 * Streaming one frame as a run of blocks, each sent while the caller prepares the next:
 * begin, then queue blocks with board_lcd_stream_block() (DMA-capable memory; it returns once
 * the block is queued), then end. A block's pixels must stay untouched until it is out:
 * board_lcd_stream_wait(n) returns once no more than `n` blocks are still pending, so a caller
 * alternating two buffers waits for n = 1 before refilling one. The panel is locked from begin
 * to end.
 */
esp_err_t board_lcd_stream_begin(void);
esp_err_t board_lcd_stream_block(int x, int y, int w, int h, const uint16_t *pixels);
esp_err_t board_lcd_stream_wait(int max_pending);
esp_err_t board_lcd_stream_end(void);
/* `frames` whole-screen fills back to back; the average time per fill in *us_per_frame. */
esp_err_t board_lcd_bench(int frames, int64_t *us_per_frame);

#if CONFIG_BOARD_TOUCH_ENABLE
/* Touch (touch.c). Off unless started; `touch on` or CONFIG_BOARD_TOUCH_AUTOSTART. */
esp_err_t board_touch_start(void);
void board_touch_stop(void);
bool board_touch_running(void);
#endif

/* IMU (imu.c). */
typedef struct {
    float ax, ay, az;   /* g */
    float gx, gy, gz;   /* degrees per second */
    float temp_c;
} board_imu_sample_t;

esp_err_t board_imu_init(void);
bool board_imu_present(void);
uint8_t board_imu_address(void);
esp_err_t board_imu_read(board_imu_sample_t *out);
/* WHO_AM_I (0x05 for a QMI8658) and the revision register. */
esp_err_t board_imu_who_am_i(uint8_t *who, uint8_t *revision);

/* `lcd`, `touch` and `imu` console commands. */
void board_register_commands(void);

#ifdef __cplusplus
}
#endif
