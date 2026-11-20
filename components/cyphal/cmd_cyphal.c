/*
 * `cyphal`: the node's status and settings on the console (manual/use/cyphal.md).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "canbus.h"
#include "cyphal.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"

static const char *s_how_to_start;     /* the application's words for what turns the node on */

static void print_node_id(uint8_t id)
{
    if (id == CYPHAL_NODE_ID_AUTO) {
        printf("plug-and-play");
    } else {
        printf("node-ID %u (fixed)", id);
    }
}

static void print_settings(const char *lead, const cyphal_settings_t *s)
{
    char rate[24];
    printf("%s", lead);
    print_node_id(s->node_id);
    printf(", %s, %s\n", s->name, canbus_format_bitrate(rate, sizeof(rate), s->bitrate));
}

static void print_status(void)
{
    cyphal_status_t st;
    cyphal_status(&st);
    switch (st.state) {
    case CYPHAL_OFF:
        printf("cyphal: off (%s)\n", s_how_to_start);
        print_settings("  settings: ", &st.saved);
        break;
    case CYPHAL_FAILED:
        if (st.stop_failed) {
            printf("cyphal: not started on GPIO%d and GPIO%d: the controller would not stop (%s)\n", st.tx_gpio,
                   st.rx_gpio, esp_err_to_name(st.error));
        } else {
            printf("cyphal: not started on GPIO%d and GPIO%d: %s%s\n", st.tx_gpio, st.rx_gpio,
                   st.error == ESP_ERR_INVALID_STATE ? "the controller is in use by " : esp_err_to_name(st.error),
                   st.error == ESP_ERR_INVALID_STATE && canbus_owner() ? canbus_owner() : "");
        }
        print_settings("  settings: ", &st.saved);
        break;
    default:
        if (st.state == CYPHAL_NO_PNP) {
            printf("cyphal: no node-ID: plug-and-play is not in this firmware yet");
        } else if (st.state == CYPHAL_LISTENING) {
            printf("cyphal: listening, before going online as node %u (fixed)", st.node_id);
        } else if (st.state == CYPHAL_IN_USE) {
            printf("cyphal: stopped: node-ID %u is in use by another node", st.node_id);
        } else {
            printf("cyphal: node %u (%s)", st.node_id, st.allocated ? "allocated" : "fixed");
        }
        char rate[24];
        printf(", %s, %s on GPIO%d (TX), GPIO%d (RX)\n", st.running.name,
               canbus_format_bitrate(rate, sizeof(rate), st.running.bitrate), st.tx_gpio, st.rx_gpio);
        break;
    }
    printf("  unique id ");
    for (int i = 0; i < CYPHAL_UNIQUE_ID_SIZE; i++) {
        printf("%02x", st.unique_id[i]);
    }
    printf("\n");
    if (st.state == CYPHAL_OFF || st.state == CYPHAL_FAILED) {
        return;
    }
    canbus_status_t bus;
    canbus_status(&bus);
    printf("  bus: %s, tx errors %u, rx errors %u, bus-off %lu times\n", canbus_state_name(bus.state), bus.tx_errors,
           bus.rx_errors, (unsigned long)bus.bus_off);
    printf("  transfers: sent %lu, received %lu; frames: sent %lu, received %lu; dropped %lu\n",
           (unsigned long)st.transfers_sent, (unsigned long)st.transfers_received, (unsigned long)bus.tx_frames,
           (unsigned long)bus.rx_frames, (unsigned long)st.transfers_dropped);
    printf("  up %lu s; frames given to the controller %lu, not sent %lu, waiting %lu; not read (queue full) %lu\n",
           (unsigned long)st.uptime_s, (unsigned long)st.frames_queued, (unsigned long)bus.tx_failed,
           (unsigned long)bus.tx_pending, (unsigned long)bus.rx_dropped);
    printf("  memory: libcanard heap %lu of %lu bytes (%lu at most%s), stack %lu bytes never used; "
           "internal heap %u free, %u lowest\n", (unsigned long)st.heap_used, (unsigned long)st.heap_size,
           (unsigned long)st.heap_peak, st.heap_failed ? ", and it ran out" : "", (unsigned long)st.stack_unused,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    if (!cyphal_settings_equal(&st.saved, &st.running)) {
        print_settings("  from the next start (`cyphal restart`): ", &st.saved);
    }
}

/* 0-127, digits only */
static bool parse_node_id(const char *s, uint8_t *out)
{
    char *end = NULL;
    const unsigned long v = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || s[0] < '0' || s[0] > '9' || v > CYPHAL_NODE_ID_MAX) {
        return false;
    }
    *out = (uint8_t)v;
    return true;
}

