/*
 * Native frames: small binary messages for hosts that would rather not build JSON. See
 * manual/reference/host-protocol.md, "Native frames".
 *
 *   0x00  COBS( addr | seq | type | payload... | crc16 )  0x00
 *
 * Each message is one of the API's routes in a few bytes -- SCENE is POST /scenes/apply, LEDS is
 * PATCH /leds -- and goes through the same route function, so it behaves, and fails, as they do.
 * A reply's payload starts with a status (0 ok, 1 bad request, 2 not found, 3 not playable,
 * 4 not ready, 5 busy, 6 failed, 7 unknown type) and the pending count.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "api_core.h"
#include "board.h"
#include "board_api.h"
#include "scenes.h"
#include "hostlink.h"
#include "hl.h"

#define BODY_MAX    56      /* addr, seq, type, 48 of payload, crc16 -- and room over */
#define PAYLOAD_MAX 48

enum {
    ST_OK, ST_BAD, ST_NOT_FOUND, ST_NOT_PLAYABLE, ST_NOT_READY, ST_BUSY, ST_FAILED, ST_UNKNOWN,
};

enum {
    T_PING = 0x01, T_VERSION = 0x02, T_STATUS = 0x03,
    T_SCENE = 0x10, T_END = 0x11,
    T_SCREEN_OFF = 0x20, T_COLOUR = 0x21, T_BACKLIGHT = 0x22,
    T_LEDS = 0x30, T_HOLO = 0x40,
    T_EVENTS_SET = 0x50, T_EVENTS_GET = 0x51,
    T_RESTART = 0x7F,
    T_EVENT = 0x70,         /* from the board only: below 0x80, so never a reply (a request + 0x80) */
};

static uint8_t s_last_status;

/* ------------------------------------------------------------------ framing */

/* CRC-16/CCITT-FALSE: 0x1021, from 0xFFFF, not reflected; "123456789" is 0x29B1 */
static uint16_t crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)*p++ << 8;
        for (int i = 0; i < 8; i++) {
            crc = crc & 0x8000 ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

int hl_cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t out_max)
{
    size_t o = 0;
    for (size_t i = 0; i < len; ) {
        const uint8_t code = in[i++];
        if (code == 0) {
            return -1;
        }
        for (uint8_t k = 1; k < code; k++) {
            if (i >= len || o >= out_max) {
                return -1;
            }
            out[o++] = in[i++];
        }
        if (code < 0xFF && i < len) {
            if (o >= out_max) {
                return -1;
            }
            out[o++] = 0;
        }
    }
    return (int)o;
}

/* `body` COBS-encoded between two zeros: malloc'd */
static uint8_t *cobs_frame(const uint8_t *body, size_t len, size_t *out_len)
{
    uint8_t *out = malloc(len + len / 254 + 4);
    if (out == NULL) {
        return NULL;
    }
    size_t o = 0;
    out[o++] = 0;
    size_t code_at = o++;
    uint8_t code = 1;
    for (size_t i = 0; i < len; i++) {
        if (body[i] == 0) {
            out[code_at] = code;
            code_at = o++;
            code = 1;
        } else {
            out[o++] = body[i];
            if (++code == 0xFF) {
                out[code_at] = code;
                code_at = o++;
                code = 1;
            }
        }
    }
    out[code_at] = code;
    out[o++] = 0;
    *out_len = o;
    return out;
}

/* A whole frame: addr, seq, type, payload, crc16 (low byte first) */
static uint8_t *frame(uint8_t addr, uint8_t seq, uint8_t type, const uint8_t *payload, size_t n, size_t *out_len)
{
    uint8_t body[3 + PAYLOAD_MAX + 2];
    if (n > PAYLOAD_MAX) {
        n = PAYLOAD_MAX;
    }
    body[0] = addr;
    body[1] = seq;
    body[2] = type;
    memcpy(body + 3, payload, n);
    const uint16_t crc = crc16(body, 3 + n);
    body[3 + n] = (uint8_t)(crc & 0xFF);
    body[4 + n] = (uint8_t)(crc >> 8);
    return cobs_frame(body, 5 + n, out_len);
}

