/*
 * What main/holo_cyphal*.c share, and nobody else (holo_cyphal.h has the outside's): holo_cyphal.c, the core (the
 * poll: setpoints carried out, Status and Axes); holo_cyphal_services.c (the request handlers and the handler
 * table); holo_cyphal_events.c (the node's listener of the kit's events, and Event). All of it on the node's worker
 * (or its second worker, for the slow handlers) but the setpoint's mailbox.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cyphal.h"
#include "holo.h"
#include "r2/holo/Event_0_1.h"

#define POLL_MS         50      /* the poll's longest sleep while a Status may go: the holo's change is seen this soon */
#define POLL_SOON_MS    10      /* Event: the happenings still to go read again, or one refused offered again, this soon */

/* ---- the core (holo_cyphal.c) */

/* MotionSpec's kind for each of the engine's: the two number them differently (the engine has SCAN at 4, MotionSpec
 * and the host link's HOLO frame at 6). One table, looked up both ways */
extern const uint8_t HOLO_SPEC_OF_KIND[HOLO_OFF + 1];

/* A motion asked for after a setpoint replaces the first setpoint's second go (s_again): not to be applied again */
void holo_setpoint_forget(void);

/* The setpoint subscription's on_message, for the table: on the node's task, into the mailbox, the worker woken */
void holo_setpoint_message(const uint8_t *payload, size_t size, uint8_t from);

/* ---- the lists (holo_cyphal_lists.c): r2.holo.ListScenes' and MediaInfo's handler functions, for the table in
 * holo_cyphal_services.c; the file services' table is holo_cyphal.h's HOLO_CYPHAL_FIXED */

bool holo_list_scenes_decode(const uint8_t *payload, size_t size, void *in);
size_t holo_list_scenes_work(const void *in, uint8_t from, uint8_t *out, size_t room);
size_t holo_list_scenes_busy(uint8_t *out, size_t room);
bool holo_media_info_decode(const uint8_t *payload, size_t size, void *in);
size_t holo_media_info_work(const void *in, uint8_t from, uint8_t *out, size_t room);
size_t holo_media_info_busy(uint8_t *out, size_t room);

/* ---- the listener and Event (holo_cyphal_events.c) */

/* Status's parts that signal their changes through the listener: read again only when marked */
enum { HOLO_DIRTY_RING = 1, HOLO_DIRTY_SCREEN = 2, HOLO_DIRTY_SCENE = 4, HOLO_DIRTY_ALL = 7 };

#define HOLO_EVENT_DEPTH    CYPHAL_PUBLISH_DEPTH_MAX    /* Event's mailbox: a scene applied makes four at once */
#define HOLO_EVENT_BATCH    HOLO_EVENT_DEPTH    /* happenings copied from the store in one walk: what the mailbox holds */
#define HOLO_EVENT_JSON_MAX 512     /* a happening as the store keeps it, its JSON: a 159-byte path's clip_started is 250 */

/* Event's, in the poll's state (PSRAM) */
typedef struct {
    bool off;                   /* its port is off this run (cyphal_publish() said so): nothing until the next */
    uint16_t cursor;            /* the last happening handed to Event's mailbox (or the board's latest at going online) */
    /* An Event refused by a full mailbox, kept serialized: offered again, alone, until it is taken, at a growing
     * interval while the refusals go on; the store isn't read meanwhile */
    bool pending;
    uint16_t pending_seq;
    size_t pending_size;
    uint8_t refusals;           /* in a row */
    int64_t retry_us;           /* not before (this, or a happening that couldn't be parsed) */
    uint8_t no_memory;          /* tries in a row at the happening after the cursor, cJSON out of memory to parse it */
    uint8_t buf[r2_holo_Event_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_];
    r2_holo_Event_0_1 event;
    /* One walk of the store: the happenings after the cursor, oldest first, copied */
    size_t got;
    struct {
        uint16_t seq;
        bool cut;               /* didn't fit */
        char json[HOLO_EVENT_JSON_MAX];
    } batch[HOLO_EVENT_BATCH];
} holo_events_t;

/* The kinds the listener wants, looked at once a second (touch's is declared only when touch starts); the listener
 * made the first time the ring's kind exists */
void holo_events_look(int64_t now);
/* The parts (HOLO_DIRTY_*) to read again, taken: marked by their change events, or all with no listener */
uint32_t holo_events_dirty(void);
/* Parts to read again at the next look, whatever the listener says: going online, a read that couldn't be made */
void holo_events_mark(uint32_t parts);
/* The node just online: Event's port looked at again, nothing kept, and what happened before isn't sent */
void holo_events_online(holo_events_t *e);
/* Every happening after the cursor into Event's mailbox, oldest first. The milliseconds until the poll is to look
 * again */
uint32_t holo_events_publish(holo_events_t *e, int64_t now);
/* The latest happening's number for Status: the last handed to Event's mailbox (the board's latest with Event off,
 * which never will be) */
uint16_t holo_events_last(const holo_events_t *e);
