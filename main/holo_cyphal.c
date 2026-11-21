/*
 * The holo on Cyphal: the node's handlers for the holo's ports (cyphal_handler_t), and its poll on the
 * node's worker (manual/reference/cyphal.md). Everything here but the setpoint's mailbox runs on the
 * worker, which may wait; nothing here touches libcanard.
 *
 * Motion goes through the code POST /api/v1/holo/motion runs once it has read its JSON, as the host link's
 * requests do: scene_holo_change(), which checks, refuses and starts a motion exactly as for HTTP. The request
 * becomes the engine's motion (holo_motion_args(), its kind mapped by SPEC_OF_KIND[]), and the refusal its
 * Error, in the same words. `off`, the release, also turns the ring off, since the ring is the holo's light
 * here, whatever the engine said. Light goes through the code PATCH /api/v1/leds runs (scene_leds_change()), which takes a leia's mark
 * besides; a leia's centre is the same motion code's `center`.
 *
 * A setpoint (r2.holo.Axes on the subscribed port) is copied into a mailbox on the node's task, the newest
 * replacing one not yet carried out, and carried out here: a direct move, as the dome panel makes for a ROS
 * joint command. The running motion, if any, is stopped through the engine (holo_motion() STOP), and each
 * axis given goes straight to its place through the servo layer (servo_set_percentage(), the call the
 * engine's own moves end in), with no easing. The engine knows where the axes are by the pulses last sent
 * once it has placed them since the board started; until then the first setpoint goes through the engine as
 * a MOVE with no duration, which places both (an axis left NONE at the centre, where the engine takes an
 * unplaced axis to be), in whole percent, and then again, exactly, once the engine has carried that out
 * (s_again). While the holo can't move, a setpoint moves nothing.
 *
 * Status and Axes are built here on the poll from the state the HTTP API's GET /holo and GET /leds report
 * (holo_status(), leds_state()), and published through the node's mailboxes at their rates (holo_rate_due()).
 * Neither is looked at while none can go: inside the 250 ms after a Status, or the 100 ms after an Axes, the
 * poll sleeps the rest. Then the holo, which signals nothing, is looked at every POLL_MS; the ring is read
 * again only when its change event (the kit's events, "leds") says it changed, and for the time left in a
 * Status that goes. A Status counts as changed when a part differs, its times aside, from the one sent. A
 * message whose port is off this run (its register 65535) isn't built at all until the node next goes online. The
 * heartbeat's health follows the holo: advisory while it can't move.
 */
#include "holo_cyphal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "board_api.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "events.h"
#include "freertos/FreeRTOS.h"
#include "holo.h"
#include "holo_cyphal_util.h"
#include "leds.h"
#include "r2/holo/Axes_0_1.h"
#include "r2/holo/Light_0_1.h"
#include "r2/holo/Motion_0_1.h"
#include "r2/holo/Status_0_1.h"
#include "r2/holo/ports.h"
#include "scenes.h"
#include "servo.h"

static const char *TAG = "holo_cyphal";

#define POLL_MS         50      /* the poll's longest sleep while a Status may go: the holo's change is seen this soon */
#define OFFLINE_POLL_MS 1000    /* and while not: the node's going online wakes it anyway */
#define NEVER           r2_holo_state_Holo_0_1_NEVER

_Static_assert((int)HOLO_RING_PULSE == (int)LEDS_PULSE && (int)HOLO_RING_BLINK == (int)LEDS_BLINK &&
               (int)HOLO_RING_FLICKER == (int)LEDS_FLICKER && (int)HOLO_RING_SOLID == (int)LEDS_SOLID &&
               (int)HOLO_RING_OFF == (int)LEDS_OFF, "the ring's numbers are the leds component's");
_Static_assert(LEDS_MODES - 1 == r2_holo_state_RingSetting_0_1_BLINK, "state.RingSetting numbers the ring's modes");
_Static_assert(sizeof(r2_holo_Motion_Request_0_1) <= CYPHAL_APP_REQUEST_MAX &&
               sizeof(r2_holo_Light_Request_0_1) <= CYPHAL_APP_REQUEST_MAX, "a request fits a job slot");
