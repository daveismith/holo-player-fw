/*
 * Host-built tests for main/holo_cyphal_util.c: the holo's Cyphal requests as the engine's motion and the
 * ring's change, Scene and Screen requests read and checked (a path's length and bytes), the board's happenings
 * (components/scenes' happening_t) as r2.holo.Event with their sizes (cyphal_util.c's frame count), the screen and
 * the scene as Status's parts, the refusals as r2.holo.Error (with components/board_api/api_codes.c) and Reason,
 * the positions as Axes, and Status's and Axes' rates
 * (manual/reference/cyphal.md; the ranges are manual/reference/host-protocol.md's and the HTTP API's,
 * checked by the routes themselves). `make unit-test`.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "api_codes.h"
#include "cyphal_util.h"
#include "holo_cyphal_util.h"
#include "r2/holo/Status_0_1.h"

static r2_holo_MotionSpec_0_1 spec(unsigned kind)
{
    r2_holo_MotionSpec_0_1 m;
    memset(&m, 0, sizeof(m));
    m.kind = (uint8_t)kind;
    return m;
}

/* Every field set, to something the route would refuse if it were sent */
static r2_holo_MotionSpec_0_1 spec_all(unsigned kind)
{
    r2_holo_MotionSpec_0_1 m = spec(kind);
    m.range = 127;
    m.count = 31;
    m.x = 300;
    m.y = -300;
    m.period_ms = 100;
    m.interval_min_ms = 50;
    m.interval_max_ms = 40;
    m.time_limit_ms = 50;
    m.duration_ms.count = 1;
    m.duration_ms.elements[0] = 20000;
    return m;
}

/* How many fields the motion carries (not 0, nor -1 for the duration): the ones it uses, as the JSON request has them */
static int fields(const holo_motion_args_t *a)
{
    return (a->x != 0) + (a->y != 0) + (a->duration_ms != -1) + (a->range != 0) + (a->count != 0) + (a->period_ms != 0) +
           (a->interval_min_s != 0 || a->interval_max_s != 0) + (a->time_s != 0);
}

static void test_motion_names(void)
{
    /* state.Holo's numbers: the host link's status block's */
    CHECK(holo_motion_number("hold") == 0 && holo_motion_number("move") == 1 && holo_motion_number("twitch") == 2 &&
          holo_motion_number("wag") == 3 && holo_motion_number("nod") == 4 && holo_motion_number("scan") == 5 &&
          holo_motion_number("circle") == 6);
    CHECK(holo_motion_number("off") == 0 && holo_motion_number(NULL) == 0);
}

static void test_motion_defaults(void)
{
    holo_motion_args_t a;
    /* All zeros: every default, so nothing but the kind, and x, y (0, 0) for move and nudge, which have none */
    for (unsigned k = 0; k <= r2_holo_MotionSpec_0_1_OFF; k++) {
        const r2_holo_MotionSpec_0_1 m = spec(k);
        holo_motion_args(&m, &a);
        CHECK(a.kind == k && fields(&a) == 0 && a.duration_ms == -1);
    }
}

static void test_motion_fields(void)
{
    holo_motion_args_t a;
    /* Each kind, every field set: only those it uses go to the check, as they were (it refuses them) */
    r2_holo_MotionSpec_0_1 m = spec_all(r2_holo_MotionSpec_0_1_CENTER);
    holo_motion_args(&m, &a);
    CHECK(fields(&a) == 1 && a.duration_ms == 20000);
    m = spec_all(r2_holo_MotionSpec_0_1_MOVE);
    holo_motion_args(&m, &a);
    CHECK(fields(&a) == 3 && a.x == 300 && a.y == -300 && a.duration_ms == 20000);
    m = spec_all(r2_holo_MotionSpec_0_1_NUDGE);
    holo_motion_args(&m, &a);
    CHECK(fields(&a) == 3 && a.x == 300 && a.duration_ms == 20000);
    m = spec_all(r2_holo_MotionSpec_0_1_TWITCH);
    holo_motion_args(&m, &a);
    CHECK(fields(&a) == 3 && a.range == 127 && a.period_ms == 0 && a.count == 0);
    CHECK(a.interval_min_s == (float)0.05 && a.interval_max_s == (float)0.04 && a.time_s == (float)0.05);
    m = spec_all(r2_holo_MotionSpec_0_1_SCAN);
    holo_motion_args(&m, &a);
    CHECK(fields(&a) == 3 && a.range == 127 && a.period_ms == 100 && a.time_s == (float)0.05 && a.interval_min_s == 0);
    for (unsigned k = r2_holo_MotionSpec_0_1_WAG; k <= r2_holo_MotionSpec_0_1_CIRCLE; k++) {
        if (k == r2_holo_MotionSpec_0_1_SCAN) {
            continue;
        }
        m = spec_all(k);
        holo_motion_args(&m, &a);
        CHECK(fields(&a) == 3 && a.range == 127 && a.count == 31 && a.period_ms == 100);
    }
    m = spec_all(r2_holo_MotionSpec_0_1_STOP);
    holo_motion_args(&m, &a);
    CHECK(fields(&a) == 0 && a.kind == r2_holo_MotionSpec_0_1_STOP);
    m = spec_all(r2_holo_MotionSpec_0_1_OFF);
    holo_motion_args(&m, &a);
    CHECK(fields(&a) == 0 && a.kind == r2_holo_MotionSpec_0_1_OFF);

    /* A duration of 0 is "at once"; an empty one is the default, by the distance */
    m = spec(r2_holo_MotionSpec_0_1_MOVE);
    m.duration_ms.count = 1;
    m.duration_ms.elements[0] = 0;
    holo_motion_args(&m, &a);
    CHECK(a.duration_ms == 0);

    /* The twitch's interval: both or neither. One alone goes, as [min, 0] or [0, max], for the check to refuse */
    m = spec(r2_holo_MotionSpec_0_1_TWITCH);
    m.interval_min_ms = 500;
    holo_motion_args(&m, &a);
    CHECK(a.interval_min_s == 0.5f && a.interval_max_s == 0);
    m.interval_min_ms = 0;
    m.interval_max_ms = 600000;
    holo_motion_args(&m, &a);
    CHECK(a.interval_min_s == 0 && a.interval_max_s == 600.0f);
    /* The edges, in seconds as the JSON request's would be once read (a double to a float) */
    m.interval_min_ms = 100;
    m.time_limit_ms = 3600000;
    holo_motion_args(&m, &a);
    CHECK(a.interval_min_s == (float)0.1 && a.time_s == 3600.0f);

    /* A kind that is none: its number, nothing else */
    for (unsigned k = 10; k <= 15; k++) {
        m = spec_all(k);
        holo_motion_args(&m, &a);
        CHECK(a.kind == k && fields(&a) == 0);
    }
}

