/*
 * `lcd`, `touch` and `imu`: the board's peripherals from the console.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "board.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ------------------------------------------------------------------ lcd */

static bool parse_colour(const char *s, uint16_t *out)
{
    static const struct {
        const char *name;
        uint16_t rgb565;
    } names[] = {
        { "r", 0xF800 }, { "red", 0xF800 },
        { "g", 0x07E0 }, { "green", 0x07E0 },
        { "b", 0x001F }, { "blue", 0x001F },
        { "w", 0xFFFF }, { "white", 0xFFFF },
        { "k", 0x0000 }, { "black", 0x0000 },
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcmp(s, names[i].name) == 0) {
            *out = names[i].rgb565;
            return true;
        }
    }
    /* Or a raw RGB565 value, e.g. 0xF81F. */
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 0);
    if (end != s && *end == '\0' && v <= 0xFFFF) {
        *out = (uint16_t)v;
        return true;
    }
    return false;
}

static int lcd_cmd(int argc, char **argv)
{
    if (argc == 1) {
        printf("cycle %s, backlight %d%%\n", board_lcd_cycle_running() ? "on" : "off",
               board_lcd_get_backlight());
        return 0;
    }
    if (strcmp(argv[1], "cycle") == 0 && argc == 3 &&
        (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        board_lcd_cycle(strcmp(argv[2], "on") == 0);
        return 0;
    }
    if (strcmp(argv[1], "fill") == 0 && argc == 3) {
        uint16_t colour;
        if (!parse_colour(argv[2], &colour)) {
            printf("lcd: colour is r, g, b, w, k or an RGB565 value\n");
            return 1;
        }
        board_lcd_cycle(false);
        esp_err_t err = board_lcd_fill(colour);
        if (err != ESP_OK) {
            printf("lcd: %s\n", esp_err_to_name(err));
            return 1;
        }
        return 0;
    }
    if (strcmp(argv[1], "bl") == 0 && argc == 3) {
        esp_err_t err = board_lcd_set_backlight(atoi(argv[2]));
        if (err != ESP_OK) {
            printf("lcd: %s\n", esp_err_to_name(err));
            return 1;
        }
        return 0;
    }
    printf("usage: lcd [cycle on|off | fill r|g|b|w|k|<rgb565> | bl <0-100>]\n");
    return 1;
}

/* ------------------------------------------------------------------ touch */

#if CONFIG_BOARD_TOUCH_ENABLE
static int touch_cmd(int argc, char **argv)
{
    if (argc == 1 || strcmp(argv[1], "status") == 0) {
        printf("touch %s\n", board_touch_running() ? "on" : "off");
        return 0;
    }
    if (strcmp(argv[1], "on") == 0) {
        esp_err_t err = board_touch_start();
        if (err != ESP_OK) {
            printf("touch: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("touch on; touches are printed as they happen\n");
        return 0;
    }
    if (strcmp(argv[1], "off") == 0) {
        board_touch_stop();
        printf("touch off\n");
        return 0;
    }
    printf("usage: touch [on|off|status]\n");
    return 1;
}
#endif

/* ------------------------------------------------------------------ imu */

/* Any byte waiting on the console. stdin is switched to non-blocking for the duration of
 * the stream by the caller; read() goes past stdio's buffer to the UART driver. */
static bool key_waiting(int fd)
{
    uint8_t c;
    return read(fd, &c, 1) == 1;
}

static int imu_cmd(int argc, char **argv)
{
    if (!board_imu_present()) {
        esp_err_t err = board_imu_init();
        if (err != ESP_OK) {
            printf("imu: %s\n", esp_err_to_name(err));
            return 1;
        }
    }
    if (argc == 2 && strcmp(argv[1], "id") == 0) {
        uint8_t who = 0, rev = 0;
        esp_err_t err = board_imu_who_am_i(&who, &rev);
        if (err != ESP_OK) {
            printf("imu: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("QMI8658 at 0x%02x: WHO_AM_I 0x%02x%s, revision 0x%02x\n", board_imu_address(),
               who, who == 0x05 ? "" : " (expected 0x05)", rev);
        return 0;
    }

    int hz = 10;
    long count = 0;   /* 0 = until a key */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            hz = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            count = atol(argv[++i]);
        } else {
            printf("usage: imu [-r <hz>] [-n <count>] | imu id\n");
            return 1;
        }
    }
    if (hz < 1 || hz > 100) {
        printf("imu: rate is 1..100 Hz\n");
        return 1;
    }

    const int fd = fileno(stdin);
    const int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    /* Whatever followed the Enter that started this (a trailing LF) is not the key. */
    while (key_waiting(fd)) {
    }

    printf("%s; press any key to stop\n", count ? "sampling" : "streaming");
    printf("      ax      ay      az (g) |      gx      gy      gz (dps) |  temp\n");
    const TickType_t period = pdMS_TO_TICKS(1000 / hz) ? pdMS_TO_TICKS(1000 / hz) : 1;
    TickType_t wake = xTaskGetTickCount();
    int rc = 0;
    for (long n = 0; count == 0 || n < count; n++) {
        board_imu_sample_t s;
        esp_err_t err = board_imu_read(&s);
        if (err != ESP_OK) {
            printf("imu: %s\n", esp_err_to_name(err));
            rc = 1;
            break;
        }
        printf("%+7.3f %+7.3f %+7.3f     | %+7.2f %+7.2f %+7.2f       | %5.1f C\n",
               s.ax, s.ay, s.az, s.gx, s.gy, s.gz, s.temp_c);
        if (key_waiting(fd)) {
            break;
        }
        vTaskDelayUntil(&wake, period);
    }
    while (key_waiting(fd)) {
    }
    fcntl(fd, F_SETFL, flags);
    return rc;
}

void board_register_commands(void)
{
    const esp_console_cmd_t lcd = {
        .command = "lcd",
        .help = "LCD: the R/G/B/W test cycle, a solid fill, or the backlight level",
        .hint = "[cycle on|off | fill r|g|b|w|k|<rgb565> | bl <0-100>]",
        .func = lcd_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&lcd));

#if CONFIG_BOARD_TOUCH_ENABLE
    const esp_console_cmd_t touch = {
        .command = "touch",
        .help = "CST816S touch reporting on or off (off at boot); prints down/move/up with x,y",
        .hint = "[on|off|status]",
        .func = touch_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&touch));
#endif

    const esp_console_cmd_t imu = {
        .command = "imu",
        .help = "Stream QMI8658 accelerometer, gyro and temperature readings until a key is pressed",
        .hint = "[-r <hz>] [-n <count>] | id",
        .func = imu_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&imu));
}