_Static_assert(r2_holo_Status_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_ <= CYPHAL_PUBLISH_MAX, "a Status fits a mailbox");
_Static_assert(API_CODE_OK == r2_holo_Error_0_1_OK && API_CODE_BAD_REQUEST == r2_holo_Error_0_1_BAD_REQUEST &&
               API_CODE_NOT_FOUND == r2_holo_Error_0_1_NOT_FOUND && API_CODE_NOT_PLAYABLE == r2_holo_Error_0_1_NOT_PLAYABLE &&
               API_CODE_NOT_READY == r2_holo_Error_0_1_NOT_READY && API_CODE_BUSY == r2_holo_Error_0_1_BUSY &&
               API_CODE_FAILED == r2_holo_Error_0_1_FAILED, "Error's codes are the routes' (api_codes.h)");

/* MotionSpec's kind for each of the engine's: the two number them differently (the engine has SCAN at 4, MotionSpec
 * and the host link's HOLO frame at 6). One table, looked up both ways */
static const uint8_t SPEC_OF_KIND[] = {
    [HOLO_CENTER] = r2_holo_MotionSpec_0_1_CENTER, [HOLO_MOVE] = r2_holo_MotionSpec_0_1_MOVE,
    [HOLO_NUDGE] = r2_holo_MotionSpec_0_1_NUDGE, [HOLO_TWITCH] = r2_holo_MotionSpec_0_1_TWITCH,
    [HOLO_SCAN] = r2_holo_MotionSpec_0_1_SCAN, [HOLO_WAG] = r2_holo_MotionSpec_0_1_WAG,
    [HOLO_NOD] = r2_holo_MotionSpec_0_1_NOD, [HOLO_CIRCLE] = r2_holo_MotionSpec_0_1_CIRCLE,
    [HOLO_STOP] = r2_holo_MotionSpec_0_1_STOP, [HOLO_OFF] = r2_holo_MotionSpec_0_1_OFF,
};
_Static_assert(sizeof(SPEC_OF_KIND) == HOLO_OFF + 1 && HOLO_OFF == r2_holo_MotionSpec_0_1_OFF,
               "each engine kind has a MotionSpec kind, and each MotionSpec kind an engine kind");

/* The setpoint's mailbox: the newest, from the node's task */
static portMUX_TYPE s_sp_mux = portMUX_INITIALIZER_UNLOCKED;
static r2_holo_Axes_0_1 s_sp;
static bool s_sp_new;

/* The first setpoint since the board started went through the engine as a MOVE in whole percent, which places the
 * axes: the same setpoint, to go again, exactly, once the engine has carried the MOVE out (on the worker's polls,
 * AGAIN_MS apart, AGAIN_POLLS at most). Dropped when a newer setpoint or a motion comes first, or anything but
 * that MOVE has been asked of the engine since */
#define AGAIN_MS    20          /* the engine's frame: it carries out a MOVE with no duration within one */
#define AGAIN_POLLS 10
static struct {
    r2_holo_Axes_0_1 setpoint;
    holo_motion_t move;         /* the MOVE that placed the axes */
    uint8_t polls;              /* polls left to wait for it; 0: nothing to go again */
} s_again;

/* The poll's, on the worker */
typedef struct {
    r2_holo_Status_0_1 status;  /* the last Status sent, each part brought up to date in place as it is looked at */
    uint8_t buf[r2_holo_Status_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_];
    holo_rate_t status_rate, axes_rate;
    r2_holo_Axes_0_1 axes;      /* the last Axes sent */
    bool online;                /* as the last poll saw the node */
    bool status_off, axes_off;  /* the port is off this run (cyphal_publish() said so): not built until the next */
} poll_t;
static poll_t *s_poll;          /* PSRAM where there is some, at the first poll */

/* The ring's change event: a listener of the kit's events (one of EVENTS_MAX_SINKS) that marks the ring changed and
 * wakes the poll. Without one (none free), the ring is read every time the holo is */
static int s_ring_sink = -1;
static bool s_ring_sink_tried;
static uint32_t s_ring_changed = 1;     /* __atomic: the ring's state is to be read again */

