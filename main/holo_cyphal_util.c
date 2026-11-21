/*
 * The holo's Cyphal interface, its pure parts. See holo_cyphal_util.h.
 */
#include "holo_cyphal_util.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* state.Holo's motion numbers: the engine's names */
static const char *const MOTION_NAMES[] = { "hold", "move", "twitch", "wag", "nod", "scan", "circle" };

void holo_motion_args(const r2_holo_MotionSpec_0_1 *m, holo_motion_args_t *out)
{
    memset(out, 0, sizeof(*out));
    out->kind = m->kind;
    out->duration_ms = -1;
    switch (m->kind) {
    case r2_holo_MotionSpec_0_1_MOVE:
    case r2_holo_MotionSpec_0_1_NUDGE:
        out->x = m->x;
        out->y = m->y;
        /* a duration, as a centre has */
        __attribute__((fallthrough));
    case r2_holo_MotionSpec_0_1_CENTER:
        if (m->duration_ms.count > 0) {
            out->duration_ms = m->duration_ms.elements[0];
        }
        break;
    case r2_holo_MotionSpec_0_1_TWITCH:
    case r2_holo_MotionSpec_0_1_SCAN:
        out->range = m->range;
        out->time_s = (float)(m->time_limit_ms / 1000.0);
        if (m->kind == r2_holo_MotionSpec_0_1_TWITCH) {
            out->interval_min_s = (float)(m->interval_min_ms / 1000.0);
            out->interval_max_s = (float)(m->interval_max_ms / 1000.0);
        } else {
            out->period_ms = m->period_ms;
        }
        break;
    case r2_holo_MotionSpec_0_1_WAG:
    case r2_holo_MotionSpec_0_1_NOD:
    case r2_holo_MotionSpec_0_1_CIRCLE:
        out->range = m->range;
        out->count = m->count;
        out->period_ms = m->period_ms;
        break;
    default:        /* stop, off: nothing; a kind that is none: its number, for the check to refuse */
        break;
    }
}

/* Seconds as whole milliseconds, held to what `bits` holds */
static uint32_t ms_of(float s, unsigned bits)
{
    const double ms = floor((double)s * 1000.0 + 0.5);
    const double max = (double)((1UL << bits) - 1);
    return ms <= 0 ? 0 : ms >= max ? (uint32_t)max : (uint32_t)ms;
}

void holo_motion_spec(const holo_motion_args_t *m, r2_holo_MotionSpec_0_1 *out)
{
    memset(out, 0, sizeof(*out));
    out->kind = m->kind > r2_holo_MotionSpec_0_1_OFF ? r2_holo_MotionSpec_0_1_STOP : (uint8_t)m->kind;
    out->range = (uint8_t)(m->range < 0 ? 0 : m->range > 127 ? 127 : m->range);
    out->count = (uint8_t)(m->count < 0 ? 0 : m->count > 31 ? 31 : m->count);
    out->x = (int16_t)(m->x < INT16_MIN ? INT16_MIN : m->x > INT16_MAX ? INT16_MAX : m->x);
    out->y = (int16_t)(m->y < INT16_MIN ? INT16_MIN : m->y > INT16_MAX ? INT16_MAX : m->y);
    out->period_ms = (uint16_t)(m->period_ms < 0 ? 0 : m->period_ms > 65535 ? 65535 : m->period_ms);
    out->interval_min_ms = ms_of(m->interval_min_s, 20);
    out->interval_max_ms = ms_of(m->interval_max_s, 20);
    out->time_limit_ms = ms_of(m->time_s, 22);
    if (m->duration_ms >= 0) {
        out->duration_ms.count = 1;
        out->duration_ms.elements[0] = (uint16_t)(m->duration_ms > 65535 ? 65535 : m->duration_ms);
    }
}

uint8_t holo_motion_number(const char *name)
{
    for (size_t i = 0; name != NULL && i < sizeof(MOTION_NAMES) / sizeof(MOTION_NAMES[0]); i++) {
        if (strcmp(name, MOTION_NAMES[i]) == 0) {
            return (uint8_t)i;
        }
    }
    return 0;
}

