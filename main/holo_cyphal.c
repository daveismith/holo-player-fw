/*
 * The holo on Cyphal, its core: the poll on the node's worker (cyphal_app_t.poll), which carries out the setpoints
 * and builds Status and Axes (manual/reference/cyphal.md). holo_cyphal_services.c has the request handlers and the
 * table, holo_cyphal_events.c the listener and Event, holo_cyphal_internal.h what they share. Everything here but the
 * setpoint's mailbox runs on the worker, which may wait; nothing here touches libcanard.
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
 * Status and Axes are built here on the poll from the state the HTTP API's GET /holo, GET /leds, GET /screen and
 * GET /scenes (`active`) report (holo_status(), leds_state(), screen_peek_state(), scene_active()), and published
 * through the node's mailboxes at their rates (holo_rate_due()). Neither is looked at while none can go: inside the
 * 250 ms after a Status, or the 100 ms after an Axes, the poll sleeps the rest. Then the holo, which signals nothing
 * the poll listens to, is looked at every POLL_MS; the ring, the screen and the scene are read again only when their
 * change events (the kit's events: "leds", "screen", "scene") say they changed, and once more as a Status goes, for
 * the time left, a clip's progress and the scene's time left. The screen is read only when nothing holds it
 * (screen_peek_state()): while a still is drawn or a clip starts, its part stays as it was and is read again at the
 * next look, so the worker never waits for the screen. A Status counts as changed when a part differs, its times and
 * a clip's progress aside, from the one sent. A message whose port is off this run (its register 65535) isn't built
 * at all until the node next goes online. The heartbeat's health follows the holo: advisory while it can't move.
 */
#include "holo_cyphal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "holo.h"
#include "holo_cyphal_internal.h"
#include "holo_cyphal_util.h"
#include "leds.h"
#include "fs_ops.h"
#include "r2/holo/Axes_0_1.h"
#include "r2/holo/Status_0_1.h"
#include "r2/holo/ports.h"
#include "scenes.h"
#include "servo.h"
#include "video_player.h"

static const char *TAG = "holo_cyphal";

#define OFFLINE_POLL_MS 1000    /* the poll's sleep while the node isn't online: its going online wakes it anyway */
#define NEVER           r2_holo_state_Holo_0_1_NEVER

_Static_assert((int)HOLO_RING_PULSE == (int)LEDS_PULSE && (int)HOLO_RING_BLINK == (int)LEDS_BLINK &&
               (int)HOLO_RING_FLICKER == (int)LEDS_FLICKER && (int)HOLO_RING_SOLID == (int)LEDS_SOLID &&
               (int)HOLO_RING_OFF == (int)LEDS_OFF, "the ring's numbers are the leds component's");
_Static_assert(LEDS_MODES - 1 == r2_holo_state_RingSetting_0_1_BLINK, "state.RingSetting numbers the ring's modes");
_Static_assert(r2_holo_Status_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_ <= CYPHAL_PUBLISH_MAX &&
               r2_holo_Event_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_ <= CYPHAL_PUBLISH_MAX, "a message fits a mailbox");
_Static_assert((int)SCREEN_NOTHING == r2_holo_state_Screen_0_1_NOTHING && (int)SCREEN_COLOUR == r2_holo_state_Screen_0_1_COLOUR &&
               (int)SCREEN_CALIBRATION == r2_holo_state_Screen_0_1_CALIBRATION &&
               (int)SCREEN_IMAGE == r2_holo_state_Screen_0_1_IMAGE && (int)SCREEN_CLIP == r2_holo_state_Screen_0_1_CLIP,
               "state.Screen numbers what shows as the video component does");
_Static_assert((int)SCENE_THEN_RESTORE == r2_holo_state_Scene_0_1_RESTORE && (int)SCENE_THEN_OFF == r2_holo_state_Scene_0_1_OFF &&
               (int)SCENE_THEN_STAY == r2_holo_events_SceneStarted_0_1_STAY, "state.Scene numbers `then` as the scenes do");

/* MotionSpec's kind for each of the engine's (holo_cyphal_internal.h) */
const uint8_t HOLO_SPEC_OF_KIND[HOLO_OFF + 1] = {
    [HOLO_CENTER] = r2_holo_MotionSpec_0_1_CENTER, [HOLO_MOVE] = r2_holo_MotionSpec_0_1_MOVE,
    [HOLO_NUDGE] = r2_holo_MotionSpec_0_1_NUDGE, [HOLO_TWITCH] = r2_holo_MotionSpec_0_1_TWITCH,
    [HOLO_SCAN] = r2_holo_MotionSpec_0_1_SCAN, [HOLO_WAG] = r2_holo_MotionSpec_0_1_WAG,
    [HOLO_NOD] = r2_holo_MotionSpec_0_1_NOD, [HOLO_CIRCLE] = r2_holo_MotionSpec_0_1_CIRCLE,
    [HOLO_STOP] = r2_holo_MotionSpec_0_1_STOP, [HOLO_OFF] = r2_holo_MotionSpec_0_1_OFF,
};
_Static_assert(HOLO_OFF == r2_holo_MotionSpec_0_1_OFF, "each engine kind has a MotionSpec kind, and each MotionSpec kind an "
               "engine kind");

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
    screen_state_t screen;      /* screen_peek_state()'s */
    r2_holo_state_Screen_0_1 screen_now;    /* the screen's part as read, before it is compared with the one sent */
    holo_events_t events;       /* Event's */
} poll_t;
static poll_t *s_poll;          /* PSRAM where there is some, at the first poll */

/* ------------------------------------------------------------------ the setpoint */

void holo_setpoint_forget(void)
{
    s_again.polls = 0;
}

