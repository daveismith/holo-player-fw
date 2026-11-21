/*
 * The holo's Cyphal interface, its pure parts. See holo_cyphal_util.h.
 */
#include "holo_cyphal_util.h"

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