bool holo_light_change(const r2_holo_Light_Request_0_1 *q, holo_light_change_t *out, char *why, size_t why_len)
{
    memset(out, 0, sizeof(*out));
    switch (q->mode) {
    case r2_holo_Light_Request_0_1_OFF:     out->mode = HOLO_RING_OFF; break;
    case r2_holo_Light_Request_0_1_ON:      out->mode = HOLO_RING_SOLID; break;
    case r2_holo_Light_Request_0_1_PULSE:   out->mode = HOLO_RING_PULSE; break;
    case r2_holo_Light_Request_0_1_FLICKER: out->mode = HOLO_RING_FLICKER; break;
    case r2_holo_Light_Request_0_1_BLINK:   out->mode = HOLO_RING_BLINK; break;
    case r2_holo_Light_Request_0_1_LEIA:    out->mode = HOLO_RING_FLICKER; out->leia = out->centre = true; break;
    default:
        snprintf(why, why_len, "`mode` is off, on, pulse, flicker, blink or leia");
        return false;
    }
    if (q->colour.count > 0) {
        out->has_colour = true;
        out->rgb[0] = q->colour.elements[0].r;
        out->rgb[1] = q->colour.elements[0].g;
        out->rgb[2] = q->colour.elements[0].b;
    }
    if ((out->mode == HOLO_RING_PULSE || out->mode == HOLO_RING_BLINK) && q->period_ms != 0) {
        out->has_period = true;
        out->period_ms = q->period_ms;
    }
    /* The level: as given, 100 without, but off has none and a leia's is full */
    if (out->mode != HOLO_RING_OFF) {
        out->has_level = true;
        out->level = !out->leia && q->level.count > 0 ? q->level.elements[0] : 100;
    }
    const uint32_t ms = q->time_limit_ms != 0 ? q->time_limit_ms : out->leia ? HOLO_LEIA_MS : 0;
    out->has_time = ms != 0;
    out->time_s = ms / 1000.0;
    return true;
}

void holo_error_message(r2_holo_Error_0_1 *e, const char *text)
{
    if (text == NULL) {
        e->message.count = 0;           /* and no memcpy() from NULL, even of nothing */
        return;
    }
    size_t n = strlen(text);
    if (n > r2_holo_Error_0_1_MESSAGE_CAPACITY) {
        n = r2_holo_Error_0_1_MESSAGE_CAPACITY;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) {     /* text[n] continues a character */
            n--;
        }
    }
    memcpy(e->message.elements, text, n);
    e->message.count = n;
}

r2_holo_Reason_0_1 holo_reason(const char *why, const char *axis, const char *h, const char *v)
{
    static const struct { const char *why; uint8_t code; } WHYS[] = {
        { "not fitted", r2_holo_Reason_0_1_NOT_FITTED }, { "not armed", r2_holo_Reason_0_1_NOT_ARMED },
        { "uncalibrated", r2_holo_Reason_0_1_UNCALIBRATED }, { "unknown axis", r2_holo_Reason_0_1_UNKNOWN_AXIS },
        { "no light", r2_holo_Reason_0_1_NO_LIGHT },
    };
    r2_holo_Reason_0_1 r = { .why = r2_holo_Reason_0_1_NONE, .axis = r2_holo_Reason_0_1_AXIS_NONE };
    if (why == NULL) {
        return r;
    }
    r.why = r2_holo_Reason_0_1_OTHER;
    for (size_t i = 0; i < sizeof(WHYS) / sizeof(WHYS[0]); i++) {
        if (strcmp(why, WHYS[i].why) == 0) {
            r.why = WHYS[i].code;
        }
    }
    if (axis != NULL && h != NULL && strcmp(axis, h) == 0) {
        r.axis = r2_holo_Reason_0_1_AXIS_HORIZONTAL;
    } else if (axis != NULL && v != NULL && strcmp(axis, v) == 0) {
        r.axis = r2_holo_Reason_0_1_AXIS_VERTICAL;
    }
    return r;
}

int16_t holo_axes_value(bool placed, float position)
{
    if (!placed) {
        return (int16_t)r2_holo_Axes_0_1_NONE;
    }
    const float v = roundf(position * 10000.0f);
    return (int16_t)(v < -10000.0f ? -10000 : v > 10000.0f ? 10000 : v);
}

