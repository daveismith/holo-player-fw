/*
 * The holo on Cyphal: its services' handlers (cyphal_handler_t) and the handler table (manual/reference/cyphal.md).
 * Each work() runs on the node's worker, or its second worker for a `slow` one; see holo_cyphal_internal.h.
 *
 * Motion goes through the code POST /api/v1/holo/motion runs once it has read its JSON, as the host link's
 * requests do: scene_holo_change(), which checks, refuses and starts a motion exactly as for HTTP. The request
 * becomes the engine's motion (holo_motion_args(), its kind mapped by HOLO_SPEC_OF_KIND[]), and the refusal its
 * Error, in the same words. `off`, the release, also turns the ring off, since the ring is the holo's light here,
 * whatever the engine said. Light goes through the code PATCH /api/v1/leds runs (scene_leds_change()), which takes a
 * leia's mark besides; a leia's centre is the same motion code's `center`. Ring goes through PATCH /api/v1/leds's
 * code too, as the host link's LEDS frame does.
 *
 * Scene and Screen go through the code POST /api/v1/scenes/apply and /scenes/end, and POST /api/v1/screen/show,
 * DELETE /api/v1/screen and PATCH /api/v1/screen run (scene_apply_by(), scene_end(); scene_screen_change(),
 * screen_set_backlight()), with their refusals and words. Both can take long -- a file is opened and its header
 * read, a still is decoded on the task that shows it (a PNG in a few hundred milliseconds), the panel takes 120 ms to
 * sleep, a scene does all of these -- so their handlers are the node's `slow` ones: they run on its second worker,
 * in turn, and the worker's setpoints and published messages never wait behind them.
 */
#include <stdio.h>
#include <string.h>
#include "board_api.h"
#include "esp_log.h"
#include "holo.h"
#include "holo_cyphal.h"
#include "holo_cyphal_internal.h"
#include "holo_cyphal_util.h"
#include "leds.h"
#include "r2/holo/Axes_0_1.h"
#include "r2/holo/Light_0_1.h"
#include "r2/holo/Motion_0_1.h"
#include "r2/holo/Ring_0_1.h"
#include "r2/holo/Scene_0_1.h"
#include "r2/holo/Screen_0_1.h"
#include "r2/holo/Status_0_1.h"
#include "r2/holo/ports.h"
#include "scenes.h"
#include "video_player.h"

static const char *TAG = "holo_cyphal";

_Static_assert(sizeof(r2_holo_Motion_Request_0_1) <= CYPHAL_APP_REQUEST_MAX &&
               sizeof(r2_holo_Light_Request_0_1) <= CYPHAL_APP_REQUEST_MAX &&
               sizeof(r2_holo_Scene_Request_0_1) <= CYPHAL_APP_REQUEST_MAX &&
               sizeof(r2_holo_Screen_Request_0_1) <= CYPHAL_APP_REQUEST_MAX &&
               sizeof(r2_holo_Ring_Request_0_1) <= CYPHAL_APP_REQUEST_MAX, "a request fits a job slot");
_Static_assert(API_CODE_OK == r2_holo_Error_0_1_OK && API_CODE_BAD_REQUEST == r2_holo_Error_0_1_BAD_REQUEST &&
               API_CODE_NOT_FOUND == r2_holo_Error_0_1_NOT_FOUND && API_CODE_NOT_PLAYABLE == r2_holo_Error_0_1_NOT_PLAYABLE &&
               API_CODE_NOT_READY == r2_holo_Error_0_1_NOT_READY && API_CODE_BUSY == r2_holo_Error_0_1_BUSY &&
               API_CODE_FAILED == r2_holo_Error_0_1_FAILED, "Error's codes are the routes' (api_codes.h)");
_Static_assert(r2_holo_Scene_Request_0_1_name_ARRAY_CAPACITY_ == SCENE_NAME_MAX,
               "Scene's name holds a scene's longest (holo_scene_request()'s words say so)");

/* ------------------------------------------------------------------ responses */

/* An Error: the code, the reason, and the message (empty for none) */
static r2_holo_Error_0_1 error_of(uint8_t code, r2_holo_Reason_0_1 reason, const char *message)
{
    r2_holo_Error_0_1 e = { .code = code, .reason = reason };
    holo_error_message(&e, message);
    return e;
}

static const r2_holo_Reason_0_1 NO_REASON = { r2_holo_Reason_0_1_NONE, r2_holo_Reason_0_1_AXIS_NONE };

/* The words a busy answer carries (the node's job slots all held) */
#define BUSY_WORDS "busy: the board is still working through earlier requests"

typedef size_t (*respond_fn)(const r2_holo_Error_0_1 *e, uint8_t *out, size_t room);

