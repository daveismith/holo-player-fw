/*
 * The holo on Cyphal: the node's listener of the kit's events, and r2.holo.Event (manual/reference/cyphal.md). On the
 * node's worker; see holo_cyphal_internal.h.
 *
 * One listener (of EVENTS_MAX_SINKS) does for everything here. Its state kinds -- the ring's ("leds"), the screen's
 * and the scene's -- mark Status's parts to be read again (holo_events_dirty()); the board's re-marks of the screen
 * and the scene every few seconds while a clip or a scene runs change nothing that counts, so they don't hurry a
 * Status. Each kind, and each happening Event carries, wakes the poll. Without a listener (none free), every part is
 * read on every look and the store every poll.
 *
 * Event is published from the board's own store of happenings (the kit's events: the last EVENTS_KEPT, each with its
 * sequence number), from a cursor, the last one handed to Event's mailbox. On a poll the happenings after it, oldest
 * first, are copied from the store in one walk (HOLO_EVENT_BATCH at most), each read back (happening_of()), made an
 * r2.holo.Event with its own number, serialized, and given to the mailbox, a queue (EVENT_DEPTH) that never
 * replaces. One the mailbox refuses, full, is kept serialized and offered again, alone, until it is taken -- POLL_SOON_MS
 * later, then at twice the interval each refusal, to EVENT_RETRY_MAX_MS -- and the store is read again only once it
 * has gone. So nothing is lost while the store still has it, and a happening the store no longer has (EVENTS_KEPT
 * later) shows as a jump in `seq`, as one this receiver missed would. The cursor starts at the board's latest as the
 * node goes online: what happened before isn't sent. A happening whose JSON can't be parsed for want of memory is
 * tried again shortly, the cursor left before it; one no Event carries (another kind, or JSON cut short) is passed.
 */
#include <string.h>
#include "cJSON.h"
#include "cyphal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "events.h"
#include "happening.h"
#include "holo_cyphal_internal.h"
#include "holo_cyphal_util.h"
#include "r2/holo/ports.h"

static const char *TAG = "holo_cyphal";

#define SINK_LOOK_US        1000000 /* how often the listener's kinds are looked at again: touch's is declared when it starts */
#define EVENT_RETRY_MAX_MS  160     /* the longest between offers of a refused Event, or tries of an unparsed happening */
#define EVENT_NO_MEMORY_TRIES 30    /* a happening tried this often with no memory to parse it (about 4 s) is skipped */

/* The kinds, by name: those declared yet (touch's only once touch has started). The state kinds first, in
 * HOLO_DIRTY_*'s order */
static const char *const SINK_KINDS[] = { "leds", "screen", "scene", "clip_started", "clip_ended", "scene_started",
                                          "scene_ended", "touch" };
#define PARTS 3
static int s_sink = -1;
static bool s_sink_tried;
static events_mask_t s_sink_want;
static int64_t s_sink_looked_us;
static events_mask_t s_part_bit[PARTS];         /* the state kinds', as last looked at */
static events_mask_t s_happenings;              /* the happenings', as last looked at */
static uint32_t s_dirty = HOLO_DIRTY_ALL;       /* __atomic: parts to read again, whatever the listener says */

/* A kind changed, or something happened, says the listener: on the task that did it, which this must not hold up */
static void notified(void *ctx)
{
    (void)ctx;
    cyphal_wake_worker();
}

void holo_events_look(int64_t now)
{
    if (s_sink_looked_us != 0 && now - s_sink_looked_us < SINK_LOOK_US) {
        return;
    }
    s_sink_looked_us = now;
    events_mask_t want = 0;
    for (size_t i = 0; i < sizeof(SINK_KINDS) / sizeof(SINK_KINDS[0]); i++) {
        const events_mask_t bit = events_bit(SINK_KINDS[i]);
        want |= bit;
        if (i < PARTS) {
            s_part_bit[i] = bit;
        }
    }
    s_happenings = want & ~events_states();
    if (s_sink < 0 && !s_sink_tried && s_part_bit[0] != 0) {
        s_sink_tried = true;
        s_sink = events_listen(want, notified, NULL);
        s_sink_want = want;
        if (s_sink < 0) {
            ESP_LOGW(TAG, "no events listener free: the ring, the screen and the scene are read on every look at the "
                          "holo, and happenings every poll");
        }
    } else if (s_sink >= 0 && want != s_sink_want) {
        events_want(s_sink, want);
        s_sink_want = want;
    }
}

uint32_t holo_events_dirty(void)
{
    uint32_t parts = __atomic_exchange_n(&s_dirty, 0, __ATOMIC_ACQ_REL);
    if (s_sink < 0) {
        return HOLO_DIRTY_ALL;
    }
    const events_mask_t took = events_take(s_sink);
    for (int i = 0; i < PARTS; i++) {
        if (took & s_part_bit[i]) {
            parts |= 1u << i;
        }
    }
    return parts;
}

void holo_events_mark(uint32_t parts)
{
    __atomic_or_fetch(&s_dirty, parts, __ATOMIC_RELEASE);
}

void holo_events_online(holo_events_t *e)
{
    e->off = false;
    e->pending = false;
    e->refusals = 0;
    e->no_memory = 0;
    e->cursor = events_seq();
}

uint16_t holo_events_last(const holo_events_t *e)
{
    return e->off ? events_seq() : e->cursor;
}

