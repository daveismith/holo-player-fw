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

/* Load the saved settings. hostlink_start() calls it too. */
void hostlink_init(void);

#define HOSTLINK_PIN_ROLES 3    /* pins A, B and C */

/* A pin the transport takes, and what for, in the `gpio` command's words */
typedef struct {
    int gpio;           /* -1: the role takes no pin */
    const char *what;   /* "host link UART TX", "CAN TX, for the Cyphal node (`link mode can`)"; kept for good */
} hostlink_pin_t;

/* `on_pins` is told, from hostlink_start(), the pins the transport takes (each role's; -1 for one it
 * doesn't use) and whether it holds them: `held` false if it (or the CAN user) failed to start.
 * Not called when the link is off. The pins change only at a restart. Before hostlink_start(). */
void hostlink_set_on_pins(void (*on_pins)(const hostlink_pin_t pins[HOSTLINK_PIN_ROLES], bool held));

/* Pins A and B as set (`link pins`), whatever the mode, for another user (the `can` command)
 * to borrow: NULL when no running transport (UART, RS485, CAN) uses either of them, otherwise why
 * not, naming who holds the pins and which ("the Cyphal node has GPIO21 (TX), GPIO33 (RX) ...").
 * The phrase is in a static buffer, good until the next call: from the console's task. */
const char *hostlink_lend_pins(int *a, int *b);

/* `link mode can`: the link runs no protocol of its own, it gives pins A (TX) and B (RX) to
 * `start`, called from hostlink_start(), which starts the application's CAN user. `who` names
 * that user in the link's words ("the Cyphal node") and `status_cmd` is the console command that
 * shows it ("cyphal"; NULL for none); both static strings. Before hostlink_register_commands(),
 * whose help names `who`. Without it, `can` mode starts nothing. In `can` mode the protocol's
 * settings (protocol, baud, address, groups, reply delay, events) are kept for UART and RS485 but
 * not used, and `link` leaves them out. If `start` fails, the link holds no pins
 * (hostlink_set_on_pins()) and `link` says it didn't start. */
void hostlink_set_can_user(esp_err_t (*start)(int tx, int rx), const char *who, const char *status_cmd);

/* The routes (/link, /link/events, and /events for the link): before the web server starts. */
esp_err_t hostlink_register_routes(void);

/* Start the transport: after the scenes and the web app, whose events it reports. */
esp_err_t hostlink_start(void);

/* `link` */
void hostlink_register_commands(void);

#ifdef __cplusplus
}
#endif
