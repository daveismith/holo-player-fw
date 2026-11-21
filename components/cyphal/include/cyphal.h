/*
 * cyphal -- a Cyphal/CAN node on the board's CAN controller (canbus.h), on libcanard: it takes a
 * node-ID (fixed, or by plug-and-play), sends uavcan.node.Heartbeat once a second and
 * uavcan.node.port.List every 10 s, answers uavcan.node.GetInfo, uavcan.register.Access and List,
 * and uavcan.node.ExecuteCommand, and stops if another node turns out to have its node-ID
 * (manual/use/cyphal.md, manual/reference/cyphal.md).
 *
 * The application decides when it runs and on which pins: on this board the host link hands it
 * pins A and B in `link mode can`. Nothing here knows about the board beyond canbus, so that the
 * node can move to another firmware: who the board is (the default name, the name register's
 * name, the unique ID's prefix, the hardware version) and where the tasks run are Kconfig options
 * (Kconfig), which the application pins in its sdkconfig.defaults; its own ports and what a
 * restart, a factory reset and identify do are cyphal_app_t, given to cyphal_init().
 *
 * Its settings (the node-ID, fixed or plug-and-play; the name; the bitrate; each application
 * port's ID) are registers (cyphal_registers.h), kept in NVS, namespace "cyphal", and apply from
 * the node's next start. None is a credential or a counter, so all are in a configuration backup.
 * The application may add registers of its own, kept and applied by itself (cyphal_app_t).
 *
 * Plug-and-play is uavcan.pnp.NodeIDAllocationData.1.0, as an allocatee that assumes nothing about
 * who answers: the node-ID it is given is not saved, and is asked for again at every start.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cyphal_registers.h"
#include "cyphal_util.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYPHAL_LISTEN_MS     2000    /* how long a fixed node-ID listens for itself before going online */

typedef enum {
    CYPHAL_OFF,         /* not started: nobody gave it pins */
    CYPHAL_FAILED,      /* the controller would not start, or (a restart) stop: cyphal_status_t.error says why */
    CYPHAL_WAITING,     /* plug-and-play: anonymous, asking for a node-ID until an allocator answers; then
                         * holding it until CYPHAL_PNP_LISTEN_US after the start, still listening */
    CYPHAL_LISTENING,   /* a fixed node-ID, listening for another node with it before going online */
    CYPHAL_ONLINE,      /* heartbeat and GetInfo on node_id */
    CYPHAL_IN_USE,      /* stopped: another node has its node-ID. Until the next start */
} cyphal_state_t;

/* cyphal_settings_t (the node-ID setting, the name, the bitrate) is in cyphal_util.h */

/* The services' counters, since the node started: register reads (Access with no value, or for no
 * register), writes done and refused, List calls, ExecuteCommand calls and the bad commands among them,
 * requests answered busy (no job slot free), port lists published */
typedef struct {
    uint32_t register_reads, register_writes, register_refused, register_lists, commands, commands_refused;
    uint32_t busy, port_lists;
} cyphal_services_t;

typedef struct {
    cyphal_state_t state;
    esp_err_t error;                    /* CYPHAL_FAILED: what canbus_start(), or canbus_stop(), said */
    bool stop_failed;                   /* CYPHAL_FAILED: a restart's stop failed, and the controller still runs */
    uint8_t node_id;                    /* the one it runs (or listens, or stopped) on; CYPHAL_NODE_ID_AUTO while waiting */
    bool allocated;                     /* node_id came from plug-and-play */
    cyphal_settings_t running;          /* as the node started; zeros before the first start */
    cyphal_settings_t saved;            /* from the next start */
    int tx_gpio, rx_gpio;               /* -1 before the first start */
    uint8_t unique_id[CYPHAL_UNIQUE_ID_SIZE];
    uint64_t pseudo_id;                 /* what plug-and-play asks with (cyphal_pseudo_id()) */
    uint32_t uptime_s;                  /* since the node started */
    /* Since the node started: */
    uint32_t transfers_sent;            /* queued for the bus: heartbeats, GetInfo responses, allocation requests */
    uint32_t transfers_received;        /* reassembled, CRC checked: GetInfo requests, allocation messages */
    uint32_t transfers_dropped;         /* given up on: not on the bus by their deadline, or no room to queue */
    uint32_t frames_queued;             /* handed to the controller */
    uint32_t recover_in_s;              /* bus-off, backing off: seconds until the node asks for the recovery; 0 otherwise */
    /* libcanard's heap (CONFIG_CYPHAL_HEAP_SIZE), and the node task's stack */
    uint32_t heap_used, heap_peak, heap_size, heap_failed;
    uint32_t stack_unused;              /* bytes of the task's stack never used, 0 when no task */
    cyphal_services_t services;
    uint32_t jobs_held, job_slots;      /* job slots not free (with the worker, or done and not yet sent), of all */
    uint32_t worker_stack_unused;       /* bytes of the worker's stack never used, 0 before the first start */
} cyphal_status_t;