/* ------------------------------------------------------------------ through the routes */

static uint8_t status_of(const api_reply_t *r)
{
    if (r->status >= 200 && r->status < 300) {
        return ST_OK;
    }
    const cJSON *e = cJSON_GetObjectItem(r->body, "error");
    const char *code = cJSON_IsString(e) ? e->valuestring : "";
    if (strcmp(code, "bad_request") == 0 || strcmp(code, "bad_path") == 0) {
        return ST_BAD;
    }
    if (strcmp(code, "not_found") == 0 || strcmp(code, "unknown_scene") == 0 || strcmp(code, "not_running") == 0) {
        return ST_NOT_FOUND;
    }
    if (strcmp(code, "not_playable") == 0) {
        return ST_NOT_PLAYABLE;
    }
    if (strcmp(code, "not_ready") == 0) {
        return ST_NOT_READY;
    }
    if (strcmp(code, "busy") == 0) {
        return ST_BUSY;
    }
    return ST_FAILED;
}

/* Call a route with `body` (taken): the status */
static uint8_t call(api_method_t m, const char *path, cJSON *body)
{
    const api_route_t *r = api_find(m, path, NULL);
    if (r == NULL) {
        cJSON_Delete(body);
        return ST_FAILED;
    }
    if (body == NULL) {
        body = cJSON_CreateObject();
    }
    const api_req_t req = { .query = NULL, .body = body, .via = "link" };
    api_reply_t reply = r->fn(&req);
    const uint8_t st = status_of(&reply);
    cJSON_Delete(reply.body);
    cJSON_Delete(body);
    return st;
}