/* ------------------------------------------------------------------ responses */

/* An Error: the code, the reason, and the message (empty for none) */
static r2_holo_Error_0_1 error_of(uint8_t code, r2_holo_Reason_0_1 reason, const char *message)
{
    r2_holo_Error_0_1 e = { .code = code, .reason = reason };
    holo_error_message(&e, message);
    return e;
}

static const r2_holo_Reason_0_1 NO_REASON = { r2_holo_Reason_0_1_NONE, r2_holo_Reason_0_1_AXIS_NONE };

/* Why the holo can't move, by the engine, now: the reason a refusal for not ready carries */
static r2_holo_Reason_0_1 holo_why(void)
{
    holo_status_t st;
    holo_desc_t d = { 0 };
    if (holo_status(0, &st) != ESP_OK) {
        return (r2_holo_Reason_0_1){ r2_holo_Reason_0_1_NOT_FITTED, r2_holo_Reason_0_1_AXIS_NONE };
    }
    holo_describe(0, &d);
    const r2_holo_Reason_0_1 r = holo_reason(st.why, st.why_axis, d.h, d.v);
    return r.why == r2_holo_Reason_0_1_NONE ? (r2_holo_Reason_0_1){ r2_holo_Reason_0_1_OTHER, 0 } : r;
}

static size_t motion_response(const r2_holo_Error_0_1 *e, uint8_t *out, size_t room)
{
    const r2_holo_Motion_Response_0_1 r = { .result = *e };
    return r2_holo_Motion_Response_0_1_serialize_(&r, out, &room) < 0 ? 0 : room;
}

static size_t light_response(const r2_holo_Error_0_1 *e, bool centred, uint8_t *out, size_t room)
{
    const r2_holo_Light_Response_0_1 r = { .result = *e, .centred = centred };
    return r2_holo_Light_Response_0_1_serialize_(&r, out, &room) < 0 ? 0 : room;
}

/* The words a busy answer carries (the node's job slots all held) */
#define BUSY_WORDS "busy: the board is still working through earlier requests"

/* ------------------------------------------------------------------ Motion */

static bool motion_decode(const uint8_t *payload, size_t size, void *in)
{
    return r2_holo_Motion_Request_0_1_deserialize_(in, payload, &size) >= 0;
}

/* The engine's kind for MotionSpec's, or one past HOLO_OFF, which scene_holo_change() refuses, for none */
static holo_motion_kind_t kind_of_spec(unsigned spec)
{
    for (size_t k = 0; k < sizeof(SPEC_OF_KIND); k++) {
        if (SPEC_OF_KIND[k] == spec) {
            return (holo_motion_kind_t)k;
        }
    }
    return (holo_motion_kind_t)(HOLO_OFF + 1);
}

/* A motion, as POST /api/v1/holo/motion's code checks and starts it: Error's code, and the words in `why` */
static uint8_t holo_change(const holo_motion_t *m, char *why, size_t len)
{
    why[0] = '\0';
    const scene_err_t err = scene_holo_change(m, false, why, len);
    if (err == SCENE_OK) {
        why[0] = '\0';
    }
    return (uint8_t)api_scene_code(err);
}

static size_t motion_work(const void *in, uint8_t from, uint8_t *out, size_t room)
{
    const r2_holo_Motion_Request_0_1 *q = in;
    holo_motion_args_t a;
    holo_motion_args(&q->motion, &a);
    const holo_motion_t m = {
        .kind = kind_of_spec(a.kind), .x = a.x, .y = a.y, .duration_ms = a.duration_ms, .range = a.range,
        .count = a.count, .period_ms = a.period_ms, .interval_min_s = a.interval_min_s,
        .interval_max_s = a.interval_max_s, .time_s = a.time_s,
    };
    s_again.polls = 0;          /* a motion asked for after a setpoint replaces it: not to be applied again */
    char message[128];
    const uint8_t code = holo_change(&m, message, sizeof(message));
    if (code != r2_holo_Error_0_1_OK) {
        ESP_LOGI(TAG, "node %u's motion refused: %s", from, message);
    }
    if (q->motion.kind == r2_holo_MotionSpec_0_1_OFF) {
        /* The release turns the holo's light off too, which on this board is the ring (the engine's own
         * light, which the motion turns off, is none here); whatever the motion half said, as a leia's light
         * half runs whatever its centre says, so that `off` always turns the light off. A refusal of the
         * motion half (no holo engine) is still the answer */
        const scene_leds_t off = { .has_mode = true, .mode = LEDS_OFF };
        char why[64];
        scene_leds_change(&off, false, why, sizeof(why));
    }
    const r2_holo_Error_0_1 e = error_of(code, code == r2_holo_Error_0_1_NOT_READY ? holo_why() : NO_REASON, message);
    return motion_response(&e, out, room);
}