/* A service whose response is only `{result: Error}`: NAME_response(), NAME_decode() (its request) and NAME_busy() */
#define ERROR_ONLY_SERVICE(NAME, TYPE)                                                                               \
    static size_t NAME##_response(const r2_holo_Error_0_1 *e, uint8_t *out, size_t room)                            \
    {                                                                                                               \
        const TYPE##_Response_0_1 r = { .result = *e };                                                             \
        return TYPE##_Response_0_1_serialize_(&r, out, &room) < 0 ? 0 : room;                                       \
    }                                                                                                               \
    static bool NAME##_decode(const uint8_t *payload, size_t size, void *in)                                        \
    {                                                                                                               \
        return TYPE##_Request_0_1_deserialize_(in, payload, &size) >= 0;                                            \
    }                                                                                                               \
    static size_t NAME##_busy(uint8_t *out, size_t room)                                                            \
    {                                                                                                               \
        const r2_holo_Error_0_1 e = error_of(r2_holo_Error_0_1_BUSY, NO_REASON, BUSY_WORDS);                        \
        return NAME##_response(&e, out, room);                                                                      \
    }

ERROR_ONLY_SERVICE(motion, r2_holo_Motion)
ERROR_ONLY_SERVICE(ring, r2_holo_Ring)
ERROR_ONLY_SERVICE(scene, r2_holo_Scene)
ERROR_ONLY_SERVICE(screen, r2_holo_Screen)

/* Every request's ending: a refusal logged (node `from`'s `what` refused, and why), then the Error (the words only
 * for a refusal) as the response */
static size_t answer(respond_fn respond, uint8_t code, r2_holo_Reason_0_1 reason, const char *why, uint8_t from,
                     const char *what, uint8_t *out, size_t room)
{
    if (code != r2_holo_Error_0_1_OK) {
        ESP_LOGI(TAG, "node %u's %s refused: %s", from, what, why);
    }
    const r2_holo_Error_0_1 e = error_of(code, reason, code == r2_holo_Error_0_1_OK ? NULL : why);
    return respond(&e, out, room);
}

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

/* The reason a refusal with Error's `code` carries: why the holo can't move for NOT_READY (the routes give it only
 * when the holo, or a scene's holo part, can't run: SCENE_NOT_READY), none otherwise */
static r2_holo_Reason_0_1 reason_of(uint8_t code)
{
    return code == r2_holo_Error_0_1_NOT_READY ? holo_why() : NO_REASON;
}

/* ------------------------------------------------------------------ Motion */