static void test_motion_spec(void)
{
    r2_holo_MotionSpec_0_1 m;
    /* The engine's last motion as asked, defaults and all */
    holo_motion_args_t a = { .kind = r2_holo_MotionSpec_0_1_STOP, .duration_ms = -1 };
    holo_motion_spec(&a, &m);
    CHECK(m.kind == r2_holo_MotionSpec_0_1_STOP && m.duration_ms.count == 0 && m.range == 0 && m.time_limit_ms == 0);
    a = (holo_motion_args_t){ .kind = r2_holo_MotionSpec_0_1_TWITCH, .range = 30, .interval_min_s = 0.5f,
                              .interval_max_s = 1.5f, .time_s = 2.5f, .duration_ms = -1 };
    holo_motion_spec(&a, &m);
    CHECK(m.kind == r2_holo_MotionSpec_0_1_TWITCH && m.range == 30 && m.interval_min_ms == 500 &&
          m.interval_max_ms == 1500 && m.time_limit_ms == 2500 && m.duration_ms.count == 0);
    a = (holo_motion_args_t){ .kind = r2_holo_MotionSpec_0_1_MOVE, .x = -50, .y = 20, .duration_ms = 0 };
    holo_motion_spec(&a, &m);
    CHECK(m.kind == r2_holo_MotionSpec_0_1_MOVE && m.x == -50 && m.y == 20 && m.duration_ms.count == 1 &&
          m.duration_ms.elements[0] == 0);
    a = (holo_motion_args_t){ .kind = r2_holo_MotionSpec_0_1_SCAN, .range = 40, .period_ms = 60000, .time_s = 3600.0f,
                              .duration_ms = -1 };
    holo_motion_spec(&a, &m);
    CHECK(m.kind == r2_holo_MotionSpec_0_1_SCAN && m.period_ms == 60000 && m.time_limit_ms == 3600000);
    /* 0.1 s as a float is 100 ms, not 99 */
    a = (holo_motion_args_t){ .kind = r2_holo_MotionSpec_0_1_TWITCH, .interval_min_s = 0.1f, .interval_max_s = 600.0f,
                              .duration_ms = -1 };
    holo_motion_spec(&a, &m);
    CHECK(m.interval_min_ms == 100 && m.interval_max_ms == 600000);
    /* What doesn't fit is held to the field's width; a kind that is none reads as a stop */
    a = (holo_motion_args_t){ .kind = 10, .range = 500, .count = 99, .x = 99999, .y = -99999, .period_ms = 99999,
                              .time_s = 99999.0f, .duration_ms = 99999 };
    holo_motion_spec(&a, &m);
    CHECK(m.kind == r2_holo_MotionSpec_0_1_STOP && m.range == 127 && m.count == 31 && m.x == 32767 && m.y == -32768 &&
          m.period_ms == 65535 && m.time_limit_ms == 4194303 && m.duration_ms.elements[0] == 65535);
    /* A request and back: the same spec, field for field, for every kind with every field it uses */
    for (unsigned k = 0; k <= r2_holo_MotionSpec_0_1_OFF; k++) {
        r2_holo_MotionSpec_0_1 q = spec(k), back;
        q.range = 40;
        q.count = 3;
        q.x = -20;
        q.y = 30;
        q.period_ms = 1500;
        q.interval_min_ms = 400;
        q.interval_max_ms = 1200;
        q.time_limit_ms = 2500;
        q.duration_ms.count = 1;
        q.duration_ms.elements[0] = 700;
        holo_motion_args(&q, &a);
        holo_motion_spec(&a, &back);
        CHECK(back.kind == k);
        CHECK(back.range == (a.range ? 40 : 0) && back.count == (a.count ? 3 : 0) && back.x == a.x && back.y == a.y);
        CHECK(back.period_ms == a.period_ms && back.interval_min_ms == (a.interval_min_s != 0 ? 400u : 0u));
        CHECK(back.time_limit_ms == (a.time_s != 0 ? 2500u : 0u));
        CHECK(back.duration_ms.count == (a.duration_ms >= 0 ? 1 : 0));
    }
}

static r2_holo_Light_Request_0_1 light(unsigned mode)
{
    r2_holo_Light_Request_0_1 q;
    memset(&q, 0, sizeof(q));
    q.mode = (uint8_t)mode;
    return q;
}