static size_t motion_busy(uint8_t *out, size_t room)
{
    const r2_holo_Error_0_1 e = error_of(r2_holo_Error_0_1_BUSY, NO_REASON, BUSY_WORDS);
    return motion_response(&e, out, room);
}

/* ------------------------------------------------------------------ Light */

static bool light_decode(const uint8_t *payload, size_t size, void *in)
{
    return r2_holo_Light_Request_0_1_deserialize_(in, payload, &size) >= 0;
}

static size_t light_work(const void *in, uint8_t from, uint8_t *out, size_t room)
{
    const r2_holo_Light_Request_0_1 *q = in;
    char why[128];
    holo_light_change_t c;
    if (!holo_light_change(q, &c, why, sizeof(why))) {
        const r2_holo_Error_0_1 e = error_of(r2_holo_Error_0_1_BAD_REQUEST, NO_REASON, why);
        return light_response(&e, false, out, room);
    }
    const scene_leds_t change = {
        .has_mode = true, .mode = c.mode,
        .has_colour = c.has_colour, .rgb = { c.rgb[0], c.rgb[1], c.rgb[2] },
        .has_period = c.has_period, .period_ms = c.period_ms,
        .has_level = c.has_level, .level = c.level,
        .has_time = c.has_time, .time_s = c.time_s,
        .leia = c.leia,
    };
    const scene_err_t err = scene_leds_change(&change, false, why, sizeof(why));
    if (err != SCENE_OK) {
        ESP_LOGI(TAG, "node %u's light refused: %s", from, why);
        const r2_holo_Error_0_1 e = error_of((uint8_t)api_scene_code(err), NO_REASON, why);
        return light_response(&e, false, out, room);
    }
    if (!c.centre) {
        const r2_holo_Error_0_1 e = error_of(r2_holo_Error_0_1_OK, NO_REASON, NULL);
        return light_response(&e, false, out, room);
    }
    /* A leia: the light half has run; the centre if the holo can, and why not if it can't */
    s_again.polls = 0;
    const holo_motion_t centre = { .kind = HOLO_CENTER, .duration_ms = -1 };
    const bool centred = holo_change(&centre, why, sizeof(why)) == r2_holo_Error_0_1_OK;
    const r2_holo_Error_0_1 e = error_of(r2_holo_Error_0_1_OK, centred ? NO_REASON : holo_why(), centred ? NULL : why);
    return light_response(&e, centred, out, room);
}

static size_t light_busy(uint8_t *out, size_t room)
{
    const r2_holo_Error_0_1 e = error_of(r2_holo_Error_0_1_BUSY, NO_REASON, BUSY_WORDS);
    return light_response(&e, false, out, room);
}

/* ------------------------------------------------------------------ the setpoint */

/* On the node's task: into the mailbox, and the worker woken */
static void setpoint_message(const uint8_t *payload, size_t size, uint8_t from)
{
    (void)from;
    r2_holo_Axes_0_1 a;
    if (r2_holo_Axes_0_1_deserialize_(&a, payload, &size) < 0) {
        return;
    }
    taskENTER_CRITICAL(&s_sp_mux);
    s_sp = a;
    s_sp_new = true;
    taskEXIT_CRITICAL(&s_sp_mux);
    cyphal_wake_worker();
}

/* A direct move: the running motion stopped, then each axis given straight to its place. `st` is the holo now,
 * NULL for an engine that didn't start */