static int held(int16_t value)
{
    return value < -10000 ? -10000 : value > 10000 ? 10000 : value;
}

int holo_setpoint_permille(int16_t value)
{
    if (value == r2_holo_Axes_0_1_NONE) {
        return -1;
    }
    /* -10000..10000 onto 0..1000, rounded half away from the centre: 20 hundredths of a percent a step */
    const int v = held(value) + 10000;
    return (v + 10) / 20;
}

int holo_setpoint_percent(int16_t value)
{
    const int v = held(value);
    return v >= 0 ? (v + 50) / 100 : -((-v + 50) / 100);
}

void holo_ring_setting(unsigned mode, const uint8_t rgb[3], bool loop, unsigned period_ms, unsigned level,
                       int brightness, r2_holo_state_RingSetting_0_1 *out)
{
    memset(out, 0, sizeof(*out));
    out->mode = (uint8_t)mode;
    out->colour = (r2_holo_Colour_0_1){ rgb[0], rgb[1], rgb[2] };
    out->brightness = (uint8_t)(((unsigned)(brightness < 0 ? 0 : brightness) * level + 50) / 100);
    out->period_ms = (uint16_t)period_ms;
    out->loop = loop;
}

bool holo_rate_due(holo_rate_t *r, int64_t now_us, bool hurry, int64_t soonest_us, int64_t latest_us)
{
    const int64_t since = now_us - r->last_us;
    if (r->sent && since < latest_us && !(hurry && since >= soonest_us)) {
        return false;
    }
    r->last_us = now_us;
    r->sent = true;
    return true;
}

int64_t holo_rate_next(const holo_rate_t *r, bool hurry, int64_t soonest_us, int64_t latest_us)
{
    return r->sent ? r->last_us + (hurry ? soonest_us : latest_us) : 0;
}

/* ------------------------------------------------------------------ Ring */

void holo_ring_change(const r2_holo_Ring_Request_0_1 *q, holo_ring_change_t *out)
{
    memset(out, 0, sizeof(*out));
    if (q->mode.count > 0) {
        out->has_mode = true;
        out->mode = q->mode.elements[0];
    }
    if (q->colour.count > 0) {
        out->has_colour = true;
        out->rgb[0] = q->colour.elements[0].r;
        out->rgb[1] = q->colour.elements[0].g;
        out->rgb[2] = q->colour.elements[0].b;
    }
    if (q->loop.count > 0) {
        out->has_loop = true;
        out->loop = (q->loop.bitpacked[0] & 1) != 0;     /* bool[<=1]: Nunavut packs it, bit 0 the first */
    }
    if (q->brightness != 0) {
        out->has_brightness = true;
        out->brightness = q->brightness;
    }
    if (q->period_ms != 0) {
        out->has_period = true;
        out->period_ms = q->period_ms;
    }
}

/* ------------------------------------------------------------------ Scene and Screen */

bool holo_c_string(const uint8_t *bytes, size_t n, char *out, size_t len)
{
    if (n >= len || memchr(bytes, '\0', n) != NULL) {
        return false;
    }
    memcpy(out, bytes, n);
    out[n] = '\0';
    return true;
}

bool holo_scene_request(const r2_holo_Scene_Request_0_1 *q, holo_scene_request_t *out, char *why, size_t why_len)
{
    memset(out, 0, sizeof(*out));
    if (q->action == r2_holo_Scene_Request_0_1_END) {
        out->end = true;
        return true;
    }
    if (q->action != r2_holo_Scene_Request_0_1_APPLY) {
        snprintf(why, why_len, "`action` is 0 (apply) or 1 (end)");
        return false;
    }
    out->slot = q->slot;
    if (q->slot != 0) {
        return true;
    }
    if (q->name.count == 0) {
        snprintf(why, why_len, "give `slot` (1..255), or `name` with slot 0");
        return false;
    }
    if (!holo_c_string(q->name.elements, q->name.count, out->name, sizeof(out->name))) {
        snprintf(why, why_len, "give `name`: 1-%u letters, digits, spaces, _ . and -",     /* the routes' words */
                 (unsigned)r2_holo_Scene_Request_0_1_name_ARRAY_CAPACITY_);
        return false;
    }
    return true;
}