static void test_light(void)
{
    holo_light_change_t c;
    char why[96];
    /* The modes: on is solid; the others the ring's own; leia a timed flicker */
    static const struct { unsigned mode; int ring; } MODES[] = {
        { r2_holo_Light_Request_0_1_OFF, HOLO_RING_OFF }, { r2_holo_Light_Request_0_1_ON, HOLO_RING_SOLID },
        { r2_holo_Light_Request_0_1_PULSE, HOLO_RING_PULSE }, { r2_holo_Light_Request_0_1_FLICKER, HOLO_RING_FLICKER },
        { r2_holo_Light_Request_0_1_BLINK, HOLO_RING_BLINK }, { r2_holo_Light_Request_0_1_LEIA, HOLO_RING_FLICKER },
    };
    for (size_t i = 0; i < sizeof(MODES) / sizeof(MODES[0]); i++) {
        r2_holo_Light_Request_0_1 q = light(MODES[i].mode);
        CHECK(holo_light_change(&q, &c, why, sizeof(why)));
        CHECK(c.mode == MODES[i].ring && !c.has_colour && !c.has_period);
        const bool leia = MODES[i].mode == r2_holo_Light_Request_0_1_LEIA;
        CHECK(c.leia == leia && c.centre == leia);
        /* The defaults: level 100 (off has none), no time (a leia's is 30 s) */
        CHECK(c.has_level == (MODES[i].mode != r2_holo_Light_Request_0_1_OFF));
        CHECK(!c.has_level || c.level == 100);
        CHECK(c.has_time == leia && (!leia || c.time_s == 30.0));
    }
    /* Every field given */
    r2_holo_Light_Request_0_1 q = light(r2_holo_Light_Request_0_1_PULSE);
    q.period_ms = 800;
    q.time_limit_ms = 1500;
    q.level.count = 1;
    q.level.elements[0] = 0;
    q.colour.count = 1;
    q.colour.elements[0] = (r2_holo_Colour_0_1){ 1, 2, 3 };
    CHECK(holo_light_change(&q, &c, why, sizeof(why)));
    CHECK(c.mode == HOLO_RING_PULSE && c.has_period && c.period_ms == 800 && c.has_time && c.time_s == 1.5);
    CHECK(c.has_level && c.level == 0 && c.has_colour && c.rgb[0] == 1 && c.rgb[1] == 2 && c.rgb[2] == 3);
    /* Out of range goes as it is, for the change to refuse in the JSON API's words */
    q.level.elements[0] = 255;
    q.period_ms = 100;
    q.time_limit_ms = 50;
    CHECK(holo_light_change(&q, &c, why, sizeof(why)) && c.level == 255 && c.period_ms == 100 && c.time_s == 0.05);
    /* A period only for a pulse or a blink; a level not for off, nor a leia, whose is full; a colour for each */
    q.mode = r2_holo_Light_Request_0_1_FLICKER;
    CHECK(holo_light_change(&q, &c, why, sizeof(why)) && !c.has_period && c.has_level && c.level == 255);
    q.mode = r2_holo_Light_Request_0_1_BLINK;
    CHECK(holo_light_change(&q, &c, why, sizeof(why)) && c.has_period && c.period_ms == 100);
    q.mode = r2_holo_Light_Request_0_1_OFF;
    CHECK(holo_light_change(&q, &c, why, sizeof(why)) && !c.has_period && !c.has_level && c.has_colour && c.has_time);
    q.mode = r2_holo_Light_Request_0_1_LEIA;
    q.time_limit_ms = 2000;
    CHECK(holo_light_change(&q, &c, why, sizeof(why)) && c.level == 100 && !c.has_period && c.has_colour &&
          c.time_s == 2.0 && c.leia && c.centre);
    /* A mode that isn't one */
    for (unsigned mode = 6; mode < 256; mode += 50) {
        q = light(mode);
        why[0] = '\0';
        CHECK(!holo_light_change(&q, &c, why, sizeof(why)));
        CHECK(strcmp(why, "`mode` is off, on, pulse, flicker, blink or leia") == 0);
    }
}

static void test_errors(void)
{
    /* A route's reply as Error's code (api_codes.c, the host link's too): the same numbers */
    CHECK(api_code_of(200, NULL) == r2_holo_Error_0_1_OK && api_code_of(202, "x") == r2_holo_Error_0_1_OK);
    CHECK(api_code_of(400, "bad_request") == r2_holo_Error_0_1_BAD_REQUEST);
    CHECK(api_code_of(400, "bad_path") == r2_holo_Error_0_1_BAD_REQUEST);
    CHECK(api_code_of(404, "not_found") == r2_holo_Error_0_1_NOT_FOUND);
    CHECK(api_code_of(404, "unknown_scene") == r2_holo_Error_0_1_NOT_FOUND);
    CHECK(api_code_of(404, "not_running") == r2_holo_Error_0_1_NOT_FOUND);
    CHECK(api_code_of(422, "not_playable") == r2_holo_Error_0_1_NOT_PLAYABLE);
    CHECK(api_code_of(409, "not_ready") == r2_holo_Error_0_1_NOT_READY);
    CHECK(api_code_of(409, "busy") == r2_holo_Error_0_1_BUSY);
    CHECK(api_code_of(409, "full") == r2_holo_Error_0_1_FAILED && api_code_of(500, NULL) == r2_holo_Error_0_1_FAILED);

    r2_holo_Error_0_1 e;
    memset(&e, 0, sizeof(e));
    holo_error_message(&e, "holo: refused, uncalibrated (tilt)");
    CHECK(e.message.count == 34 && memcmp(e.message.elements, "holo: refused, uncalibrated (tilt)", 34) == 0);
    holo_error_message(&e, NULL);
    CHECK(e.message.count == 0);
    char text[100];
    memset(text, 'a', sizeof(text));
    text[99] = '\0';
    holo_error_message(&e, text);
    CHECK(e.message.count == 64);
    /* Cut at a character's start: a two-byte character across the 64th byte goes whole */
    memset(text, 'a', 63);
    strcpy(text + 63, "\xc3\xa9tc");     /* é at bytes 63 and 64 */
    holo_error_message(&e, text);
    CHECK(e.message.count == 63);
    strcpy(text + 62, "\xe2\x82\xac");  /* € at 62, 63, 64 */
    holo_error_message(&e, text);
    CHECK(e.message.count == 62);
    memset(text, 'a', 64);
    strcpy(text + 64, "\xc3\xa9");      /* é after the 64th: the 64 go */
    holo_error_message(&e, text);
    CHECK(e.message.count == 64);
}