static void carry_out(const r2_holo_Axes_0_1 *a, const holo_status_t *st)
{
    s_again.polls = 0;          /* this one replaces any still to go again */
    holo_desc_t d;
    if (st == NULL || !st->ready || !holo_describe(0, &d)) {
        return;         /* it can't move: Status says why */
    }
    char why[96];
    if (strcmp(st->motion, "hold") != 0) {
        const holo_motion_t stop = { .kind = HOLO_STOP, .duration_ms = -1 };
        holo_motion(0, &stop, why, sizeof(why));
    }
    if (!st->placed) {
        /* The first since the board started: through the engine, which then knows where the axes are, in whole
         * percent; and the same setpoint again, exactly, once it has placed them */
        const holo_motion_t m = {
            .kind = HOLO_MOVE,
            .duration_ms = 0,
            .x = a->x == r2_holo_Axes_0_1_NONE ? (int)lroundf(st->x * 100) : holo_setpoint_percent(a->x),
            .y = a->y == r2_holo_Axes_0_1_NONE ? (int)lroundf(st->y * 100) : holo_setpoint_percent(a->y),
        };
        if (holo_motion(0, &m, why, sizeof(why)) != ESP_OK) {
            ESP_LOGW(TAG, "setpoint: %s", why);
            return;
        }
        s_again.setpoint = *a;
        s_again.move = m;
        s_again.polls = AGAIN_POLLS;
        return;
    }
    const int px = holo_setpoint_permille(a->x), py = holo_setpoint_permille(a->y);
    if (px >= 0) {
        servo_set_percentage(d.h, (uint16_t)px);
    }
    if (py >= 0) {
        servo_set_percentage(d.v, (uint16_t)py);
    }
}

/* The first setpoint since the board started, again, exactly, once the engine has carried out the MOVE that placed
 * the axes (placed, holding, and that MOVE still the last thing asked of it); dropped if anything else was asked
 * since. Whether it went */
static bool go_again(const holo_status_t *st)
{
    holo_motion_t last;
    const holo_motion_t *m = &s_again.move;
    if (st == NULL || !holo_last_motion(0, &last) || last.kind != m->kind || last.x != m->x || last.y != m->y ||
        last.duration_ms != m->duration_ms) {
        s_again.polls = 0;
        return false;
    }
    if (st->placed && strcmp(st->motion, "hold") == 0) {
        const r2_holo_Axes_0_1 a = s_again.setpoint;
        carry_out(&a, st);
        return true;
    }
    if (--s_again.polls == 0) {
        ESP_LOGW(TAG, "setpoint: the engine didn't place the axes in time; left at the whole percent");
    }
    return false;
}

/* ------------------------------------------------------------------ Status and Axes */

static void axis_of(const char *ident, bool placed, r2_holo_state_Axis_0_1 *out)
{
    servo_limits_t l;
    const bool known = ident != NULL && servo_get_limits(ident, &l);
    out->fitted = ident != NULL && servo_exists(ident);
    out->calibrated = known && l.calibrated;
    out->inverted = known && l.invert;
    out->placed = out->fitted && placed;
}