static int saved(esp_err_t err, const char *what)
{
    if (err != ESP_OK) {
        printf("cyphal: not saved: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("cyphal: %s, from the node's next start (`cyphal restart`, or a restart of the board)\n", what);
    return 0;
}

static int cmd_cyphal(int argc, char **argv)
{
    if (argc == 1) {
        print_status();
        return 0;
    }
    const char *sub = argv[1];
    char what[96];
    if (strcmp(sub, "id") == 0 && argc == 3) {
        uint8_t id = CYPHAL_NODE_ID_AUTO;
        if (strcmp(argv[2], "auto") != 0 && !parse_node_id(argv[2], &id)) {
            printf("cyphal: a node-ID is 0 to 127, or `auto` for plug-and-play\n");
            return 1;
        }
        if (id == CYPHAL_NODE_ID_AUTO) {
            snprintf(what, sizeof(what), "plug-and-play (not in this firmware yet: the node will send nothing)");
        } else {
            snprintf(what, sizeof(what), "node-ID %u, fixed", id);
        }
        return saved(cyphal_set_node_id(id), what);
    }
    if (strcmp(sub, "name") == 0 && argc == 3) {
        const bool reset = strcmp(argv[2], "--reset") == 0;
        const char *name = reset ? CONFIG_CYPHAL_DEFAULT_NAME : argv[2];
        char why[128];
        const esp_err_t err = cyphal_set_name(name, why, sizeof(why));
        if (err == ESP_ERR_INVALID_ARG) {
            printf("cyphal: not a name: %s\n", why);
            return 1;
        }
        snprintf(what, sizeof(what), "the name %s%s", reset ? "back to " : "", name);
        return saved(err, what);
    }
    if (strcmp(sub, "bitrate") == 0 && argc == 3) {
        uint32_t bitrate = 0;
        const esp_err_t err = canbus_parse_bitrate(argv[2], &bitrate) ? cyphal_set_bitrate(bitrate) : ESP_ERR_INVALID_ARG;
        if (err == ESP_ERR_INVALID_ARG) {
            printf("cyphal: the bitrate is 125k, 250k, 500k or 1m\n");
            return 1;
        }
        return saved(err, canbus_format_bitrate(what, sizeof(what), bitrate));
    }
    if (strcmp(sub, "restart") == 0 && argc == 2) {
        const esp_err_t err = cyphal_restart();
        if (err == ESP_ERR_INVALID_STATE) {
            printf("cyphal: off, so there is nothing to restart (%s)\n", s_how_to_start);
            return 1;
        }
        print_status();
        return err == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "reset") == 0 && argc == 2) {
        return saved(cyphal_reset_settings(), "plug-and-play, the default name and 1 Mbit/s");
    }
    printf("usage: cyphal [id <0-127>|auto | name <name>|--reset | bitrate <125k|250k|500k|1m> | restart | reset]\n");
    return 1;
}

void cyphal_register_commands(const char *how_to_start)
{
    s_how_to_start = how_to_start ? how_to_start : "not started by this firmware";
    const esp_console_cmd_t cmd = {
        .command = "cyphal",
        .help = "The Cyphal node on the CAN bus: alone, its state, node-ID, name, unique ID, the bus and the counters. "
                "`id` sets a fixed node-ID (or `auto`: plug-and-play), `name` the name GetInfo reports, `bitrate` the "
                "bus's; each from the node's next start. `restart` starts it again with the settings as they are; "
                "`reset` puts the settings back to the defaults.",
        .hint = "[id <0-127>|auto | name <name>|--reset | bitrate <125k|250k|500k|1m> | restart | reset]",
        .func = cmd_cyphal,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
