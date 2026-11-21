/*
 * Host-built tests for main/holo_cyphal_util.c: the holo's Cyphal requests as the engine's motion and the
 * ring's change, the refusals as r2.holo.Error (with components/board_api/api_codes.c) and Reason, the positions as Axes, and Status's and Axes' rates
 * (manual/reference/cyphal.md; the ranges are manual/reference/host-protocol.md's and the HTTP API's,
 * checked by the routes themselves). `make unit-test`.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "api_codes.h"
#include "holo_cyphal_util.h"

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
    printf("test_holo_cyphal_util: %d checks, %d failed\n", s_checks, s_failed);
    return s_failed != 0;
}
