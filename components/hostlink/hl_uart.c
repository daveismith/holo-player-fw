/*
 * The host link on a UART (UART1), or on RS485 through a transceiver whose DE/RE the UART drives
 * from pin C. One task owns both directions: it reads a line, answers it, and between requests
 * sends the events the host asked to have pushed -- so an event never lands in the middle of a
 * reply, and there is nothing to lock.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "hostlink.h"
#include "hl.h"

static const char *TAG = "hostlink";

#define PORT        UART_NUM_1
#define RX_BUF      2048
#define TX_BUF      2048
#define POLL_MS     20

static TaskHandle_t s_task;
static QueueHandle_t s_uart_events;
static hl_settings_t s_run;

static void wake(void)
{
    TaskHandle_t t = s_task;
    if (t != NULL) {
        xTaskNotifyGive(t);
    }
}

static void put(const char *s, size_t n)
{
    if (n == 0) {
        return;
    }
    if (s_run.mode == HL_RS485 && s_run.reply_delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(s_run.reply_delay_ms));   /* the host's transceiver turns round */
    }
    uart_write_bytes(PORT, s, n);
}

static void put_line(const char *s)
{
    if (hl_logging()) {
        printf("link > %s\n", s);
    }
    const size_t n = strlen(s);
    char *line = malloc(n + 1);
    if (line == NULL) {
        return;
    }
    memcpy(line, s, n);
    line[n] = '\n';
    put(line, n + 1);
    free(line);
}

static void handle(char *line)
{
    if (hl_logging()) {
        printf("link < %s\n", line);
    }
    char *reply = hl_line_handle(line, &s_run);
    if (reply != NULL) {
        put_line(reply);
        free(reply);
    }
    const int baud = hl_take_baud();
    if (baud > 0) {
        uart_wait_tx_done(PORT, pdMS_TO_TICKS(500));    /* the reply at the old rate */
        uart_set_baudrate(PORT, baud);
        s_run.baud = baud;
    }
}

/* The UART's own news: damaged bytes, and bytes lost. True when the line being read can't be
 * trusted -- a wrong baud rate's garbage -- and should be dropped. */
static bool drain_uart_events(void)
{
    bool damaged = false;
    uart_event_t ev;
    while (xQueueReceive(s_uart_events, &ev, 0) == pdTRUE) {
        hl_counters_t *c = hl_counters();
        switch (ev.type) {
        case UART_FRAME_ERR:
        case UART_PARITY_ERR:
            c->framing_errors++;
            damaged = true;
            break;
        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
            c->overruns++;
            damaged = true;
            uart_flush_input(PORT);
            xQueueReset(s_uart_events);
            break;
        default:
            break;
        }
    }
    return damaged;
}

static void push_events(void)
{
    if (s_run.mode != HL_UART) {
        return;                     /* RS485: nobody speaks unasked */
    }
    size_t len;
    char *lines = hl_events_push_lines(&len);
    if (lines != NULL) {
        if (hl_logging()) {
            printf("link > %.*s", (int)len, lines);
        }
        put(lines, len);
        for (size_t i = 0; i < len; i++) {
            hl_counters()->events += lines[i] == '\n';
        }
        free(lines);
    }
    if (s_run.attn && s_run.pin_c >= 0) {
        gpio_set_level(s_run.pin_c, hl_events_pending() > 0 ? 0 : 1);
    }
}

static void link_task(void *arg)
{
    (void)arg;
    static char line[HL_LINE_MAX + 1];
    size_t n = 0;
    bool too_long = false;
    uint8_t buf[128];

    if (s_run.mode == HL_UART) {
        char ready[128];
        snprintf(ready, sizeof(ready), "! {\"event\":\"ready\",\"firmware\":\"%s\",\"protocol\":%d,\"address\":%d}",
                 esp_app_get_description()->version, HOSTLINK_PROTOCOL_VERSION, s_run.address);
        put_line(ready);
    }
    for (;;) {
        const int got = uart_read_bytes(PORT, buf, sizeof(buf), pdMS_TO_TICKS(POLL_MS));
        for (int i = 0; i < got; i++) {
            const char ch = (char)buf[i];
            if (ch == '\n') {
                if (too_long) {
                    hl_counters()->bad_lines++;
                    if (s_run.mode == HL_UART) {
                        put_line("413 {\"error\":\"too_large\",\"message\":\"a line is at most 1024 bytes\"}");
                    }
                } else {
                    line[n] = '\0';
                    handle(line);
                }
                n = 0;
                too_long = false;
            } else if (ch == '\r') {
                continue;
            } else if (n == 0 && ch == '\0') {
                continue;               /* between native frames: not this protocol's */
            } else if (n < HL_LINE_MAX - 1) {
                line[n++] = ch;
            } else {
                too_long = true;
            }
        }
        if (drain_uart_events() && n > 0) {
            hl_counters()->bad_lines++;
            n = 0;                      /* what came garbled isn't a request */
            too_long = false;
        }
        ulTaskNotifyTake(pdTRUE, 0);
        push_events();
        hl_running(&s_run);             /* the live settings: address, groups, delay */
    }
}

esp_err_t hl_uart_start(const hl_settings_t *run)
{
    s_run = *run;
    const uart_config_t cfg = {
        .baud_rate = run->baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(PORT, RX_BUF, TX_BUF, 16, &s_uart_events, 0);
    if (err == ESP_OK) {
        err = uart_param_config(PORT, &cfg);
    }
    const bool rs485 = run->mode == HL_RS485;
    if (err == ESP_OK) {
        err = uart_set_pin(PORT, run->pin_a, run->pin_b, rs485 ? run->pin_c : UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK && rs485) {
        err = uart_set_mode(PORT, UART_MODE_RS485_HALF_DUPLEX);
    }
    if (err == ESP_OK && run->mode == HL_UART && run->attn && run->pin_c >= 0) {
        gpio_reset_pin(run->pin_c);
        gpio_set_direction(run->pin_c, GPIO_MODE_OUTPUT);
        gpio_set_level(run->pin_c, 1);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART: %s", esp_err_to_name(err));
        uart_driver_delete(PORT);
        return err;
    }
    hl_events_set_waker(wake);
    if (xTaskCreate(link_task, "hostlink", CONFIG_HOSTLINK_TASK_STACK, NULL, 5, &s_task) != pdPASS) {
        uart_driver_delete(PORT);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "%s on GPIO%d (TX), GPIO%d (RX)%s, %d baud, address %d", HL_MODES[run->mode], run->pin_a,
             run->pin_b, rs485 ? ", DE on pin C" : "", run->baud, run->address);
    return ESP_OK;
}

void hl_uart_set_baud(int baud)
{
    if (s_task != NULL) {
        uart_set_baudrate(PORT, baud);
    }
}