bool holo_screen_request(const r2_holo_Screen_Request_0_1 *q, holo_screen_request_t *out, char *why, size_t why_len)
{
    memset(out, 0, sizeof(*out));
    out->backlight = -1;
    if (q->show > r2_holo_Screen_Request_0_1_OFF) {
        snprintf(why, why_len, "`show` is 0 (keep), 1 (a file), 2 (a colour), 3 (the crosshair) or 4 (nothing)");
        return false;
    }
    out->show = q->show;
    if (q->backlight.count > 0) {
        if (q->backlight.elements[0] > 100) {
            snprintf(why, why_len, "`backlight` is 0..100");
            return false;
        }
        out->backlight = q->backlight.elements[0];
    }
    if (q->show == r2_holo_Screen_Request_0_1_FILE) {
        if (!holo_c_string(q->path.path.elements, q->path.path.count, out->path, sizeof(out->path))) {
            snprintf(why, why_len, "control characters and \\ are not allowed in a path");    /* fs_path()'s words */
            return false;
        }
        out->loop = q->loop;
        out->loops = q->loops;
        out->whole_frame = q->whole_frame;
    } else if (q->show == r2_holo_Screen_Request_0_1_COLOUR) {
        out->rgb[0] = q->colour.r;
        out->rgb[1] = q->colour.g;
        out->rgb[2] = q->colour.b;
    }
    return true;
}

/* ------------------------------------------------------------------ Event */

/* A number held to 0..max, rounded */
static uint32_t held_to(double v, uint32_t max)
{
    return !(v > 0) ? 0 : v >= max ? max : (uint32_t)(v + 0.5);
}

static void bytes_of(const char *text, uint8_t *elements, size_t *count, size_t capacity)
{
    size_t n = text != NULL ? strlen(text) : 0;
    n = n > capacity ? capacity : n;
    if (n > 0) {
        memcpy(elements, text, n);
    }
    *count = n;
}

#define BYTES_OF(text, field) bytes_of((text), (field).elements, &(field).count, sizeof((field).elements))

