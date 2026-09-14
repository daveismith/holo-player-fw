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
/* GPIO10/11 are not SPI2's IOMUX pins, so the signals go through the GPIO matrix. */
#define BOARD_LCD_PCLK_HZ       (40 * 1000 * 1000)

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

/* LCD (lcd.c). Colours are RGB565 in the usual order; the driver byte-swaps for the bus. */
esp_err_t board_lcd_init(void);
esp_err_t board_lcd_fill(uint16_t rgb565);
esp_err_t board_lcd_set_backlight(int percent);
int board_lcd_get_backlight(void);
/* The red, green, blue, white test cycle, one second each. */
void board_lcd_cycle(bool on);
bool board_lcd_cycle_running(void);

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