static void test_reasons(void)
{
    r2_holo_Reason_0_1 r = holo_reason(NULL, "pan", "pan", "tilt");
    CHECK(r.why == r2_holo_Reason_0_1_NONE && r.axis == r2_holo_Reason_0_1_AXIS_NONE);
    r = holo_reason("uncalibrated", "tilt", "pan", "tilt");
    CHECK(r.why == r2_holo_Reason_0_1_UNCALIBRATED && r.axis == r2_holo_Reason_0_1_AXIS_VERTICAL);
    r = holo_reason("not fitted", "pan", "pan", "tilt");
    CHECK(r.why == r2_holo_Reason_0_1_NOT_FITTED && r.axis == r2_holo_Reason_0_1_AXIS_HORIZONTAL);
    r = holo_reason("not armed", NULL, "pan", "tilt");
    CHECK(r.why == r2_holo_Reason_0_1_NOT_ARMED && r.axis == r2_holo_Reason_0_1_AXIS_NONE);
    CHECK(holo_reason("unknown axis", "x", "pan", "tilt").why == r2_holo_Reason_0_1_UNKNOWN_AXIS);
    CHECK(holo_reason("unknown axis", "x", "pan", "tilt").axis == r2_holo_Reason_0_1_AXIS_NONE);
    CHECK(holo_reason("no light", NULL, "pan", "tilt").why == r2_holo_Reason_0_1_NO_LIGHT);
    CHECK(holo_reason("something new", NULL, "pan", "tilt").why == r2_holo_Reason_0_1_OTHER);
}

static void test_axes(void)
{
    CHECK(holo_axes_value(false, 0.5f) == r2_holo_Axes_0_1_NONE);
    CHECK(holo_axes_value(true, 0.0f) == 0 && holo_axes_value(true, 1.0f) == 10000 && holo_axes_value(true, -1.0f) == -10000);
    CHECK(holo_axes_value(true, 0.398f) == 3980 && holo_axes_value(true, -0.602f) == -6020);
    CHECK(holo_axes_value(true, 1.5f) == 10000 && holo_axes_value(true, -7.0f) == -10000);
    /* Setpoints: permille of the travel, 0 closed to 1000 open, by steps of 20 hundredths of a percent */
    CHECK(holo_setpoint_permille(r2_holo_Axes_0_1_NONE) == -1);
    CHECK(holo_setpoint_permille(-10000) == 0 && holo_setpoint_permille(0) == 500 && holo_setpoint_permille(10000) == 1000);
    CHECK(holo_setpoint_permille(-32767) == 0 && holo_setpoint_permille(32767) == 1000);       /* held */
    CHECK(holo_setpoint_permille(9) == 500 && holo_setpoint_permille(19) == 501 && holo_setpoint_permille(10) == 501 && holo_setpoint_permille(-10) == 500);
    CHECK(holo_setpoint_permille(-11) == 499 && holo_setpoint_permille(2500) == 625 && holo_setpoint_permille(-2500) == 375);
    /* Every step round trips: the permille's position, published, is the setpoint again */
    int bad = 0;
    for (int p = 0; p <= 1000; p++) {
        const int16_t v = holo_axes_value(true, (float)p / 500.0f - 1.0f);
        bad += holo_setpoint_permille(v) != p;
    }
    CHECK(bad == 0);
    /* And as whole percent, for a MOVE */
    CHECK(holo_setpoint_percent(0) == 0 && holo_setpoint_percent(10000) == 100 && holo_setpoint_percent(-10000) == -100);
    CHECK(holo_setpoint_percent(149) == 1 && holo_setpoint_percent(150) == 2 && holo_setpoint_percent(-150) == -2);
    CHECK(holo_setpoint_percent(-149) == -1 && holo_setpoint_percent(30000) == 100 && holo_setpoint_percent(-30000) == -100);
}

static void test_ring(void)
{
    r2_holo_state_RingSetting_0_1 r;
    const uint8_t rgb[3] = { 255, 128, 0 };
    holo_ring_setting(HOLO_RING_PULSE, rgb, false, 2000, 100, 30, &r);
    CHECK(r.mode == r2_holo_state_RingSetting_0_1_PULSE && r.colour.r == 255 && r.colour.g == 128 && r.colour.b == 0);
    CHECK(r.brightness == 30 && r.period_ms == 2000 && !r.loop);
    /* The brightness is the ring's times the level */
    holo_ring_setting(HOLO_RING_SOLID, rgb, false, 0, 50, 30, &r);
    CHECK(r.brightness == 15);
    holo_ring_setting(HOLO_RING_SOLID, rgb, false, 0, 0, 100, &r);
    CHECK(r.brightness == 0);
    holo_ring_setting(HOLO_RING_SOLID, rgb, false, 0, 33, 33, &r);
    CHECK(r.brightness == 11);      /* 10.89 */
    holo_ring_setting(HOLO_RING_WIPE, rgb, true, 0, 100, 100, &r);
    CHECK(r.mode == r2_holo_state_RingSetting_0_1_WIPE && r.loop && r.brightness == 100);
}