/* The holo's part, from holo_status() (which GET /holo reports): st NULL for an engine that didn't start */
static void holo_part(const holo_status_t *st, r2_holo_state_Holo_0_1 *h)
{
    memset(h, 0, sizeof(*h));
    h->motion_age_ms = h->motion_remaining_ms = h->stopped_age_ms = NEVER;
    h->last.kind = r2_holo_MotionSpec_0_1_STOP;
    holo_desc_t d = { 0 };
    if (st == NULL || !holo_describe(0, &d)) {
        h->why = (r2_holo_Reason_0_1){ r2_holo_Reason_0_1_NOT_FITTED, r2_holo_Reason_0_1_AXIS_NONE };
        return;
    }
    h->ready = st->ready;
    h->why = holo_reason(st->why, st->why_axis, d.h, d.v);
    h->motion = holo_motion_number(st->motion);
    if (h->motion != r2_holo_state_Holo_0_1_HOLD) {
        h->motion_age_ms = st->motion_elapsed_ms;
        h->motion_remaining_ms = st->motion_remaining_ms;      /* UINT32_MAX, NEVER, when it has no limit */
    }
    if (st->stopped != NULL) {
        h->stopped = holo_reason(st->stopped, st->stopped_axis, d.h, d.v);
        h->stopped_motion = holo_motion_number(st->stopped_motion);
        h->stopped_age_ms = st->stopped_ms_ago;
    }
    axis_of(d.h, st->placed, &h->horizontal);
    axis_of(d.v, st->placed, &h->vertical);
    /* The last motion asked for, as asked */
    holo_motion_t m;
    if (holo_last_motion(0, &m) && (unsigned)m.kind < sizeof(SPEC_OF_KIND)) {
        const holo_motion_args_t asked = {
            .kind = SPEC_OF_KIND[m.kind], .x = m.x, .y = m.y, .duration_ms = m.duration_ms, .range = m.range,
            .count = m.count, .period_ms = m.period_ms, .interval_min_s = m.interval_min_s,
            .interval_max_s = m.interval_max_s, .time_s = m.time_s,
        };
        holo_motion_spec(&asked, &h->last);
    }
}

/* The ring's part, from leds_state() (which GET /leds reports) */
static void ring_part(r2_holo_state_Ring_0_1 *r)
{
    leds_state_t st;
    leds_state(&st);
    const leds_setting_t *n = &st.now, *a = &st.after;
    memset(r, 0, sizeof(*r));
    holo_ring_setting(n->mode, n->rgb, n->loop, n->period_ms, n->level, st.brightness, &r->now);
    holo_ring_setting(a->mode, a->rgb, a->loop, a->period_ms, a->level, st.brightness, &r->after);
    r->remaining_ms = leds_remaining_ms(&st);
    r->leia = st.leia;
}

/* The holo's part into s, and whether it changed but for its times */
static bool take_holo(r2_holo_state_Holo_0_1 *s, const holo_status_t *st)
{
    r2_holo_state_Holo_0_1 h;
    holo_part(st, &h);
    const uint32_t age = h.motion_age_ms, remaining = h.motion_remaining_ms, stopped = h.stopped_age_ms;
    h.motion_age_ms = s->motion_age_ms;
    h.motion_remaining_ms = s->motion_remaining_ms;
    h.stopped_age_ms = s->stopped_age_ms;
    const bool changed = memcmp(&h, s, sizeof(h)) != 0;     /* both zeroed first, padding and all */
    memcpy(s, &h, sizeof(h));
    s->motion_age_ms = age;
    s->motion_remaining_ms = remaining;
    s->stopped_age_ms = stopped;
    return changed;
}

/* The ring's part into r, and whether it changed but for its time */
static bool take_ring(r2_holo_state_Ring_0_1 *r)
{
    r2_holo_state_Ring_0_1 now;
    ring_part(&now);
    const uint32_t ms = now.remaining_ms;
    now.remaining_ms = r->remaining_ms;
    const bool changed = memcmp(&now, r, sizeof(now)) != 0;
    memcpy(r, &now, sizeof(now));
    r->remaining_ms = ms;
    return changed;
}

/* The ring changed, says its event: on the task that changed it, which this must not hold up */
static void ring_changed(void *ctx)
{
    (void)ctx;
    __atomic_store_n(&s_ring_changed, 1, __ATOMIC_RELEASE);
    cyphal_wake_worker();
}

/* Whether the ring is to be read again: its event came, or there is no listener to say. The listener is made the
 * first time the ring's event exists to listen to */
static bool ring_dirty(void)
{
    if (s_ring_sink < 0 && !s_ring_sink_tried) {
        const events_mask_t bit = events_bit("leds");
        if (bit != 0) {
            s_ring_sink_tried = true;
            s_ring_sink = events_listen(bit, ring_changed, NULL);
            if (s_ring_sink < 0) {
                ESP_LOGW(TAG, "no events listener free: the ring is read on every look at the holo");
            }
        }
    }
    return __atomic_exchange_n(&s_ring_changed, 0, __ATOMIC_ACQ_REL) != 0 || s_ring_sink < 0;
}

