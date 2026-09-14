/*
 * CST816S touch, off unless started.
 *
 * Reads are driven by the controller's interrupt rather than polled: the CST816S only answers
 * on I2C while it is awake -- a finger down, or just after -- so polling an idle screen is a
 * stream of NACKs and driver error logs. One read after the interrupts stop settles whether
 * the finger has lifted, since not every controller revision pulses on release.
 */
#include <stdio.h>
#include <stdlib.h>
#include "board.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch";

#define RELEASE_TIMEOUT_MS 80

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_touch_handle_t s_tp;
static TaskHandle_t s_task;
static volatile bool s_run;

static void IRAM_ATTR on_touch_int(esp_lcd_touch_handle_t tp)
{
    (void)tp;
    BaseType_t woken = pdFALSE;
    TaskHandle_t task = s_task;
    if (task != NULL) {
        vTaskNotifyGiveFromISR(task, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

/* The current point, if a finger is down. False on a failed read (an asleep controller). */
static bool read_point(uint16_t *x, uint16_t *y)
{
    if (esp_lcd_touch_read_data(s_tp) != ESP_OK) {
        return false;
    }
    esp_lcd_touch_point_data_t pt[1];
    uint8_t count = 0;
    if (esp_lcd_touch_get_data(s_tp, pt, &count, 1) != ESP_OK || count == 0) {
        return false;
    }
    *x = pt[0].x;
    *y = pt[0].y;
    return true;
}

static void touch_task(void *arg)
{
    (void)arg;
    bool down = false;
    uint16_t last_x = 0, last_y = 0;
    while (s_run) {
        const uint32_t events = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RELEASE_TIMEOUT_MS));
        if (!s_run) {
            break;
        }
        if (events == 0 && !down) {
            continue;
        }
        uint16_t x, y;
        if (read_point(&x, &y)) {
            if (!down) {
                printf("touch down x=%u y=%u\n", x, y);
            } else if (abs((int)x - last_x) > 2 || abs((int)y - last_y) > 2) {
                printf("touch move x=%u y=%u\n", x, y);
            }
            down = true;
            last_x = x;
            last_y = y;
        } else if (down) {
            printf("touch up   x=%u y=%u\n", last_x, last_y);
            down = false;
        }
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t board_touch_start(void)
{
    if (s_tp != NULL) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = board_i2c_bus();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_STATE, TAG, "board_init() first");

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &s_io), TAG, "panel io");

    const esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = BOARD_TOUCH_RST,
        .int_gpio_num = BOARD_TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
        .interrupt_callback = on_touch_int,
    };
    esp_err_t err = esp_lcd_touch_new_i2c_cst816s(s_io, &cfg, &s_tp);
    if (err != ESP_OK) {
        s_tp = NULL;
        board_touch_stop();
        return err;
    }
    /* Only now, with s_tp set: the driver's reset makes the controller pulse INT during
     * esp_lcd_touch_new_i2c_cst816s(), and a task already waiting would read through a
     * handle that does not exist yet. The ISR drops edges while s_task is NULL. */
    s_run = true;
    if (xTaskCreate(touch_task, "touch", 3072, NULL, 4, &s_task) != pdPASS) {
        board_touch_stop();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "CST816S on, reporting touches");
    return ESP_OK;
}

void board_touch_stop(void)
{
    s_run = false;
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
        while (s_task != NULL) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (s_tp != NULL) {
        esp_lcd_touch_register_interrupt_callback(s_tp, NULL);
        esp_lcd_touch_del(s_tp);
        s_tp = NULL;
    }
    if (s_io != NULL) {
        esp_lcd_panel_io_del(s_io);
        s_io = NULL;
    }
    /* Held in reset: the controller draws nothing and cannot pull INT. */
    gpio_set_direction(BOARD_TOUCH_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(BOARD_TOUCH_RST, 0);
}

bool board_touch_running(void)
{
    return s_tp != NULL;
}