/*
 * What the application gives the node: its own ports, whose IDs are registers; registers of its own;
 * and the means for uavcan.node.ExecuteCommand. Each function here runs on the node's worker task,
 * which may wait (a register's read excepted: cyphal_register_t).
 */
typedef struct {
    const cyphal_port_t *ports;         /* kept, not copied: a static table. NULL for none */
    size_t port_count;
    /* Its registers, after the node's own (cyphal_register_t's rules: kept, applied and saved by the
     * application, never CYPHAL_REG_NEXT_START). Kept, not copied: a static table. NULL for none */
    const cyphal_register_t *registers;
    size_t register_count;
    /* Restart the board in delay_ms (by when the response has gone). NULL: restart and factory reset are
     * bad commands */
    void (*restart)(uint32_t delay_ms);
    /* A factory reset's part beyond the node's own settings (reset first): the application's settings to
     * their defaults, or, failing, left as they were; the node then puts its own back too and answers
     * internal error, with no restart. Then the restart. NULL: the node's settings only */
    esp_err_t (*factory_reset)(void);
    /* Show which board this is, for a few seconds, and return. NULL: identify is a bad command */
    esp_err_t (*identify)(void);
} cyphal_app_t;

/* The saved settings, the register table and the lock. From app_main, once, before anything else
 * here; `app` (copied) may be NULL, for no ports and no commands. */
esp_err_t cyphal_init(const cyphal_app_t *app);

/* Start the node on these pins (TX, RX) with the saved settings: the controller at the saved
 * bitrate, then a fixed node-ID's listen, or plug-and-play's requests. The pins are the
 * caller's, checked as free, and the node's from then on. ESP_ERR_INVALID_STATE if it runs
 * already; another error if the controller would not start, which the status then shows: the
 * pins are then the caller's again, and cyphal_restart() won't take them. */
esp_err_t cyphal_start(int tx_gpio, int rx_gpio);

/* Stop, then start again on the same pins with the settings as they are now: also how a node
 * stopped by a node-ID in use starts again. ESP_ERR_INVALID_STATE if cyphal_start() never
 * succeeded (no pins to start on); any other error if the stop or the start failed, which the
 * status shows. */
esp_err_t cyphal_restart(void);

/* Settings, saved; each applies from the node's next start. A bad value is refused with
 * ESP_ERR_INVALID_ARG (and for a name, the reason in `why`); NVS's error if it wasn't saved. */
esp_err_t cyphal_set_node_id(uint8_t node_id);
esp_err_t cyphal_set_name(const char *name, char *why, size_t why_len);
esp_err_t cyphal_set_bitrate(uint32_t bitrate);
/* Back to the defaults (plug-and-play, CONFIG_CYPHAL_DEFAULT_NAME, 1 Mbit/s, every port's default
 * ID: every register), saved */
esp_err_t cyphal_reset_settings(void);

/* A register written (cyphal_registers.h's index and rules). One of the node's settings (the same as
 * above) is saved and applies from the node's next start; an application's register applies as its write
 * says. cyphal_reg_error()'s error for a refusal (the reason in `why`), or NVS's. `now` (NULL for none;
 * it may be `value` itself) gets the register's value as it is afterwards, written or not, as Access
 * answers a write; it is left alone for an index past the last. */
esp_err_t cyphal_write_register(size_t index, const uavcan_register_Value_1_0 *value, uavcan_register_Value_1_0 *now,
                                char *why, size_t why_len);

/* A register write's result as an error: ESP_OK, ESP_ERR_NOT_SUPPORTED for a register that can't be
 * written (or none), ESP_ERR_INVALID_ARG for a value refused */
esp_err_t cyphal_reg_error(cyphal_reg_result_t r);

void cyphal_status(cyphal_status_t *out);

/* `cyphal`. From app_main, after cyphal_init(). `how_to_start`: the application's words for what
 * turns the node on, for the status while it's off ("`link mode can`, then restart"). */
void cyphal_register_commands(const char *how_to_start);

#ifdef __cplusplus
}
#endif