/* Status, now that one may go (250 ms after the last): the holo looked at, the ring if it changed; sent if anything
 * did, or a second after the last. The microseconds until the poll is to look again */
static int64_t publish_status(poll_t *p, const holo_status_t *st, int64_t now)
{
    r2_holo_Status_0_1 *s = &p->status;
    bool changed = take_holo(&s->holo, st);
    const bool ring = ring_dirty();
    if (ring) {
        changed |= take_ring(&s->ring);
    }
    const uint16_t seq = events_seq();
    bool touch = false;
#if CONFIG_BOARD_TOUCH_ENABLE
    touch = board_touch_running();
#endif
    changed |= seq != s->event_seq || touch != s->reporting_touch;
    s->event_seq = seq;
    s->reporting_touch = touch;
    /* The screen and the scene aren't filled in yet: they are nothing, and no scene, as zeroed */
    if (!holo_rate_due(&p->status_rate, now, changed, HOLO_STATUS_SOONEST_US, HOLO_STATUS_LATEST_US)) {
        return holo_rate_next(&p->status_rate, false, HOLO_STATUS_SOONEST_US, HOLO_STATUS_LATEST_US) - now;
    }
    if (!ring) {
        take_ring(&s->ring);        /* its time left, now */
    }
    size_t size = sizeof(p->buf);
    const esp_err_t err = r2_holo_Status_0_1_serialize_(s, p->buf, &size) < 0 ? ESP_FAIL
                          : cyphal_publish(R2_HOLO_PORT_STATUS, p->buf, size);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        p->status_off = true;               /* its port is off: nothing more until the node's next start */
        return INT64_MAX;
    }
    if (err != ESP_OK) {
        p->status_rate.sent = false;        /* not gone: again at the next poll, as if never sent */
        return POLL_MS * 1000;
    }
    return HOLO_STATUS_SOONEST_US;
}

/* Axes, now that one may go (100 ms after the last): ten a second while a motion runs or the positions change, once
 * a second at rest. The microseconds until the poll is to look again */
static int64_t publish_axes(poll_t *p, const holo_status_t *st, int64_t now)
{
    const bool placed = st != NULL && st->placed;
    const r2_holo_Axes_0_1 a = { .x = holo_axes_value(placed, st != NULL ? st->x : 0),
                                 .y = holo_axes_value(placed, st != NULL ? st->y : 0) };
    const bool moving = (st != NULL && strcmp(st->motion, "hold") != 0) || a.x != p->axes.x || a.y != p->axes.y;
    if (holo_rate_due(&p->axes_rate, now, moving, HOLO_AXES_MOVING_US, HOLO_AXES_RESTING_US)) {
        uint8_t buf[r2_holo_Axes_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_];
        size_t size = sizeof(buf);
        const esp_err_t err = r2_holo_Axes_0_1_serialize_(&a, buf, &size) < 0 ? ESP_FAIL
                              : cyphal_publish(R2_HOLO_PORT_AXES, buf, size);
        if (err == ESP_OK) {
            p->axes = a;
        } else if (err == ESP_ERR_NOT_SUPPORTED) {
            p->axes_off = true;             /* its port is off: nothing more until the node's next start */
            return INT64_MAX;
        } else {
            p->axes_rate.sent = false;
        }
        return moving ? HOLO_AXES_MOVING_US : HOLO_AXES_RESTING_US;
    }
    return holo_rate_next(&p->axes_rate, moving, HOLO_AXES_MOVING_US, HOLO_AXES_RESTING_US) - now;
}

/* ------------------------------------------------------------------ the poll */

/* Microseconds until a message with this rate may go again at the soonest: 0 when it may now */
static int64_t closed_for(const holo_rate_t *r, int64_t soonest_us, int64_t now)
{
    return r->sent && now - r->last_us < soonest_us ? r->last_us + soonest_us - now : 0;
}