/* The engine's kind for MotionSpec's, or one past HOLO_OFF, which scene_holo_change() refuses, for none */
static holo_motion_kind_t kind_of_spec(unsigned spec)
{
    for (size_t k = 0; k < sizeof(HOLO_SPEC_OF_KIND); k++) {
        if (HOLO_SPEC_OF_KIND[k] == spec) {
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
    holo_setpoint_forget();
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
    const r2_holo_Error_0_1 e = error_of(code, reason_of(code), message);
    return motion_response(&e, out, room);
}

/* ------------------------------------------------------------------ Light */

static size_t light_response(const r2_holo_Error_0_1 *e, bool centred, uint8_t *out, size_t room)
{
    const r2_holo_Light_Response_0_1 r = { .result = *e, .centred = centred };
    return r2_holo_Light_Response_0_1_serialize_(&r, out, &room) < 0 ? 0 : room;
}

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
    holo_setpoint_forget();
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

/* ------------------------------------------------------------------ Ring */

/* PATCH /api/v1/leds, as the host link's LEDS frame makes it: only what the request gives */
static size_t ring_work(const void *in, uint8_t from, uint8_t *out, size_t room)
{
    holo_ring_change_t c;
    holo_ring_change(in, &c);
    const scene_leds_t change = {
        .has_mode = c.has_mode, .mode = c.mode,
        .has_colour = c.has_colour, .rgb = { c.rgb[0], c.rgb[1], c.rgb[2] },
        .has_loop = c.has_loop, .loop = c.loop,
        .has_brightness = c.has_brightness, .brightness = c.brightness,
        .has_period = c.has_period, .period_ms = c.period_ms,
    };
    char why[128] = "";
    const scene_err_t err = scene_leds_change(&change, false, why, sizeof(why));
    return answer(ring_response, (uint8_t)api_scene_code(err), NO_REASON, why, from, "ring", out, room);
}

/* ------------------------------------------------------------------ Scene, on the second worker */

/* POST /api/v1/scenes/apply with a saved scene's name (the host link's SCENE, by its slot), or /scenes/end: Error's
 * code, and the words in `why` */
static uint8_t scene_change(const holo_scene_request_t *q, char *why, size_t len)
{
    if (q->end) {
        return (uint8_t)api_scene_code(scene_end(why, len));
    }
    if (q->slot == 0 && !scene_name_ok(q->name)) {
        snprintf(why, len, "give `name`: 1-%d letters, digits, spaces, _ . and -", SCENE_NAME_MAX);   /* the routes' words */
        return r2_holo_Error_0_1_BAD_REQUEST;
    }
    return (uint8_t)api_scene_code(scene_apply_by(q->name, q->slot, why, len));
}

static size_t scene_work(const void *in, uint8_t from, uint8_t *out, size_t room)
{
    holo_scene_request_t q;
    char why[128] = "";
    const uint8_t code = holo_scene_request(in, &q, why, sizeof(why)) ? scene_change(&q, why, sizeof(why))
                                                                       : r2_holo_Error_0_1_BAD_REQUEST;
    /* A scene whose holo part can't run is refused NOT_READY, with why, as Motion is */
    return answer(scene_response, code, reason_of(code), why, from, "scene", out, room);
}

/* ------------------------------------------------------------------ Screen, on the second worker */

/* POST /api/v1/screen/show (a file, a colour, the crosshair), DELETE /api/v1/screen (nothing), then PATCH
 * /api/v1/screen (the backlight), only once what shows has been done: Error's code, and the words in `why` */
static uint8_t screen_change(const holo_screen_request_t *q, char *why, size_t len)
{
    static const scene_screen_kind_t KIND_OF[] = {
        [r2_holo_Screen_Request_0_1_FILE] = SCENE_SCREEN_FILE, [r2_holo_Screen_Request_0_1_COLOUR] = SCENE_SCREEN_COLOUR,
        [r2_holo_Screen_Request_0_1_CALIBRATION] = SCENE_SCREEN_CALIBRATION,
        [r2_holo_Screen_Request_0_1_OFF] = SCENE_SCREEN_CLEAR,
    };
    scene_err_t err = SCENE_OK;
    if (q->show != r2_holo_Screen_Request_0_1_KEEP) {
        const scene_screen_t s = { .show = KIND_OF[q->show], .path = q->path, .loop = q->loop, .loops = q->loops,
                                   .whole_frame = q->whole_frame, .rgb = { q->rgb[0], q->rgb[1], q->rgb[2] } };
        err = scene_screen_change(&s, false, why, len);
    }
    if (err == SCENE_OK && q->backlight >= 0) {
        screen_set_backlight(q->backlight);
    }
    return (uint8_t)api_scene_code(err);
}

static size_t screen_work(const void *in, uint8_t from, uint8_t *out, size_t room)
{
    holo_screen_request_t q;            /* 0.3 KB: the second worker's stack has the room (CONFIG_CYPHAL_SLOW_STACK) */
    char why[160] = "";
    const uint8_t code = holo_screen_request(in, &q, why, sizeof(why)) ? screen_change(&q, why, sizeof(why))
                                                                         : r2_holo_Error_0_1_BAD_REQUEST;
    return answer(screen_response, code, NO_REASON, why, from, "screen request", out, room);
}

/* ------------------------------------------------------------------ the table */

const cyphal_handler_t HOLO_CYPHAL_HANDLERS[] = {
    { .port = R2_HOLO_PORT_STATUS, .publish_max = r2_holo_Status_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_ },
    { .port = R2_HOLO_PORT_EVENT, .publish_max = r2_holo_Event_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_,
      .publish_depth = HOLO_EVENT_DEPTH },
    { .port = R2_HOLO_PORT_AXES, .publish_max = r2_holo_Axes_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_ },
    { .port = R2_HOLO_PORT_SETPOINT, .extent = r2_holo_Axes_0_1_EXTENT_BYTES_, .on_message = holo_setpoint_message },
    { .port = R2_HOLO_PORT_MOTION, .extent = r2_holo_Motion_Request_0_1_EXTENT_BYTES_, .decode = motion_decode,
      .in_size = sizeof(r2_holo_Motion_Request_0_1), .work = motion_work, .busy = motion_busy },
    { .port = R2_HOLO_PORT_LIGHT, .extent = r2_holo_Light_Request_0_1_EXTENT_BYTES_, .decode = light_decode,
      .in_size = sizeof(r2_holo_Light_Request_0_1), .work = light_work, .busy = light_busy },
    { .port = R2_HOLO_PORT_SCENE, .extent = r2_holo_Scene_Request_0_1_EXTENT_BYTES_, .decode = scene_decode,
      .in_size = sizeof(r2_holo_Scene_Request_0_1), .work = scene_work, .busy = scene_busy, .slow = true },
    { .port = R2_HOLO_PORT_SCREEN, .extent = r2_holo_Screen_Request_0_1_EXTENT_BYTES_, .decode = screen_decode,
      .in_size = sizeof(r2_holo_Screen_Request_0_1), .work = screen_work, .busy = screen_busy, .slow = true },
    { .port = R2_HOLO_PORT_RING, .extent = r2_holo_Ring_Request_0_1_EXTENT_BYTES_, .decode = ring_decode,
      .in_size = sizeof(r2_holo_Ring_Request_0_1), .work = ring_work, .busy = ring_busy },
};
const size_t HOLO_CYPHAL_HANDLER_COUNT = sizeof(HOLO_CYPHAL_HANDLERS) / sizeof(HOLO_CYPHAL_HANDLERS[0]);
