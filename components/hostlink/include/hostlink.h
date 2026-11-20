/*
 * hostlink -- a controller on a wire: the board's API over UART or RS485 on P2's spare pins, one
 * request a line, and its events. See manual/use/host-link.md and
 * manual/reference/host-protocol.md; hostlink.c for the settings, hl_line.c for the protocol,
 * hl_events.c for the events, hl_uart.c for the wire.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HOSTLINK_PROTOCOL_VERSION 1

/* Load the saved settings: before the pins it holds are reserved (hostlink_pins()). */
void hostlink_init(void);

/* The pins the link will hold from the start (-1 for none): the `gpio` command's to refuse. */
void hostlink_pins(int *a, int *b, int *c);

/* Pins A and B as set (`link pins`), whatever the mode, for another user (the `can` command)
 * to borrow: NULL when no running transport (UART, RS485) uses either of them, otherwise why
 * not, naming the pins the transport runs on. The phrase is in a static buffer, good until the
 * next call: from the console's task. */
const char *hostlink_lend_pins(int *a, int *b);

/* The routes (/link, /link/events, and /events for the link): before the web server starts. */
esp_err_t hostlink_register_routes(void);

/* Start the transport: after the scenes and the web app, whose events it reports. */
esp_err_t hostlink_start(void);

/* `link` */
void hostlink_register_commands(void);

#ifdef __cplusplus
}
#endif