static void test_rates(void)
{
    holo_rate_t r = { 0 };
    const int64_t S = HOLO_STATUS_SOONEST_US, L = HOLO_STATUS_LATEST_US;
    /* The first goes at once, change or not */
    CHECK(holo_rate_due(&r, 5000000, false, S, L) && r.last_us == 5000000);
    /* No change: once a second */
    CHECK(!holo_rate_due(&r, 5000000 + 999999, false, S, L));
    CHECK(holo_rate_next(&r, false, S, L) == 6000000);
    CHECK(holo_rate_due(&r, 6000000, false, S, L));
    /* A change: at once if a quarter of a second has passed since the last, else when it has */
    CHECK(holo_rate_due(&r, 6300000, true, S, L));
    CHECK(!holo_rate_due(&r, 6400000, true, S, L));
    CHECK(holo_rate_next(&r, true, S, L) == 6550000);
    CHECK(holo_rate_due(&r, 6550000, true, S, L));
    /* So never more than four a second, however much changes */
    holo_rate_t q = { 0 };
    int sent = 0;
    for (int64_t t = 0; t < 10000000; t += 10000) {
        sent += holo_rate_due(&q, t, true, S, L);
    }
    CHECK(sent == 40);
    /* And a change waits a quarter of a second at most: any change at t, looked at again at holo_rate_next() */
    holo_rate_t w = { 0 };
    holo_rate_due(&w, 0, false, S, L);
    int late = 0;
    for (int64_t t = 1; t < 3000000; t += 7919) {
        holo_rate_t c = w;
        if (!holo_rate_due(&c, t, true, S, L)) {
            const int64_t next = holo_rate_next(&c, true, S, L);
            late += next - t > S || !holo_rate_due(&c, next, true, S, L);
        }
    }
    CHECK(late == 0);
    /* Axes: ten a second while moving, once a second at rest */
    holo_rate_t a = { 0 };
    int moving = 0, resting = 0;
    for (int64_t t = 0; t < 5000000; t += 50000) {
        moving += holo_rate_due(&a, t, true, HOLO_AXES_MOVING_US, HOLO_AXES_RESTING_US);
    }
    for (int64_t t = 5000000; t < 10000000; t += 50000) {
        resting += holo_rate_due(&a, t, false, HOLO_AXES_MOVING_US, HOLO_AXES_RESTING_US);
    }
    CHECK(moving == 50 && resting == 5);
    /* Not sent (the mailbox refused it): the next goes at once */
    a.sent = false;
    CHECK(holo_rate_due(&a, a.last_us + 1, false, HOLO_AXES_MOVING_US, HOLO_AXES_RESTING_US));
}

static void test_ring_request(void)
{
    r2_holo_Ring_Request_0_1 q;
    holo_ring_change_t c;
    /* Nothing in it: nothing to change */
    memset(&q, 0, sizeof(q));
    holo_ring_change(&q, &c);
    CHECK(!c.has_mode && !c.has_colour && !c.has_loop && !c.has_brightness && !c.has_period);
    /* Each field there only if given; 0 a mode of its own (off) */
    q.mode.count = 1;
    q.mode.elements[0] = 0;
    q.colour.count = 1;
    q.colour.elements[0] = (r2_holo_Colour_0_1){ 1, 2, 3 };
    q.loop.count = 1;
    q.loop.bitpacked[0] = 1;
    q.brightness = 40;
    q.period_ms = 900;
    holo_ring_change(&q, &c);
    CHECK(c.has_mode && c.mode == 0 && c.has_colour && c.rgb[0] == 1 && c.rgb[1] == 2 && c.rgb[2] == 3);
    CHECK(c.has_loop && c.loop && c.has_brightness && c.brightness == 40 && c.has_period && c.period_ms == 900);
    /* A loop given as false is given; out of range is passed on, for the change's check and its words */
    q.loop.bitpacked[0] = 0;
    q.mode.elements[0] = 7;
    q.brightness = 255;
    q.period_ms = 65535;
    holo_ring_change(&q, &c);
    CHECK(c.has_loop && !c.loop && c.mode == 7 && c.brightness == 255 && c.period_ms == 65535);
}

static void test_c_string(void)
{
    char out[8];
    CHECK(holo_c_string((const uint8_t *)"abc", 3, out, sizeof(out)) && strcmp(out, "abc") == 0);
    CHECK(holo_c_string((const uint8_t *)"", 0, out, sizeof(out)) && out[0] == '\0');
    CHECK(holo_c_string((const uint8_t *)"1234567", 7, out, sizeof(out)) && strcmp(out, "1234567") == 0);
    CHECK(!holo_c_string((const uint8_t *)"12345678", 8, out, sizeof(out)));      /* no room for the NUL */
    CHECK(!holo_c_string((const uint8_t *)"ab\0c", 4, out, sizeof(out)));         /* a NUL inside */
}

static void test_scene_request(void)
{
    r2_holo_Scene_Request_0_1 q;
    holo_scene_request_t r;
    char why[96];
    memset(&q, 0, sizeof(q));
    /* END ignores the rest */
    q.action = r2_holo_Scene_Request_0_1_END;
    q.slot = 9;
    CHECK(holo_scene_request(&q, &r, why, sizeof(why)) && r.end);
    /* APPLY by slot: the name ignored */
    q.action = r2_holo_Scene_Request_0_1_APPLY;
    q.name.count = 3;
    memcpy(q.name.elements, "a\0b", 3);
    CHECK(holo_scene_request(&q, &r, why, sizeof(why)) && !r.end && r.slot == 9);
    /* By name */
    q.slot = 0;
    q.name.count = 32;
    memset(q.name.elements, 'x', 32);
    CHECK(holo_scene_request(&q, &r, why, sizeof(why)) && r.slot == 0 && strlen(r.name) == 32);
    /* No slot and no name; a name with a NUL; an action that is none */
    q.name.count = 0;
    CHECK(!holo_scene_request(&q, &r, why, sizeof(why)) && strcmp(why, "give `slot` (1..255), or `name` with slot 0") == 0);
    q.name.count = 3;
    memcpy(q.name.elements, "a\0b", 3);
    CHECK(!holo_scene_request(&q, &r, why, sizeof(why)) &&
          strcmp(why, "give `name`: 1-32 letters, digits, spaces, _ . and -") == 0);
    q.action = 2;
    CHECK(!holo_scene_request(&q, &r, why, sizeof(why)) && strcmp(why, "`action` is 0 (apply) or 1 (end)") == 0);
}

