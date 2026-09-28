/*
 * What the host link's files share, and nobody else.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#include "events.h"

typedef enum { HL_OFF, HL_UART, HL_RS485, HL_I2C } hl_mode_t;
typedef enum { HL_AUTO, HL_JSON, HL_NATIVE } hl_proto_t;

#define HL_MAX_GROUPS   8
#define HL_LINE_MAX     1024    /* a JSON line, with its line feed */

extern const char *const HL_MODES[];
extern const char *const HL_PROTOS[];

/* The link's settings: saved as LinkSettings (the OpenAPI description), in NVS. */
typedef struct {
    hl_mode_t mode;
    hl_proto_t protocol;
    int pin_a, pin_b, pin_c;        /* pin_c -1 for none */
    int baud;
    int address;
    uint8_t groups[HL_MAX_GROUPS];
    int n_groups;
    int reply_delay_ms;
    bool attn;
    int i2c_address;
    bool general_call;
    char kinds[192];                /* the events it reports from the start: comma-separated names */
    bool push;
} hl_settings_t;

typedef struct {
    uint32_t requests, events, bad_lines, crc_errors, framing_errors, overruns, ignored;
    int64_t last_request_us;        /* 0 for none */
} hl_counters_t;

/* ---- hostlink.c ---- */

/* The settings running now: what the transport reads. A copy, under the lock. */
void hl_running(hl_settings_t *out);
hl_counters_t *hl_counters(void);
bool hl_logging(void);
/* A new baud rate to take after the reply now being sent (0: none). The transport takes it. */
int hl_take_baud(void);

/* ---- hl_events.c ---- */

void hl_events_init(void);
/* What the link reports, and whether it pushes (UART). `kinds` 0: nothing. */
void hl_events_set(events_mask_t kinds, bool push);
events_mask_t hl_events_kinds(void);
bool hl_events_push(void);
/* Happenings of its kinds not yet given to the host */
uint32_t hl_events_pending(void);
/* {"seq", "lost", "events"}: the happenings not yet given; `give` counts them as given. */
cJSON *hl_events_ungiven(bool give);
/* As hl_events_ungiven(), at most `max`: the rest stay ungiven. */
cJSON *hl_events_ungiven_max(size_t max, bool give);
/* For a native host, pushed: the happenings not yet given (a JSON array, given now), or NULL. */
cJSON *hl_events_push_happenings(void);
/* For the transport: every event to push now, as JSON lines ("! {...}\n"), appended to `out`
 * (malloc'd, *len bytes); NULL when none. Also tells whether pending changed. */
char *hl_events_push_lines(size_t *len);
/* Something happened: the transport is woken with this. */
void hl_events_set_waker(void (*wake)(void));
/* A kinds array (["leds","scene_ended"]) as a mask: false with the first unknown name. */
bool hl_kinds_of(const cJSON *arr, events_mask_t *out, char *bad, size_t bad_len);
/* A mask as a JSON array of names */
cJSON *hl_kinds_json(events_mask_t kinds);

/* ---- hl_line.c ---- */

/* One JSON line (no line feed), as it came. The reply line (no line feed, malloc'd), or NULL
 * when there is none to send (another board's address, a broadcast on RS485, ...). */
char *hl_line_handle(const char *line, const hl_settings_t *run);

/* ---- hl_native.c ---- */

/* One native frame, COBS-decoded (addr seq type payload crc16). The reply frame, encoded and
 * delimited (malloc'd, *out_len bytes), or NULL for none. */
uint8_t *hl_native_handle(const uint8_t *frame, size_t len, const hl_settings_t *run, size_t *out_len);
/* COBS-decode `in` (no delimiters) into `out`: the decoded length, or -1 when it isn't COBS. */
int hl_cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t out_max);
/* The pushed happenings as 0x70 frames, and `ready` as one */
uint8_t *hl_native_event_frames(const hl_settings_t *run, size_t *out_len);
uint8_t *hl_native_ready(const hl_settings_t *run, size_t *out_len);

/* ---- hl_uart.c ---- */

esp_err_t hl_uart_start(const hl_settings_t *run);
void hl_uart_set_baud(int baud);
