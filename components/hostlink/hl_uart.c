/*
 * The host link on a UART (UART1), or on RS485 through a transceiver whose DE/RE the UART drives
 * from pin C. One task owns both directions: it reads a line or a frame, answers it, and between
 * requests sends the events the host asked to have pushed -- so an event never lands in the
 * middle of a reply, and there is nothing to lock.
 *
 * A JSON line starts with a printable character; a native frame with 0x00, and ends at the next
 * 0x00. With `auto`, both are taken; events go to the host in the form it last used.
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
static bool s_native;               /* the host's last request was a native frame */

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

static void handle_frame(const uint8_t *enc, size_t n)
{
    uint8_t body[64];
    const int len = hl_cobs_decode(enc, n, body, sizeof(body));
    if (len < 0) {
        hl_counters()->crc_errors++;
        return;
    }
    if (hl_logging()) {
        printf("link < frame");
        for (int i = 0; i < len; i++) {
            printf(" %02x", body[i]);
        }
        printf("\n");
    }
    s_native = true;
    size_t out_n;
    uint8_t *reply = hl_native_handle(body, (size_t)len, &s_run, &out_n);
    if (reply != NULL) {
        if (hl_logging()) {
            printf("link > frame of %u bytes\n", (unsigned)out_n);
        }
        put((const char *)reply, out_n);
        free(reply);
    }
}

static void push_events(void)
{
    if (s_run.mode != HL_UART) {
        return;                     /* RS485: nobody speaks unasked */
    }
    size_t len;
    if (s_native) {
        uint8_t *frames = hl_native_event_frames(&s_run, &len);
        if (frames != NULL) {
            put((const char *)frames, len);
            hl_counters()->events++;
            free(frames);
        }
        if (s_run.attn && s_run.pin_c >= 0) {
            gpio_set_level(s_run.pin_c, hl_events_pending() > 0 ? 0 : 1);
        }
        return;
    }
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
    static uint8_t frame[96];
    size_t n = 0, fn = 0;
    bool too_long = false;
    bool in_frame = false;
    uint8_t buf[128];

    if (s_run.mode == HL_UART && s_run.protocol == HL_NATIVE) {
        size_t len;
        uint8_t *ready = hl_native_ready(&s_run, &len);
        if (ready != NULL) {
            put((const char *)ready, len);
            free(ready);
        }
        s_native = true;
    } else if (s_run.mode == HL_UART) {
        char ready[128];
        snprintf(ready, sizeof(ready), "! {\"event\":\"ready\",\"firmware\":\"%s\",\"protocol\":%d,\"address\":%d}",
                 esp_app_get_description()->version, HOSTLINK_PROTOCOL_VERSION, s_run.address);
        put_line(ready);
    }
    for (;;) {
        const int got = uart_read_bytes(PORT, buf, sizeof(buf), pdMS_TO_TICKS(POLL_MS));
        for (int i = 0; i < got; i++) {
            const char ch = (char)buf[i];
            if (in_frame) {
                if (ch != '\0') {
                    if (fn < sizeof(frame)) {
                        frame[fn++] = (uint8_t)ch;
                    }
                } else if (fn > 0) {
                    if (s_run.protocol != HL_JSON) {
                        handle_frame(frame, fn);
                    }
                    in_frame = false;
                    fn = 0;
                }
                continue;               /* a zero with nothing before it: the opening one, again */
            }
            if (n == 0 && ch == '\0') {
                in_frame = true;
                fn = 0;
                continue;
            }
            if (ch == '\n') {
                if (too_long) {
                    hl_counters()->bad_lines++;
                    if (s_run.mode == HL_UART) {
                        put_line("413 {\"error\":\"too_large\",\"message\":\"a line is at most 1024 bytes\"}");
                    }
                } else if (s_run.protocol != HL_NATIVE) {
                    line[n] = '\0';
                    s_native = false;
                    handle(line);
                }
                n = 0;
                too_long = false;
            } else if (ch == '\r') {
                continue;
            } else if (n < HL_LINE_MAX - 1) {
                line[n++] = ch;
            } else {
                too_long = true;
            }
        }
        if (drain_uart_events() && (n > 0 || in_frame)) {
            hl_counters()->bad_lines++;
            n = fn = 0;                 /* what came garbled isn't a request */
            too_long = in_frame = false;
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