static void test_screen_request(void)
{
    static r2_holo_Screen_Request_0_1 q;
    static holo_screen_request_t r;
    char why[128];
    memset(&q, 0, sizeof(q));
    /* KEEP, nothing else: the backlight as it is */
    CHECK(holo_screen_request(&q, &r, why, sizeof(why)) && r.show == r2_holo_Screen_Request_0_1_KEEP && r.backlight == -1);
    /* The backlight, 0 a level of its own; over 100 refused */
    q.backlight.count = 1;
    q.backlight.elements[0] = 0;
    CHECK(holo_screen_request(&q, &r, why, sizeof(why)) && r.backlight == 0);
    q.backlight.elements[0] = 100;
    CHECK(holo_screen_request(&q, &r, why, sizeof(why)) && r.backlight == 100);
    q.backlight.elements[0] = 101;
    CHECK(!holo_screen_request(&q, &r, why, sizeof(why)) && strcmp(why, "`backlight` is 0..100") == 0);
    q.backlight.count = 0;
    /* A show that is none */
    q.show = 5;
    CHECK(!holo_screen_request(&q, &r, why, sizeof(why)) && strncmp(why, "`show` is 0 (keep)", 18) == 0);
    /* COLOUR: the colour; FILE fields ignored */
    q.show = r2_holo_Screen_Request_0_1_COLOUR;
    q.colour = (r2_holo_Colour_0_1){ 9, 8, 7 };
    q.loop = true;
    q.loops = 5000;
    CHECK(holo_screen_request(&q, &r, why, sizeof(why)) && r.rgb[0] == 9 && r.rgb[1] == 8 && r.rgb[2] == 7 && !r.loop &&
          r.loops == 0);
    /* FILE: loop and loops as given (the scenes component checks them, with the JSON API's words) */
    q.show = r2_holo_Screen_Request_0_1_FILE;
    memcpy(q.path.path.elements, "/clips/a.mov", 12);
    q.path.path.count = 12;
    q.whole_frame = true;
    CHECK(holo_screen_request(&q, &r, why, sizeof(why)) && strcmp(r.path, "/clips/a.mov") == 0 && r.loop &&
          r.loops == 5000 && r.whole_frame);
    /* uavcan.file.Path's whole 255 bytes reach fs_path(), which holds the board's 159 (and accepts a longer one that
     * starts with the volume's own root); an empty one too, for its words */
    memset(q.path.path.elements, 'p', 255);
    q.path.path.elements[0] = '/';
    q.path.path.count = 255;
    CHECK(holo_screen_request(&q, &r, why, sizeof(why)) && strlen(r.path) == 255);
    q.path.path.count = 0;
    CHECK(holo_screen_request(&q, &r, why, sizeof(why)) && r.path[0] == '\0');
    /* A NUL inside: refused in fs_path()'s words for a control character */
    memcpy(q.path.path.elements, "/a\0b.png", 8);
    q.path.path.count = 8;
    CHECK(!holo_screen_request(&q, &r, why, sizeof(why)) &&
          strcmp(why, "control characters and \\ are not allowed in a path") == 0);
}

