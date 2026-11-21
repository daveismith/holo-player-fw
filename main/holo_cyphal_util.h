/*
 * The holo's Cyphal interface, its pure parts (holo_cyphal.c has the rest): a Motion request as the
 * holo engine's motion, a Light request as a change to the LED ring, the refusals' words in
 * r2.holo.Error, the engine's words as r2.holo.Reason, the positions as r2.holo.Axes,
 * and the rates Status and Axes go out at (manual/reference/cyphal.md). Plain C over the generated
 * DSDL headers, no ESP-IDF: tests/host builds it.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "r2/holo/Axes_0_1.h"
#include "r2/holo/Error_0_1.h"
#include "r2/holo/Light_0_1.h"
#include "r2/holo/MotionSpec_0_1.h"
#include "r2/holo/Reason_0_1.h"
#include "r2/holo/state/RingSetting_0_1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Motion */

/*
 * A motion as the holo engine's holo_motion_t holds it (holo.h), field for field, but its kind, which is
 * MotionSpec's here (the engine numbers its kinds differently; holo_cyphal.c maps them). 0 is a field's default,
 * as it is to the engine, and -1 a duration by the distance.
 */
typedef struct {
    unsigned kind;                  /* MotionSpec's: CENTER ... OFF; past OFF, none */
    int x, y, duration_ms, range, count, period_ms;
    float interval_min_s, interval_max_s, time_s;
} holo_motion_args_t;

/*
 * A Motion request as the engine's motion, for the check POST /api/v1/holo/motion runs (scene_holo_change()),
 * which refuses it with the JSON API's words: the fields the motion uses, and only those (a field it doesn't use
 * is left 0, whatever the request holds, where the check would refuse it). Zero is the default, as in the JSON
 * request a field left out is; an empty duration is -1, by the distance. The times go from ms to seconds; the
 * twitch's interval goes as both or neither, so that one alone reaches the check, which refuses it. A kind that
 * is none keeps its number, for the check to refuse.
 */
void holo_motion_args(const r2_holo_MotionSpec_0_1 *m, holo_motion_args_t *out);

/* And back: the engine's last motion asked for, as MotionSpec, in Status: each field held to what MotionSpec's
 * holds, the times as whole milliseconds; a kind past OFF reads as a stop */
void holo_motion_spec(const holo_motion_args_t *m, r2_holo_MotionSpec_0_1 *out);

/* The engine's motion ("hold", "move", "twitch", ...) as state.Holo's number: HOLD for one it doesn't know */
uint8_t holo_motion_number(const char *name);

/* ---- Light */

/* The LED ring's modes, by number: leds_mode_t's and state.RingSetting's */
enum { HOLO_RING_OFF, HOLO_RING_SOLID, HOLO_RING_WIPE, HOLO_RING_RAINBOW, HOLO_RING_FLICKER, HOLO_RING_PULSE,
       HOLO_RING_BLINK };
#define HOLO_LEIA_MS 30000          /* a leia's time limit when it gives none */

/* A Light request as a change to the ring (the scenes component's scene_leds_t, field for field), and
 * whether the holo then turns to the centre (a leia). The modes: OFF the ring off, ON solid, PULSE, FLICKER,
 * BLINK the ring's of those names; LEIA a timed flicker at level 100, 30 s unless timed, marked as a leia.
 * A level and a colour, when given, apply to each but OFF's level and LEIA's level; a period only to a pulse
 * or a blink; a time limit makes it timed. The ranges are the change's to check, with the JSON API's words.
 * False, with why, for a mode that isn't one */
typedef struct {
    int mode;                       /* HOLO_RING_* */
    bool has_colour; uint8_t rgb[3];
    bool has_period; int period_ms;
    bool has_level; int level;
    bool has_time; double time_s;
    bool leia, centre;
} holo_light_change_t;

bool holo_light_change(const r2_holo_Light_Request_0_1 *q, holo_light_change_t *out, char *why, size_t why_len);

/* ---- Error and Reason (Error's codes are the host link's: api_codes.h) */

/* `text` as Error's message: cut to 64 bytes, at a UTF-8 character's start */
void holo_error_message(r2_holo_Error_0_1 *e, const char *text);

/* The holo engine's words for why it can't move (HOLO_WHY_*: "not fitted" ...), and the servo ident of the
 * axis at fault, as a Reason: the axis by the holo's horizontal and vertical idents. NONE for no why */
r2_holo_Reason_0_1 holo_reason(const char *why, const char *axis, const char *h, const char *v);

/* ---- Axes */

/* A position, -1..1 of the travel each side of the centre, in hundredths of a percent: NONE when not placed */
int16_t holo_axes_value(bool placed, float position);
/* A setpoint, -10000..10000 (held to it), as the engine's permille of the travel, 0 (closed) to 1000
 * (open), which the holo places an axis by; -1 for NONE */
int holo_setpoint_permille(int16_t value);
/* The same as whole percent, -100..100, for a MOVE */
int holo_setpoint_percent(int16_t value);

/* ---- the ring in Status */

/* A ring setting as state.RingSetting: its brightness is the ring's times the setting's level, rounded, so
 * that it says how bright the ring is */
void holo_ring_setting(unsigned mode, const uint8_t rgb[3], bool loop, unsigned period_ms, unsigned level,
                       int brightness, r2_holo_state_RingSetting_0_1 *out);

/* ---- rates */

#define HOLO_STATUS_SOONEST_US  250000      /* Status: four a second at most */
#define HOLO_STATUS_LATEST_US   1000000     /* and once a second whatever changes */
#define HOLO_AXES_MOVING_US     100000      /* Axes: ten a second while anything moves */
#define HOLO_AXES_RESTING_US    1000000     /* and once a second at rest */

/* When a message last went */
typedef struct {
    int64_t last_us;
    bool sent;                      /* one has gone since the start */
} holo_rate_t;

/* Whether a message goes now: the first, or `hurry` (a change, a motion) once `soonest_us` has passed since
 * the last, or `latest_us` since the last whatever. When it does, from now */
bool holo_rate_due(holo_rate_t *r, int64_t now_us, bool hurry, int64_t soonest_us, int64_t latest_us);
/* When the next is due, for a wake: soonest_us after the last with `hurry`, latest_us without */
int64_t holo_rate_next(const holo_rate_t *r, bool hurry, int64_t soonest_us, int64_t latest_us);

#ifdef __cplusplus
}
#endif