void holo_setpoint_message(const uint8_t *payload, size_t size, uint8_t from)
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
    if (holo_last_motion(0, &m) && (unsigned)m.kind < sizeof(HOLO_SPEC_OF_KIND)) {
        const holo_motion_args_t asked = {
            .kind = HOLO_SPEC_OF_KIND[m.kind], .x = m.x, .y = m.y, .duration_ms = m.duration_ms, .range = m.range,
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

/* The screen's part into s, when nothing holds the screen (screen_peek_state()), and whether it changed but for a
 * clip's progress. False in `read`, the part as it was and no change, while something does */
static bool take_screen(poll_t *p, r2_holo_state_Screen_0_1 *s, bool *read)
{
    const screen_state_t *st = &p->screen;
    *read = screen_peek_state(&p->screen);
    if (!*read) {
        return false;
    }
    char rel[FS_PATH_MAX + 8] = "";
    const bool file = st->showing == SCREEN_IMAGE || st->showing == SCREEN_CLIP;
    if (file) {
        fs_rel(st->path, rel, sizeof(rel));
    }
    const holo_screen_in_t in = {
        .showing = st->showing, .backlight = st->backlight, .rgb = { st->rgb[0], st->rgb[1], st->rgb[2] },
        .frames = st->frames, .shown = st->shown, .loops = st->loops, .plays = st->plays, .fps = st->fps,
        .path = file ? rel : NULL,
    };
    holo_screen_part(&in, &p->screen_now);
    const bool changed = holo_screen_differs(&p->screen_now, s);
    memcpy(s, &p->screen_now, sizeof(*s));
    return changed;
}

/* The scene's part into s, and whether it changed but for its time left */
static bool take_scene(r2_holo_state_Scene_0_1 *s)
{
    scene_active_t a;
    const bool on = scene_active(&a);
    r2_holo_state_Scene_0_1 now;
    holo_scene_part(on, on ? a.name : NULL, on ? a.slot : 0, on ? (int)a.then : 0, on && a.clip,
                    on ? a.remaining_s : -1, &now);
    const bool changed = holo_scene_differs(&now, s);
    memcpy(s, &now, sizeof(*s));
    return changed;
}

/* The parts in `parts` (HOLO_DIRTY_*) into Status, and whether any changed. A screen that couldn't be read is marked
 * to be read again */
static bool take_parts(poll_t *p, uint32_t parts)
{
    r2_holo_Status_0_1 *s = &p->status;
    bool changed = false;
    if (parts & HOLO_DIRTY_RING) {
        changed |= take_ring(&s->ring);
    }
    if (parts & HOLO_DIRTY_SCREEN) {
        bool read;
        changed |= take_screen(p, &s->screen, &read);
        if (!read) {
            holo_events_mark(HOLO_DIRTY_SCREEN);
        }
    }
    if (parts & HOLO_DIRTY_SCENE) {
        changed |= take_scene(&s->scene);
    }
    return changed;
}

/* Status, now that one may go (250 ms after the last): the holo looked at, the ring, the screen and the scene if they
 * changed; sent if anything did, or a second after the last. The microseconds until the poll is to look again */
static int64_t publish_status(poll_t *p, const holo_status_t *st, int64_t now)
{
    r2_holo_Status_0_1 *s = &p->status;
    bool changed = take_holo(&s->holo, st);
    const uint32_t dirty = holo_events_dirty();
    changed |= take_parts(p, dirty);
    const uint16_t seq = holo_events_last(&p->events);
    bool touch = false;
#if CONFIG_BOARD_TOUCH_ENABLE
    touch = board_touch_running();
#endif
    changed |= seq != s->event_seq || touch != s->reporting_touch;
    s->event_seq = seq;
    s->reporting_touch = touch;
    if (!holo_rate_due(&p->status_rate, now, changed, HOLO_STATUS_SOONEST_US, HOLO_STATUS_LATEST_US)) {
        return holo_rate_next(&p->status_rate, false, HOLO_STATUS_SOONEST_US, HOLO_STATUS_LATEST_US) - now;
    }
    take_parts(p, HOLO_DIRTY_ALL & ~dirty);     /* the rest as they are now: times left, a clip's progress */
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
    holo_events_look(esp_timer_get_time());
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
        holo_events_online(&p->events);     /* what happened before isn't sent */
        p->axes = (r2_holo_Axes_0_1){ r2_holo_Axes_0_1_NONE, r2_holo_Axes_0_1_NONE };
        holo_events_mark(HOLO_DIRTY_ALL);
    }
    /* Only what may go is looked at: the rest waits out its spacing, and a port that is off isn't looked at */
    const int64_t now = esp_timer_get_time();
    const int64_t status_wait = p->status_off ? INT64_MAX : closed_for(&p->status_rate, HOLO_STATUS_SOONEST_US, now);
    const int64_t axes_wait = p->axes_off ? INT64_MAX : closed_for(&p->axes_rate, HOLO_AXES_MOVING_US, now);
    if (moved && (status_wait == 0 || axes_wait == 0) && have) {
        have = holo_status(0, &st) == ESP_OK;       /* the setpoint has just changed it */
    }
    /* Events first, so that a Status built now carries the number of the latest one handed to Event's mailbox */
    const int64_t e_wait = (int64_t)holo_events_publish(&p->events, now) * 1000;
    const int64_t s_wait = status_wait > 0 ? status_wait : publish_status(p, have ? &st : NULL, now);
    const int64_t a_wait = axes_wait > 0 ? axes_wait : publish_axes(p, have ? &st : NULL, now);
    int64_t wait = s_wait < a_wait ? s_wait : a_wait;
    wait = e_wait < wait ? e_wait : wait;
    const int64_t ms = wait >= (int64_t)longest * 1000 ? longest : (wait + 999) / 1000;
    return ms < 1 ? 1 : (uint32_t)ms;
}