uint32_t holo_cyphal_poll(bool online)
{
    if (s_poll == NULL) {
        s_poll = heap_caps_calloc_prefer(1, sizeof(poll_t), 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_8BIT);
        if (s_poll == NULL) {
            ESP_LOGE(TAG, "no memory: nothing published");
            return OFFLINE_POLL_MS;
        }
    }
    poll_t *p = s_poll;
    r2_holo_Axes_0_1 sp;
    taskENTER_CRITICAL(&s_sp_mux);
    const bool got = s_sp_new;
    sp = s_sp;
    s_sp_new = false;
    taskEXIT_CRITICAL(&s_sp_mux);
    holo_status_t st;
    bool have = holo_status(0, &st) == ESP_OK;
    cyphal_set_health(have && st.ready ? CYPHAL_HEALTH_NOMINAL : CYPHAL_HEALTH_ADVISORY);
    bool moved = got;
    if (got) {
        carry_out(&sp, have ? &st : NULL);
    } else if (s_again.polls > 0) {
        moved = go_again(have ? &st : NULL);
    }
    /* The poll's longest sleep: shorter while the first setpoint waits to go again */
    const uint32_t longest = s_again.polls > 0 ? AGAIN_MS : POLL_MS;
    if (!online) {
        p->online = false;
        return s_again.polls > 0 ? AGAIN_MS : OFFLINE_POLL_MS;
    }
    if (!p->online) {
        /* Just online: everything at once, as if never sent, and every part looked at afresh; a port that was
         * off is looked at again, since its register may have been changed for this start */
        p->online = true;
        p->status_rate.sent = p->axes_rate.sent = false;
        p->status_off = p->axes_off = false;
        p->axes = (r2_holo_Axes_0_1){ r2_holo_Axes_0_1_NONE, r2_holo_Axes_0_1_NONE };
        __atomic_store_n(&s_ring_changed, 1, __ATOMIC_RELEASE);
    }
    /* Only what may go is looked at: the rest waits out its spacing, and a port that is off isn't looked at */
    const int64_t now = esp_timer_get_time();
    const int64_t status_wait = p->status_off ? INT64_MAX : closed_for(&p->status_rate, HOLO_STATUS_SOONEST_US, now);
    const int64_t axes_wait = p->axes_off ? INT64_MAX : closed_for(&p->axes_rate, HOLO_AXES_MOVING_US, now);
    if (moved && (status_wait == 0 || axes_wait == 0) && have) {
        have = holo_status(0, &st) == ESP_OK;       /* the setpoint has just changed it */
    }
    const int64_t s_wait = status_wait > 0 ? status_wait : publish_status(p, have ? &st : NULL, now);
    const int64_t a_wait = axes_wait > 0 ? axes_wait : publish_axes(p, have ? &st : NULL, now);
    const int64_t wait = s_wait < a_wait ? s_wait : a_wait;
    const int64_t ms = wait >= (int64_t)longest * 1000 ? longest : (wait + 999) / 1000;
    return ms < 1 ? 1 : (uint32_t)ms;
}

/* ------------------------------------------------------------------ the table */

const cyphal_handler_t HOLO_CYPHAL_HANDLERS[] = {
    { .port = R2_HOLO_PORT_STATUS, .publish_max = r2_holo_Status_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_ },
    { .port = R2_HOLO_PORT_AXES, .publish_max = r2_holo_Axes_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_ },
    { .port = R2_HOLO_PORT_SETPOINT, .extent = r2_holo_Axes_0_1_EXTENT_BYTES_, .on_message = setpoint_message },
    { .port = R2_HOLO_PORT_MOTION, .extent = r2_holo_Motion_Request_0_1_EXTENT_BYTES_, .decode = motion_decode,
      .in_size = sizeof(r2_holo_Motion_Request_0_1), .work = motion_work, .busy = motion_busy },
    { .port = R2_HOLO_PORT_LIGHT, .extent = r2_holo_Light_Request_0_1_EXTENT_BYTES_, .decode = light_decode,
      .in_size = sizeof(r2_holo_Light_Request_0_1), .work = light_work, .busy = light_busy },
};
const size_t HOLO_CYPHAL_HANDLER_COUNT = sizeof(HOLO_CYPHAL_HANDLERS) / sizeof(HOLO_CYPHAL_HANDLERS[0]);