/* events_since()'s callback, with the store locked: the happenings after the cursor, copied, as many as fit */
static void take(uint16_t seq, events_mask_t bit, const char *json, void *ctx)
{
    (void)bit;
    holo_events_t *e = ctx;
    if (e->got < HOLO_EVENT_BATCH) {
        e->batch[e->got].seq = seq;
        e->batch[e->got].cut = strlcpy(e->batch[e->got].json, json, HOLO_EVENT_JSON_MAX) >= HOLO_EVENT_JSON_MAX;
        e->got++;
    }
}

/* A happening's JSON, as the store keeps it (whole: one cut short is no Event's), as an r2.holo.Event with its
 * number, serialized into e->buf: its size; 0 for one no Event carries; SERIALIZE_NO_MEMORY when it couldn't be
 * parsed. The store's JSON is cJSON's own printing, so a parse that fails is cJSON out of memory, not the
 * happening's fault: it is to be tried again */
#define SERIALIZE_NO_MEMORY SIZE_MAX
static size_t serialize(holo_events_t *e, const char *json, uint16_t seq)
{
    cJSON *o = cJSON_Parse(json);
    if (o == NULL) {
        return SERIALIZE_NO_MEMORY;
    }
    happening_t h;
    size_t size = sizeof(e->buf);
    const bool ok = happening_of(o, &h) && holo_event(&h, seq, &e->event) &&
                    r2_holo_Event_0_1_serialize_(&e->event, e->buf, &size) >= 0;
    cJSON_Delete(o);
    return ok ? size : 0;
}

/* Into Event's mailbox: true if it was taken. A refusal (full, or the node just gone offline) keeps it pending */
static bool offer(holo_events_t *e, uint16_t seq, size_t size, int64_t now)
{
    const esp_err_t err = cyphal_publish(R2_HOLO_PORT_EVENT, e->buf, size);
    if (err == ESP_OK) {
        e->cursor = seq;
        e->pending = false;
        e->refusals = 0;
        return true;
    }
    if (err == ESP_ERR_NOT_SUPPORTED) {
        e->off = true;              /* its port is off: nothing more until the node's next start */
        e->pending = false;
        return false;
    }
    const unsigned shift = e->refusals < 4 ? e->refusals : 4;
    e->pending = true;
    e->pending_seq = seq;
    e->pending_size = size;
    e->refusals = e->refusals < UINT8_MAX ? e->refusals + 1 : UINT8_MAX;
    e->retry_us = now + (int64_t)(POLL_SOON_MS << shift) * 1000;
    return false;
}

/* The milliseconds until the refused Event is to be offered again */
static uint32_t retry_ms(const holo_events_t *e, int64_t now)
{
    const int64_t ms = (e->retry_us - now + 999) / 1000;
    return ms < 1 ? 1 : ms > EVENT_RETRY_MAX_MS ? EVENT_RETRY_MAX_MS : (uint32_t)ms;
}

uint32_t holo_events_publish(holo_events_t *e, int64_t now)
{
    if (e->off || s_happenings == 0) {
        return POLL_MS;
    }
    if (e->no_memory > 0 && now < e->retry_us) {
        return retry_ms(e, now);    /* a happening that couldn't be parsed: not before its next try */
    }
    if (e->pending) {
        if (now < e->retry_us) {
            return retry_ms(e, now);
        }
        if (!offer(e, e->pending_seq, e->pending_size, now)) {
            return e->off ? POLL_MS : retry_ms(e, now);
        }
    }
    e->got = 0;
    uint32_t lost = 0;
    events_since(e->cursor, s_happenings, take, e, &lost);
    if (lost > 0) {
        ESP_LOGW(TAG, "event: %lu happenings gone from the store before they could go: a jump in seq",
                 (unsigned long)lost);
    }
    for (size_t i = 0; i < e->got; i++) {
        const uint16_t seq = e->batch[i].seq;
        size_t size = e->batch[i].cut ? 0 : serialize(e, e->batch[i].json, seq);
        if (size == SERIALIZE_NO_MEMORY) {
            /* The cursor stays: the store is read again from here shortly, at a growing interval. Given up only
             * after EVENT_NO_MEMORY_TRIES (some seconds), so that a happening that never parses can't hold Event up
             * for good */
            const unsigned shift = e->no_memory < 4 ? e->no_memory : 4;
            if (e->no_memory++ == 0) {
                ESP_LOGW(TAG, "event %u: no memory to read the happening; trying again", seq);
            }
            if (e->no_memory < EVENT_NO_MEMORY_TRIES) {
                e->retry_us = now + (int64_t)(POLL_SOON_MS << shift) * 1000;
                return retry_ms(e, now);
            }
            ESP_LOGW(TAG, "event %u: still no memory to read the happening after %u tries; skipped", seq,
                     (unsigned)e->no_memory);
            size = 0;
        } else if (size == 0) {
            ESP_LOGW(TAG, "event %u: not one Event carries; skipped", seq);
        }
        e->no_memory = 0;
        if (size == 0) {
            e->cursor = seq;
            continue;
        }
        if (!offer(e, seq, size, now)) {
            return e->off ? POLL_MS : retry_ms(e, now);    /* the rest of the walk is read again once it has gone */
        }
    }
    return e->got == HOLO_EVENT_BATCH ? POLL_SOON_MS : POLL_MS;     /* a full walk: more may wait */
}