bool holo_event(const happening_t *h, uint16_t seq, r2_holo_Event_0_1 *out)
{
    memset(out, 0, sizeof(*out));
    out->seq = seq;
    r2_holo_events_Happening_0_1 *w = &out->what;
    switch (h->kind) {
    case HAPPENING_CLIP_STARTED:
        r2_holo_events_Happening_0_1_select_clip_started_(w);
        w->clip_started.plays = (uint16_t)held_to(h->plays, UINT16_MAX);
        w->clip_started.frames = (uint16_t)held_to(h->frames, UINT16_MAX);
        w->clip_started.fps = (float)(h->fps > 0 ? h->fps : 0);
        BYTES_OF(h->path, w->clip_started.path.path);
        break;
    case HAPPENING_CLIP_ENDED:
        r2_holo_events_Happening_0_1_select_clip_ended_(w);
        w->clip_ended.finished = h->finished;
        BYTES_OF(h->path, w->clip_ended.path.path);
        break;
    case HAPPENING_SCENE_STARTED:
        r2_holo_events_Happening_0_1_select_scene_started_(w);
        w->scene_started.then = (uint8_t)h->then;
        w->scene_started.until_clip_ends = h->until_clip_ends;
        w->scene_started.duration_ms = held_to(h->duration_s * 1000.0, UINT32_MAX);
        w->scene_started.slot = (uint8_t)held_to(h->slot, UINT8_MAX);
        BYTES_OF(h->name, w->scene_started.name);
        break;
    case HAPPENING_SCENE_ENDED:
        r2_holo_events_Happening_0_1_select_scene_ended_(w);
        w->scene_ended.then = (uint8_t)h->then;
        w->scene_ended.by = (uint8_t)h->by;
        w->scene_ended.slot = (uint8_t)held_to(h->slot, UINT8_MAX);
        BYTES_OF(h->name, w->scene_ended.name);
        break;
    case HAPPENING_TOUCH:
        r2_holo_events_Happening_0_1_select_screen_touch_(w);
        w->screen_touch.action = h->up ? r2_holo_events_Touch_0_1_UP : r2_holo_events_Touch_0_1_DOWN;
        w->screen_touch.x = (uint8_t)held_to(h->x, UINT8_MAX);
        w->screen_touch.y = (uint8_t)held_to(h->y, UINT8_MAX);
        break;
    default:
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ the screen and the scene in Status */

void holo_screen_part(const holo_screen_in_t *in, r2_holo_state_Screen_0_1 *out)
{
    memset(out, 0, sizeof(*out));
    out->showing = (uint8_t)(in->showing < 0 || in->showing > (int)r2_holo_state_Screen_0_1_CLIP ? 0 : in->showing);
    out->backlight = (uint8_t)held_to(in->backlight, 100);
    if (out->showing == r2_holo_state_Screen_0_1_COLOUR) {
        out->colour = (r2_holo_Colour_0_1){ in->rgb[0], in->rgb[1], in->rgb[2] };
    }
    if (out->showing == r2_holo_state_Screen_0_1_CLIP) {
        out->frames = (uint16_t)held_to(in->frames, UINT16_MAX);
        out->shown = in->shown;
        out->passes = (uint16_t)held_to(in->loops, UINT16_MAX);
        out->plays = (uint16_t)held_to(in->plays, UINT16_MAX);
        out->fps = (float)(in->fps > 0 ? in->fps : 0);
    }
    if (out->showing == r2_holo_state_Screen_0_1_IMAGE || out->showing == r2_holo_state_Screen_0_1_CLIP) {
        BYTES_OF(in->path, out->path.path);
    }
}

/* As the holo's and the ring's parts are compared (holo_cyphal.c): a copy of `a` given b's fields that don't count,
 * then the bytes, both made zeroed first */
bool holo_screen_differs(const r2_holo_state_Screen_0_1 *a, const r2_holo_state_Screen_0_1 *b)
{
    r2_holo_state_Screen_0_1 t;
    memcpy(&t, a, sizeof(t));
    t.shown = b->shown;
    t.passes = b->passes;
    return memcmp(&t, b, sizeof(t)) != 0;
}

void holo_scene_part(bool running, const char *name, int slot, int then, bool until_clip_ends, double remaining_s,
                     r2_holo_state_Scene_0_1 *out)
{
    memset(out, 0, sizeof(*out));
    if (!running) {
        return;
    }
    out->running = true;
    out->then = (uint8_t)(then < 0 || then > (int)r2_holo_state_Scene_0_1_OFF ? 0 : then);
    out->until_clip_ends = until_clip_ends;
    /* Only -1 is "no duration": any other negative is a deadline just passed, none left */
    out->remaining_ms = remaining_s <= -1.0 ? r2_holo_state_Scene_0_1_NEVER
                        : remaining_s < 0 ? 0 : held_to(remaining_s * 1000.0, r2_holo_state_Scene_0_1_NEVER - 1);
    out->slot = (uint8_t)held_to(slot, UINT8_MAX);
    BYTES_OF(name, out->name);
}

bool holo_scene_differs(const r2_holo_state_Scene_0_1 *a, const r2_holo_state_Scene_0_1 *b)
{
    r2_holo_state_Scene_0_1 t;
    memcpy(&t, a, sizeof(t));
    if ((a->remaining_ms == r2_holo_state_Scene_0_1_NEVER) == (b->remaining_ms == r2_holo_state_Scene_0_1_NEVER)) {
        t.remaining_ms = b->remaining_ms;
    }
    return memcmp(&t, b, sizeof(t)) != 0;
}

/* ------------------------------------------------------------------ the lists */

bool holo_file_path(const uavcan_file_Path_2_0 *p, char *out, size_t len)
{
    size_t n = p->path.count;
    while (n > 1 && p->path.elements[n - 1] == '/') {
        n--;            /* a directory's "/" at the end; "/" alone is the root */
    }
    if (n == 0) {
        if (len < 2) {
            return false;
        }
        strcpy(out, "/");
        return true;
    }
    return holo_c_string(p->path.elements, n, out, len);
}

uint16_t holo_file_error(int err)
{
    switch (err) {
    case 0:       return uavcan_file_Error_1_0_OK;
    case ENOENT:
    case ENOTDIR: return uavcan_file_Error_1_0_NOT_FOUND;   /* a file as a directory in the path: never found */
    case EINVAL:  return uavcan_file_Error_1_0_INVALID_VALUE;
    case EACCES:
    case EPERM:   return uavcan_file_Error_1_0_ACCESS_DENIED;
    case EISDIR:  return uavcan_file_Error_1_0_IS_DIRECTORY;
    default:      return uavcan_file_Error_1_0_IO_ERROR;
    }
}

bool holo_file_entry(const char *name, bool dir, uavcan_file_Path_2_0 *out)
{
    const size_t n = strlen(name);
    out->path.count = 0;
    if (n + (dir ? 1 : 0) > sizeof(out->path.elements)) {
        return false;
    }
    memcpy(out->path.elements, name, n);
    if (dir) {
        out->path.elements[n] = '/';
    }
    out->path.count = n + (dir ? 1 : 0);
    return true;
}

void holo_media_info(const holo_media_in_t *in, r2_holo_MediaInfo_Response_0_1 *out)
{
    /* media_kind_t and media_format_t, each one less than MediaInfo's number for it (0 is NONE there) */
    out->kind = in->kind >= 0 && in->kind <= 2 ? (uint8_t)(in->kind + 1) : r2_holo_MediaInfo_Response_0_1_NONE;
    out->format = in->kind >= 0 && in->format >= 0 && in->format <= 3 ? (uint8_t)(in->format + 1)
                                                                      : r2_holo_MediaInfo_Response_0_1_FORMAT_NONE;
    if (out->kind == r2_holo_MediaInfo_Response_0_1_NONE) {
        out->width = out->height = out->frames = 0;
        out->fps = 0;
        out->duration_ms = 0;
        return;
    }
    out->width = (uint16_t)held_to(in->width, UINT16_MAX);
    out->height = (uint16_t)held_to(in->height, UINT16_MAX);
    if (out->kind == r2_holo_MediaInfo_Response_0_1_IMAGE) {
        out->frames = 1;
        out->fps = 0;
        out->duration_ms = 0;
        return;
    }
    out->frames = (uint16_t)held_to(in->frames, UINT16_MAX);
    out->fps = in->fps > 0 && in->fps < 65504 ? (float)in->fps : 0;     /* float16's largest */
    out->duration_ms = held_to(in->duration_s * 1000.0, UINT32_MAX);
}

void holo_list_scenes(int count, const char *name, int slot, r2_holo_ListScenes_Response_0_1 *out)
{
    memset(out, 0, sizeof(*out));
    if (count < 0) {
        out->result.code = r2_holo_Error_0_1_BUSY;
        holo_error_message(&out->result, "busy: the scenes can't be read now");
        return;
    }
    out->count = (uint8_t)held_to(count, UINT8_MAX);
    out->slot = name != NULL && name[0] != '\0' ? (uint8_t)held_to(slot, UINT8_MAX) : 0;
    BYTES_OF(name, out->name);
}

/* ------------------------------------------------------------------ the holo's registers */

void holo_endpoints_of(uint16_t op_min_us, uint16_t op_max_us, bool invert, uint16_t out[2])
{
    out[0] = invert ? op_max_us : op_min_us;
    out[1] = invert ? op_min_us : op_max_us;
}

bool holo_endpoints_write(const int64_t v[2], unsigned abs_min_us, unsigned abs_max_us, holo_endpoints_t *out,
                          char *why, size_t why_len)
{
    memset(out, 0, sizeof(*out));
    if (v[0] == 0 && v[1] == 0) {
        out->clear = true;
        return true;
    }
    if (v[0] < abs_min_us || v[0] > abs_max_us || v[1] < abs_min_us || v[1] > abs_max_us || v[0] == v[1]) {
        snprintf(why, why_len, "closed and open are each %u to %u us, and not the same; [0, 0] goes back to the "
                 "default range", abs_min_us, abs_max_us);
        return false;
    }
    out->invert = v[0] > v[1];
    out->min_us = (uint16_t)(out->invert ? v[1] : v[0]);
    out->max_us = (uint16_t)(out->invert ? v[0] : v[1]);
    return true;
}

bool holo_settle_write(int64_t v, uint16_t *out, char *why, size_t why_len)
{
    if (v < 0 || v > UINT16_MAX) {
        snprintf(why, why_len, "a settle time is 0 to 65535 ms");
        return false;
    }
    *out = (uint16_t)v;
    return true;
}