/* An Event's serialized size, in bytes */
static size_t event_size(const r2_holo_Event_0_1 *e)
{
    static uint8_t buf[r2_holo_Event_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t size = sizeof(buf);
    return r2_holo_Event_0_1_serialize_(e, buf, &size) < 0 ? 0 : size;
}

static void test_events(void)
{
    static r2_holo_Event_0_1 e;
    /* clip_started: as runner.c makes it (happening.c reads it back from its JSON) */
    happening_t h = { .kind = HAPPENING_CLIP_STARTED, .path = "/clips/intro.mov", .plays = 3, .frames = 450, .fps = 29.97 };
    CHECK(holo_event(&h, 42, &e) && e.seq == 42 && r2_holo_events_Happening_0_1_is_clip_started_(&e.what));
    CHECK(e.what.clip_started.plays == 3 && e.what.clip_started.frames == 450 &&
          fabsf(e.what.clip_started.fps - 29.97f) < 0.01f && e.what.clip_started.path.path.count == 16 &&
          memcmp(e.what.clip_started.path.path.elements, "/clips/intro.mov", 16) == 0);
    /* Forever: 0 plays; frames held to 16 bits; an fps that isn't a number (NAN) as 0 */
    h = (happening_t){ .kind = HAPPENING_CLIP_STARTED, .path = "/a.gif", .plays = 0, .frames = 100000, .fps = NAN };
    CHECK(holo_event(&h, 1, &e) && e.what.clip_started.plays == 0 && e.what.clip_started.frames == 65535 &&
          e.what.clip_started.fps == 0);
    /* The type's own sizes: a 20-byte path's clip_started is 5 frames, its clip_ended 4 */
    h = (happening_t){ .kind = HAPPENING_CLIP_STARTED, .path = "/clips/abcdefghi.mov", .plays = 1 };
    CHECK(holo_event(&h, 7, &e) && cyphal_frames(event_size(&e)) == 5);
    h.kind = HAPPENING_CLIP_ENDED;
    CHECK(holo_event(&h, 7, &e) && cyphal_frames(event_size(&e)) == 4);
    /* clip_ended */
    h = (happening_t){ .kind = HAPPENING_CLIP_ENDED, .path = "/b.mov", .finished = true };
    CHECK(holo_event(&h, 65535, &e) && e.seq == 65535 && r2_holo_events_Happening_0_1_is_clip_ended_(&e.what) &&
          e.what.clip_ended.finished && e.what.clip_ended.path.path.count == 6);
    /* A 159-byte path, the board's longest, whole */
    static char longest[160];
    memset(longest, 'z', 159);
    longest[0] = '/';
    h.path = longest;
    CHECK(holo_event(&h, 65535, &e) && e.what.clip_ended.path.path.count == 159 &&
          event_size(&e) == 2 + 1 + 1 + 1 + 159);   /* seq, union tag, finished, length */
    /* scene_started, with and without a slot and a duration */
    h = (happening_t){ .kind = HAPPENING_SCENE_STARTED, .name = "cantina", .slot = 7, .then = SCENE_THEN_RESTORE,
                       .until_clip_ends = true, .duration_s = 12.5 };
    CHECK(holo_event(&h, 3, &e) && r2_holo_events_Happening_0_1_is_scene_started_(&e.what));
    CHECK(e.what.scene_started.then == r2_holo_events_SceneStarted_0_1_RESTORE && e.what.scene_started.until_clip_ends &&
          e.what.scene_started.duration_ms == 12500 && e.what.scene_started.slot == 7 &&
          e.what.scene_started.name.count == 7 && memcmp(e.what.scene_started.name.elements, "cantina", 7) == 0);
    h = (happening_t){ .kind = HAPPENING_SCENE_STARTED, .name = "now", .then = SCENE_THEN_STAY };
    CHECK(holo_event(&h, 4, &e) && e.what.scene_started.then == r2_holo_events_SceneStarted_0_1_STAY &&
          e.what.scene_started.slot == 0 && e.what.scene_started.duration_ms == 0 && !e.what.scene_started.until_clip_ends);
    h.then = SCENE_THEN_OFF;
    h.duration_s = 0.1;
    CHECK(holo_event(&h, 4, &e) && e.what.scene_started.then == r2_holo_events_SceneStarted_0_1_OFF &&
          e.what.scene_started.duration_ms == 100);
    h.duration_s = 86400;
    CHECK(holo_event(&h, 4, &e) && e.what.scene_started.duration_ms == 86400000u);
    /* A 10-byte name's scene_started is 4 frames, its scene_ended 3 */
    h = (happening_t){ .kind = HAPPENING_SCENE_STARTED, .name = "abcdefghij", .then = SCENE_THEN_RESTORE };
    CHECK(holo_event(&h, 4, &e) && cyphal_frames(event_size(&e)) == 4);
    /* scene_ended: each `by`, numbered as the scenes component numbers it */
    for (unsigned i = 0; i < 4; i++) {
        h = (happening_t){ .kind = HAPPENING_SCENE_ENDED, .name = "abcdefghij", .slot = 255, .then = SCENE_THEN_OFF,
                           .by = (scene_by_t)i };
        CHECK(holo_event(&h, 5, &e) && r2_holo_events_Happening_0_1_is_scene_ended_(&e.what) && e.what.scene_ended.by == i &&
              e.what.scene_ended.then == r2_holo_events_SceneStarted_0_1_OFF && e.what.scene_ended.slot == 255);
        CHECK(cyphal_frames(event_size(&e)) == 3);
    }
    CHECK(r2_holo_events_SceneEnded_0_1_CLIP == SCENE_BY_CLIP && r2_holo_events_SceneEnded_0_1_TIME == SCENE_BY_TIME &&
          r2_holo_events_SceneEnded_0_1_END == SCENE_BY_END && r2_holo_events_SceneEnded_0_1_REPLACED == SCENE_BY_REPLACED);
    /* A name past 32 bytes is cut to 32 (never, on this board: the scenes component refuses it) */
    h.name = "0123456789012345678901234567890123456789";
    CHECK(holo_event(&h, 5, &e) && e.what.scene_ended.name.count == 32);
    /* touch: down and up, x and y; one frame */
    h = (happening_t){ .kind = HAPPENING_TOUCH, .x = 239, .y = 0 };
    CHECK(holo_event(&h, 9, &e) && r2_holo_events_Happening_0_1_is_screen_touch_(&e.what) &&
          e.what.screen_touch.action == r2_holo_events_Touch_0_1_DOWN && e.what.screen_touch.x == 239 &&
          e.what.screen_touch.y == 0 && cyphal_frames(event_size(&e)) == 1);
    h.up = true;
    h.x = 300;
    CHECK(holo_event(&h, 9, &e) && e.what.screen_touch.action == r2_holo_events_Touch_0_1_UP && e.what.screen_touch.x == 255);
    /* Not one the type has */
    h.kind = HAPPENING_NONE;
    CHECK(!holo_event(&h, 9, &e));
}

static void test_screen_part(void)
{
    static r2_holo_state_Screen_0_1 a, b;
    /* Nothing: all 0, the backlight as it is */
    holo_screen_in_t in = { .showing = r2_holo_state_Screen_0_1_NOTHING, .backlight = 80, .rgb = { 1, 2, 3 },
                            .frames = 9, .path = "/x.png" };
    holo_screen_part(&in, &a);
    CHECK(a.showing == 0 && a.backlight == 80 && a.colour.r == 0 && a.frames == 0 && a.path.path.count == 0);
    /* A colour */
    in.showing = r2_holo_state_Screen_0_1_COLOUR;
    holo_screen_part(&in, &a);
    CHECK(a.showing == 1 && a.colour.r == 1 && a.colour.g == 2 && a.colour.b == 3 && a.path.path.count == 0);
    /* An image: its path, no clip's fields */
    in = (holo_screen_in_t){ .showing = r2_holo_state_Screen_0_1_IMAGE, .backlight = 100, .frames = 5, .path = "/x.png" };
    holo_screen_part(&in, &a);
    CHECK(a.showing == 3 && a.frames == 0 && a.path.path.count == 6 && memcmp(a.path.path.elements, "/x.png", 6) == 0);
    /* A clip: its progress */
    in = (holo_screen_in_t){ .showing = r2_holo_state_Screen_0_1_CLIP, .backlight = 100, .frames = 300, .shown = 1234,
                             .loops = 4, .plays = 0, .fps = 30, .path = "/c.mov" };
    holo_screen_part(&in, &a);
    CHECK(a.showing == 4 && a.frames == 300 && a.shown == 1234 && a.passes == 4 && a.plays == 0 && a.fps == 30.0f);
    /* Its progress isn't a change; anything else is */
    in.shown = 1300;
    in.loops = 5;
    holo_screen_part(&in, &b);
    CHECK(!holo_screen_differs(&a, &b));
    in.plays = 2;
    holo_screen_part(&in, &b);
    CHECK(holo_screen_differs(&a, &b));
    in.plays = 0;
    in.path = "/d.mov";
    holo_screen_part(&in, &b);
    CHECK(holo_screen_differs(&a, &b));
    in.path = "/c.mov";
    in.backlight = 50;
    holo_screen_part(&in, &b);
    CHECK(holo_screen_differs(&a, &b));
    /* Held: a backlight past 100, more than 65535 frames or plays, a showing that is none */
    in = (holo_screen_in_t){ .showing = 9, .backlight = 120 };
    holo_screen_part(&in, &a);
    CHECK(a.showing == 0 && a.backlight == 100);
    in = (holo_screen_in_t){ .showing = r2_holo_state_Screen_0_1_CLIP, .frames = 70000, .plays = 70000 };
    holo_screen_part(&in, &a);
    CHECK(a.frames == 65535 && a.plays == 65535);
    /* The largest Status: 375 bytes (54 frames), with a 255-byte path and a 32-byte name */
    static r2_holo_Status_0_1 st;
    memset(&st, 0, sizeof(st));
    static char path[256];
    memset(path, 'p', 255);
    in = (holo_screen_in_t){ .showing = r2_holo_state_Screen_0_1_CLIP, .path = path };
    holo_screen_part(&in, &st.screen);
    holo_scene_part(true, "0123456789012345678901234567890123", 1, 1, false, 3, &st.scene);
    st.holo.last.duration_ms.count = 1;     /* the last motion asked for, with its duration */
    static uint8_t buf[r2_holo_Status_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t size = sizeof(buf);
    CHECK(r2_holo_Status_0_1_serialize_(&st, buf, &size) >= 0 && size == 375 && cyphal_frames(size) == 54);
    /* Nothing showing, no scene: 86 bytes, 13 frames; a 20-byte path and a 10-byte name, 116 and 17 */
    memset(&st, 0, sizeof(st));
    size = sizeof(buf);
    CHECK(r2_holo_Status_0_1_serialize_(&st, buf, &size) >= 0 && size == 86 && cyphal_frames(size) == 13);
    in = (holo_screen_in_t){ .showing = r2_holo_state_Screen_0_1_IMAGE, .path = "/clips/abcdefghi.mov" };
    holo_screen_part(&in, &st.screen);
    holo_scene_part(true, "abcdefghij", 1, 1, false, 3, &st.scene);
    size = sizeof(buf);
    CHECK(r2_holo_Status_0_1_serialize_(&st, buf, &size) >= 0 && size == 116 && cyphal_frames(size) == 17);
}

static void test_scene_part(void)
{
    r2_holo_state_Scene_0_1 a, b;
    /* None running: all 0 */
    holo_scene_part(false, "x", 5, 1, true, 10, &a);
    CHECK(!a.running && a.then == 0 && a.slot == 0 && a.name.count == 0 && a.remaining_ms == 0 && !a.until_clip_ends);
    /* Running: its fields; NEVER without a duration */
    holo_scene_part(true, "cantina", 7, 2, true, -1, &a);
    CHECK(a.running && a.then == r2_holo_state_Scene_0_1_OFF && a.slot == 7 && a.until_clip_ends &&
          a.remaining_ms == r2_holo_state_Scene_0_1_NEVER && a.name.count == 7);
    /* A deadline just passed, before the runner has ended the scene: none left, not NEVER, and no change */
    holo_scene_part(true, "cantina", 7, 2, false, 0.5, &b);
    holo_scene_part(true, "cantina", 7, 2, false, -0.01, &a);
    CHECK(a.remaining_ms == 0 && !holo_scene_differs(&a, &b));
    holo_scene_part(true, "cantina", 7, 2, false, -0.6, &a);
    CHECK(a.remaining_ms == 0);
    holo_scene_part(true, "cantina", 0, 1, false, 12.3456, &a);
    CHECK(a.then == r2_holo_state_Scene_0_1_RESTORE && a.slot == 0 && a.remaining_ms == 12346);
    /* The time left isn't a change; the rest are */
    holo_scene_part(true, "cantina", 0, 1, false, 2.0, &b);
    CHECK(!holo_scene_differs(&a, &b));
    holo_scene_part(true, "cantina", 0, 1, false, -1, &b);
    CHECK(holo_scene_differs(&a, &b));
    holo_scene_part(true, "cantinb", 0, 1, false, 2.0, &b);
    CHECK(holo_scene_differs(&a, &b));
    holo_scene_part(false, NULL, 0, 0, false, -1, &b);
    CHECK(holo_scene_differs(&a, &b));
}

int main(void)
{
    test_motion_names();
    test_motion_defaults();
    test_motion_fields();
    test_motion_spec();
    test_light();
    test_errors();
    test_reasons();
    test_axes();
    test_ring();
    test_rates();
    test_ring_request();
    test_c_string();
    test_scene_request();
    test_screen_request();
    test_events();
    test_screen_part();
    test_scene_part();
    printf("test_holo_cyphal_util: %d checks, %d failed\n", s_checks, s_failed);
    return s_failed != 0;
}