static int index_in(const char *const *names, size_t n, const cJSON *v)
{
    for (size_t i = 0; cJSON_IsString(v) && i < n; i++) {
        if (strcmp(v->valuestring, names[i]) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* The 12-byte status block */
static void status_block(uint8_t b[12])
{
    static const char *const SHOWING[] = { "nothing", "colour", "calibration", "image", "clip" };
    static const char *const HOLO[] = { "hold", "move", "twitch", "wag", "nod", "scan", "circle" };
    static const char *const LEDS[] = { "off", "solid", "wipe", "rainbow", "flicker" };
    memset(b, 0, 12);
    cJSON *screen = api_screen_json();
    cJSON *leds = api_leds_json();
    cJSON *holo = api_holo_json();
    cJSON *active = api_active_scene_json();
    const int showing = index_in(SHOWING, 5, cJSON_GetObjectItem(screen, "showing"));
    b[0] = showing < 0 ? 0 : (uint8_t)showing;
    uint16_t remaining = 0xFFFF;
    if (cJSON_IsObject(active)) {
        const cJSON *slot = cJSON_GetObjectItem(active, "slot");
        b[1] = cJSON_IsNumber(slot) ? (uint8_t)slot->valueint : 0xFF;
        const cJSON *rem = cJSON_GetObjectItem(active, "remaining_s");
        if (cJSON_IsNumber(rem)) {
            remaining = (uint16_t)(rem->valuedouble * 10 > 0xFFFE ? 0xFFFE : rem->valuedouble * 10);
        }
    }
    b[2] = (uint8_t)(remaining & 0xFF);
    b[3] = (uint8_t)(remaining >> 8);
    const bool ready = cJSON_IsTrue(cJSON_GetObjectItem(holo, "ready"));
    const int motion = index_in(HOLO, 7, cJSON_GetObjectItem(holo, "motion"));
    b[4] = ready && motion >= 0 ? (uint8_t)motion : 0xFF;
    const int mode = index_in(LEDS, 5, cJSON_GetObjectItem(leds, "mode"));
    b[5] = mode < 0 ? 0 : (uint8_t)mode;
    const cJSON *bl = cJSON_GetObjectItem(screen, "backlight");
    b[6] = cJSON_IsNumber(bl) ? (uint8_t)bl->valueint : 0;
    const cJSON *br = cJSON_GetObjectItem(leds, "brightness");
    b[7] = cJSON_IsNumber(br) ? (uint8_t)br->valueint : 0;
    const uint16_t seq = events_seq();
    b[8] = (uint8_t)(seq & 0xFF);
    b[9] = (uint8_t)(seq >> 8);
    b[10] = s_last_status;
#if CONFIG_BOARD_TOUCH_ENABLE
    b[11] |= board_touch_running() ? 1 : 0;
#endif
    b[11] |= ready ? 2 : 0;
    cJSON_Delete(screen);
    cJSON_Delete(leds);
    cJSON_Delete(holo);
    cJSON_Delete(active);
}

/* An event (its JSON) as an 8-byte record: false for one that has none */
static bool record_of(const cJSON *e, uint8_t r[8])
{
    static const char *const THEN[] = { "stay", "restore", "off" };
    static const char *const BY[] = { "clip", "time", "end", "replaced" };
    memset(r, 0, 8);
    const cJSON *name = cJSON_GetObjectItem(e, "event");
    const cJSON *seq = cJSON_GetObjectItem(e, "seq");
    const uint16_t s = cJSON_IsNumber(seq) ? (uint16_t)seq->valueint : 0;
    r[1] = (uint8_t)(s & 0xFF);
    r[2] = (uint8_t)(s >> 8);
    if (!cJSON_IsString(name)) {
        return false;
    }
    if (strcmp(name->valuestring, "clip_ended") == 0) {
        r[0] = 1;
        r[3] = cJSON_IsTrue(cJSON_GetObjectItem(e, "finished"));
    } else if (strcmp(name->valuestring, "scene_ended") == 0) {
        r[0] = 2;
        const cJSON *slot = cJSON_GetObjectItem(e, "slot");
        r[3] = cJSON_IsNumber(slot) ? (uint8_t)slot->valueint : 0;
        const int then = index_in(THEN, 3, cJSON_GetObjectItem(e, "then"));
        const int by = index_in(BY, 4, cJSON_GetObjectItem(e, "by"));
        r[4] = then < 0 ? 0 : (uint8_t)then;
        r[5] = by < 0 ? 0 : (uint8_t)by;
    } else if (strcmp(name->valuestring, "touch") == 0) {
        r[0] = 3;
        r[3] = strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(e, "action")) ?: "", "up") == 0;
        r[4] = (uint8_t)cJSON_GetNumberValue(cJSON_GetObjectItem(e, "x"));
        r[5] = (uint8_t)cJSON_GetNumberValue(cJSON_GetObjectItem(e, "y"));
    } else {
        return false;
    }
    return true;
}

/* Happenings (an array of event JSON) as records into `p`, at most `max`: how many */
static size_t records(const cJSON *events, uint8_t *p, size_t max)
{
    size_t n = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, events) {
        if (n < max && record_of(e, p + 8 * n)) {
            n++;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ messages */

static bool addressed(uint8_t addr, const hl_settings_t *run, bool *answer)
{
    if (addr == 0) {
        *answer = run->mode != HL_RS485;
        return true;
    }
    if (addr >= 224) {
        for (int i = 0; i < run->n_groups; i++) {
            if (run->groups[i] == addr - 223) {
                *answer = run->mode != HL_RS485;
                return true;
            }
        }
        return false;
    }
    *answer = true;
    return addr == run->address;
}

uint8_t *hl_native_handle(const uint8_t *f, size_t len, const hl_settings_t *run, size_t *out_len)
{
    hl_counters_t *c = hl_counters();
    if (len < 5 || crc16(f, len - 2) != (uint16_t)(f[len - 2] | f[len - 1] << 8)) {
        c->crc_errors++;
        return NULL;
    }
    bool answer = false;
    if (!addressed(f[0], run, &answer)) {
        return NULL;
    }
    const uint8_t seq = f[1], type = f[2];
    const uint8_t *p = f + 3;
    const size_t n = len - 5;
    uint8_t out[2 + PAYLOAD_MAX];
    size_t out_n = 0;
    uint8_t st = ST_OK;
    char hex[8];

    switch (type) {
    case T_PING: {
        const uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
        out[2] = HOSTLINK_PROTOCOL_VERSION;
        memcpy(out + 3, &up, 4);
        out_n = 5;
        break;
    }
    case T_VERSION: {
        const char *v = esp_app_get_description()->version;
        out_n = strnlen(v, 32);
        memcpy(out + 2, v, out_n);
        break;
    }
    case T_STATUS:
        status_block(out + 2);
        out_n = 12;
        break;
    case T_SCENE: {
        cJSON *scene = n >= 1 ? scene_load_slot(p[0]) : NULL;
        if (scene == NULL) {
            st = n >= 1 ? ST_NOT_FOUND : ST_BAD;
            break;
        }
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "name", cJSON_GetStringValue(cJSON_GetObjectItem(scene, "name")));
        cJSON_Delete(scene);
        st = call(API_POST, "/api/v1/scenes/apply", body);
        break;
    }
    case T_END:
        st = call(API_POST, "/api/v1/scenes/end", NULL);
        break;
    case T_SCREEN_OFF:
        st = call(API_DELETE, "/api/v1/screen", NULL);
        break;
    case T_COLOUR: {
        if (n < 3) {
            st = ST_BAD;
            break;
        }
        cJSON *body = cJSON_CreateObject();
        snprintf(hex, sizeof(hex), "#%02x%02x%02x", p[0], p[1], p[2]);
        cJSON_AddStringToObject(body, "colour", hex);
        st = call(API_POST, "/api/v1/screen/show", body);
        break;
    }
    case T_BACKLIGHT: {
        if (n < 1) {
            st = ST_BAD;
            break;
        }
        cJSON *body = cJSON_CreateObject();
        cJSON_AddNumberToObject(body, "backlight", p[0]);
        st = call(API_PATCH, "/api/v1/screen", body);
        break;
    }
    case T_LEDS: {
        static const char *const MODES[] = { "off", "solid", "wipe", "rainbow", "flicker" };
        if (n < 6 || (p[0] > 4 && p[0] != 0xFF)) {
            st = ST_BAD;
            break;
        }
        cJSON *body = cJSON_CreateObject();
        if (p[0] != 0xFF) {
            cJSON_AddStringToObject(body, "mode", MODES[p[0]]);
        }
        if (p[5] & 2) {
            snprintf(hex, sizeof(hex), "#%02x%02x%02x", p[1], p[2], p[3]);
            cJSON_AddStringToObject(body, "colour", hex);
        }
        if (p[4] != 0) {
            cJSON_AddNumberToObject(body, "brightness", p[4]);
        }
        if (p[0] == 2 || p[0] == 3) {
            cJSON_AddBoolToObject(body, "loop", p[5] & 1);
        }
        st = call(API_PATCH, "/api/v1/leds", body);
        break;
    }
    case T_HOLO: {
        static const char *const MOTIONS[] = { "center", "move", "nudge", "twitch", "wag", "nod", "scan", "circle",
                                               "stop", "off" };
        if (n < 7 || p[0] > 9) {
            st = ST_BAD;
            break;
        }
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "motion", MOTIONS[p[0]]);
        if (p[0] == 1 || p[0] == 2) {
            cJSON_AddNumberToObject(body, "x", (int8_t)p[1]);
            cJSON_AddNumberToObject(body, "y", (int8_t)p[2]);
        }
        const uint16_t duration = (uint16_t)(p[3] | p[4] << 8);
        if (duration != 0 && p[0] <= 2) {
            cJSON_AddNumberToObject(body, "duration_ms", duration);
        }
        if (p[5] != 0) {
            cJSON_AddNumberToObject(body, "range", p[5]);
        }
        if (p[6] != 0) {
            cJSON_AddNumberToObject(body, "count", p[6]);
        }
        st = call(API_POST, "/api/v1/holo/motion", body);
        break;
    }
    case T_EVENTS_SET: {
        static const char *const KINDS[] = { "clip_ended", "scene_ended", "touch" };
        if (n < 2) {
            st = ST_BAD;
            break;
        }
        events_mask_t mask = 0;
        for (int i = 0; i < 3; i++) {
            if (p[0] & (1u << i)) {
                mask |= events_bit(KINDS[i]);
            }
        }
        hl_events_set(mask, p[1] != 0);
        const uint16_t s = events_seq();
        out[2] = (uint8_t)(s & 0xFF);
        out[3] = (uint8_t)(s >> 8);
        out_n = 2;
        break;
    }
    case T_EVENTS_GET: {
        if (n < 2) {
            st = ST_BAD;
            break;
        }
        const uint16_t after = (uint16_t)(p[0] | p[1] << 8);
        cJSON *o;
        if (after == 0) {
            o = hl_events_ungiven_max(5, true);
        } else {
            char q[40];
            snprintf(q, sizeof(q), "after=%u", after);
            const api_route_t *r = api_find(API_GET, "/api/v1/events", NULL);
            const api_req_t req = { .query = q, .body = NULL, .via = "link" };
            api_reply_t reply = r != NULL ? r->fn(&req) : api_json(500, NULL);
            o = reply.body;
        }
        const cJSON *lost = cJSON_GetObjectItem(o, "lost");
        out[2] = (uint8_t)(cJSON_IsNumber(lost) && lost->valueint < 255 ? lost->valueint : 255);
        const size_t count = records(cJSON_GetObjectItem(o, "events"), out + 4, 5);
        out[3] = (uint8_t)count;
        out_n = 2 + 8 * count;
        cJSON_Delete(o);
        break;
    }
    case T_RESTART:
        st = call(API_POST, "/api/v1/restart", NULL);
        break;
    default:
        st = ST_UNKNOWN;
        break;
    }
    c->requests++;
    c->last_request_us = esp_timer_get_time();
    s_last_status = st;
    if (!answer) {
        return NULL;
    }
    const uint32_t pending = hl_events_pending();
    out[0] = st;
    out[1] = (uint8_t)(pending > 255 ? 255 : pending);
    return frame((uint8_t)run->address, seq, (uint8_t)(type | 0x80), out, 2 + out_n, out_len);
}

uint8_t *hl_native_event_frames(const hl_settings_t *run, size_t *out_len)
{
    *out_len = 0;
    cJSON *events = hl_events_push_happenings();
    if (events == NULL) {
        return NULL;
    }
    uint8_t *all = NULL;
    const cJSON *e;
    cJSON_ArrayForEach(e, events) {
        uint8_t r[8];
        size_t n;
        uint8_t *f = record_of(e, r) ? frame((uint8_t)run->address, 0, T_EVENT, r, 8, &n) : NULL;
        if (f == NULL) {
            continue;
        }
        uint8_t *grown = realloc(all, *out_len + n);
        if (grown != NULL) {
            all = grown;
            memcpy(all + *out_len, f, n);
            *out_len += n;
        }
        free(f);
    }
    cJSON_Delete(events);
    return all;
}

uint8_t *hl_native_ready(const hl_settings_t *run, size_t *out_len)
{
    const uint8_t r[8] = { 15, 0, 0, HOSTLINK_PROTOCOL_VERSION, (uint8_t)run->address };
    return frame((uint8_t)run->address, 0, T_EVENT, r, 8, out_len);
}
