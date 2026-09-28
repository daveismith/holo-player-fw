/*
 * The host link's events: a listener on the board's event bus (the kit's `events`) with the
 * kinds the host asked for, and a cursor -- the last happening given to the host -- from which
 * the pending count and GET /link/events come.
 *
 * Pushed (UART, `push`), a resource's change goes out as its latest state when the transport
 * next looks, and a happening as it came. Not pushed, a resource's changes are dropped (the host
 * reads the resource when it wants) and happenings wait to be fetched.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hl.h"

static SemaphoreHandle_t s_lock;
static int s_listener = -1;
static events_mask_t s_kinds;
static bool s_push;
static uint16_t s_cursor;           /* the last happening given */
static void (*s_wake)(void);
static uint32_t s_sent[EVENTS_MAX_KINDS];   /* a hash of each state as last pushed: the same again isn't */

/* FNV-1a */
static uint32_t hash_of(const char *text)
{
    uint32_t h = 2166136261u;
    for (; *text; text++) {
        h = (h ^ (uint8_t)*text) * 16777619u;
    }
    return h;
}

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

static void notified(void *ctx)
{
    (void)ctx;
    void (*wake)(void) = s_wake;
    if (wake != NULL) {
        wake();
    }
}

void hl_events_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
}

void hl_events_set_waker(void (*wake)(void))
{
    s_wake = wake;
}

void hl_events_set(events_mask_t kinds, bool push)
{
    lock();
    s_kinds = kinds;
    s_push = push;
    s_cursor = events_seq();        /* from now on: what went before isn't this host's */
    memset(s_sent, 0, sizeof(s_sent));
    if (s_listener < 0) {
        s_listener = events_listen(kinds, notified, NULL);
    } else {
        events_want(s_listener, kinds);
    }
    events_take(s_listener);        /* no stale states */
    unlock();
}

events_mask_t hl_events_kinds(void)
{
    return s_kinds;
}

bool hl_events_push(void)
{
    return s_push;
}

static void count(uint16_t seq, events_mask_t bit, const char *json, void *ctx)
{
    (void)seq;
    (void)bit;
    (void)json;
    (*(uint32_t *)ctx)++;
}

uint32_t hl_events_pending(void)
{
    uint32_t n = 0;
    lock();
    const events_mask_t happenings = s_kinds & ~events_states();
    if (happenings != 0) {
        events_since(s_cursor, happenings, count, &n, NULL);
    }
    unlock();
    return n;
}

typedef struct {
    cJSON *arr;
    size_t max, n;
    uint16_t last;          /* the seq of the last one taken */
} taking_t;

static void add_parsed(uint16_t seq, events_mask_t bit, const char *json, void *ctx)
{
    (void)bit;
    taking_t *t = ctx;
    if (t->n >= t->max) {
        return;
    }
    cJSON *e = cJSON_Parse(json);
    if (e != NULL) {
        cJSON_AddItemToArray(t->arr, e);
        t->n++;
        t->last = seq;
    }
}

cJSON *hl_events_ungiven(bool give)
{
    return hl_events_ungiven_max(SIZE_MAX, give);
}

cJSON *hl_events_ungiven_max(size_t max, bool give)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    uint32_t lost = 0;
    taking_t t = { .arr = arr, .max = max };
    lock();
    const events_mask_t happenings = s_kinds & ~events_states();
    const uint16_t seq = events_since(s_cursor, happenings, add_parsed, &t, &lost);
    if (give) {
        s_cursor = t.n < max ? seq : t.last;    /* the rest wait for the next ask */
    }
    unlock();
    cJSON_AddNumberToObject(o, "seq", seq);
    cJSON_AddNumberToObject(o, "lost", lost);
    cJSON_AddItemToObject(o, "events", arr);
    return o;
}

typedef struct {
    char *buf;
    size_t len, cap;
} text_t;

static void append(text_t *t, const char *s)
{
    const size_t n = strlen(s);
    if (t->len + n + 4 > t->cap) {
        const size_t cap = (t->len + n + 4) * 2;
        char *b = realloc(t->buf, cap);
        if (b == NULL) {
            return;
        }
        t->buf = b;
        t->cap = cap;
    }
    memcpy(t->buf + t->len, "! ", 2);
    memcpy(t->buf + t->len + 2, s, n);
    t->buf[t->len + 2 + n] = '\n';
    t->len += n + 3;
}

static void add_line(uint16_t seq, events_mask_t bit, const char *json, void *ctx)
{
    (void)seq;
    (void)bit;
    append(ctx, json);
}

cJSON *hl_events_push_happenings(void)
{
    if (s_listener < 0) {
        return NULL;
    }
    lock();
    events_take(s_listener);        /* a resource's changes don't fit a frame */
    const bool push = s_push;
    unlock();
    if (!push) {
        return NULL;
    }
    cJSON *o = hl_events_ungiven(true);
    cJSON *events = cJSON_DetachItemFromObject(o, "events");
    cJSON_Delete(o);
    if (cJSON_GetArraySize(events) == 0) {
        cJSON_Delete(events);
        return NULL;
    }
    return events;
}

char *hl_events_push_lines(size_t *len)
{
    *len = 0;
    if (s_listener < 0) {
        return NULL;
    }
    text_t t = { 0 };
    lock();
    const events_mask_t dirty = events_take(s_listener) & s_kinds;
    const bool push = s_push;
    const events_mask_t happenings = s_kinds & ~events_states();
    unlock();
    if (!push) {
        return NULL;                /* states dropped; happenings wait to be fetched */
    }
    for (events_mask_t rest = dirty; rest != 0; rest &= rest - 1) {
        cJSON *o = events_state_json(rest & -rest);
        char *json = o != NULL ? cJSON_PrintUnformatted(o) : NULL;
        cJSON_Delete(o);
        const int k = __builtin_ctz(rest);
        if (json != NULL && hash_of(json) != s_sent[k]) {
            s_sent[k] = hash_of(json);
            append(&t, json);
        }
        cJSON_free(json);
    }
    lock();
    if (happenings != 0) {
        s_cursor = events_since(s_cursor, happenings, add_line, &t, NULL);
    }
    unlock();
    *len = t.len;
    if (t.len == 0) {
        free(t.buf);
        return NULL;
    }
    return t.buf;
}

bool hl_kinds_of(const cJSON *arr, events_mask_t *out, char *bad, size_t bad_len)
{
    events_mask_t m = 0;
    const cJSON *k;
    if (!cJSON_IsArray(arr)) {
        snprintf(bad, bad_len, "(not a list)");
        return false;
    }
    cJSON_ArrayForEach(k, arr) {
        const events_mask_t bit = cJSON_IsString(k) ? events_bit(k->valuestring) : 0;
        if (bit == 0) {
            snprintf(bad, bad_len, "%s", cJSON_IsString(k) ? k->valuestring : "?");
            return false;
        }
        m |= bit;
    }
    *out = m;
    return true;
}

cJSON *hl_kinds_json(events_mask_t kinds)
{
    cJSON *arr = cJSON_CreateArray();
    for (events_mask_t rest = kinds; rest != 0; rest &= rest - 1) {
        const char *name = events_name(rest & -rest);
        if (name != NULL) {
            cJSON_AddItemToArray(arr, cJSON_CreateString(name));
        }
    }
    return arr;
}
