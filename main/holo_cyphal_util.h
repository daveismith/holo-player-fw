/*
 * The holo's Cyphal interface, its pure parts (holo_cyphal.c has the rest): a Motion request as the
 * holo engine's motion, a Light or Ring request as a change to the LED ring, a Scene or Screen request
 * read and checked, the refusals' words in r2.holo.Error, the engine's words as r2.holo.Reason, the
 * positions as r2.holo.Axes, the board's happenings as r2.holo.Event, the screen and the scene as
 * Status's parts, and the rates Status and Axes go out at (manual/reference/cyphal.md). Plain C over
 * the generated DSDL headers and happening.h, no ESP-IDF: tests/host builds it.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "r2/holo/Axes_0_1.h"
#include "r2/holo/Error_0_1.h"
#include "r2/holo/Event_0_1.h"
#include "r2/holo/Light_0_1.h"
#include "r2/holo/MotionSpec_0_1.h"
#include "r2/holo/Reason_0_1.h"
#include "r2/holo/Ring_0_1.h"
#include "r2/holo/Scene_0_1.h"
#include "r2/holo/Screen_0_1.h"
#include "r2/holo/state/RingSetting_0_1.h"
#include "r2/holo/state/Scene_0_1.h"
#include "r2/holo/state/Screen_0_1.h"
#include "happening.h"

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

/* ---- Ring */

/* A Ring request as a change to the ring (scene_leds_t, field for field: PATCH /leds's), each field there only if
 * the request gives it: a mode, a colour, a loop (each an optional array), a brightness (0 none) and a period
 * (0 the mode's default: none). The ranges are the change's to check, with the JSON API's words */
typedef struct {
    bool has_mode; int mode;        /* HOLO_RING_*, or past them for the check to refuse */
    bool has_colour; uint8_t rgb[3];
    bool has_loop; bool loop;
    bool has_brightness; int brightness;
    bool has_period; int period_ms;
} holo_ring_change_t;

void holo_ring_change(const r2_holo_Ring_Request_0_1 *q, holo_ring_change_t *out);

/* ---- Scene */

/* A Scene request read: END, or APPLY by slot (1..255), or by name with slot 0 (as a C string, for the scenes
 * component to check against its own rules). False, with why, for an action that is none, or APPLY with slot 0
 * and no name, or a name with a NUL in it */
typedef struct {
    bool end;
    uint8_t slot;                   /* 0: by name */
    char name[r2_holo_Scene_Request_0_1_name_ARRAY_CAPACITY_ + 1];
} holo_scene_request_t;

bool holo_scene_request(const r2_holo_Scene_Request_0_1 *q, holo_scene_request_t *out, char *why, size_t why_len);

/* ---- Screen */

/* A Screen request read: what to show (Screen's KEEP ... OFF), a FILE's path as a C string (uavcan.file.Path's
 * up to 255 bytes: the board's limit, 159, and its other rules are fs_path()'s, with its words), `loop`, `loops`
 * and `whole_frame` as given (their check is the scenes component's, with the JSON API's words), a COLOUR's
 * colour, and the backlight (-1: as it is). False, with why, for a `show` that is none, a backlight over 100,
 * or a path with a NUL in it, in fs_path()'s words for a control character */
typedef struct {
    uint8_t show;                   /* r2_holo_Screen_Request_0_1_KEEP ... OFF */
    char path[uavcan_file_Path_2_0_path_ARRAY_CAPACITY_ + 1];
    bool loop;
    int loops;                      /* 0: none given */
    bool whole_frame;
    uint8_t rgb[3];
    int backlight;                  /* 0..100, or -1 */
} holo_screen_request_t;

bool holo_screen_request(const r2_holo_Screen_Request_0_1 *q, holo_screen_request_t *out, char *why, size_t why_len);

/* `n` bytes of a DSDL byte array as a C string in `out` (len bytes, room for the NUL): false if it holds a NUL, or
 * doesn't fit */
bool holo_c_string(const uint8_t *bytes, size_t n, char *out, size_t len);

/* ---- Event */

/* One of the board's happenings (happening.h, read from the JSON the kit's events keep), with its number, as an
 * r2.holo.Event: its numbers held to the type's fields, rounded (a duration to whole milliseconds), a path cut to 255
 * bytes and a name to 32 (neither ever is, on this board). False for a kind the type doesn't have */
bool holo_event(const happening_t *h, uint16_t seq, r2_holo_Event_0_1 *out);

/* ---- the screen and the scene in Status */

/* What the screen shows, as the video component's screen_state_t has it (GET /screen's), for holo_screen_part() */
typedef struct {
    int showing;                    /* screen_showing_t: the same numbers as state.Screen's */
    int backlight;
    uint8_t rgb[3];                 /* a colour's */
    uint32_t frames, shown, loops, plays;   /* a clip's: `loops` its times through so far */
    double fps;
    const char *path;               /* an image's or a clip's, from the volume's root; NULL otherwise */
} holo_screen_in_t;

/* As state.Screen, zeroed first (padding and all, for holo_screen_differs()'s memcmp): a colour only while one
 * shows (black otherwise), a clip's progress only while one plays, a path only for an image or a clip */
void holo_screen_part(const holo_screen_in_t *in, r2_holo_state_Screen_0_1 *out);
/* Whether two made by holo_screen_part() differ but for a clip's progress (`shown`, `passes`): a change for Status */
bool holo_screen_differs(const r2_holo_state_Screen_0_1 *a, const r2_holo_state_Screen_0_1 *b);

/* The scene running to its end, as the scenes component's scene_active_t has it (GET /scenes's `active`), as
 * state.Scene, zeroed first: `running` false for none, and every field then 0 or empty; `then` a scene_then_t (the
 * same numbers); `remaining_s` -1 for no duration, NEVER then, and any other negative (a deadline just passed) 0 */
void holo_scene_part(bool running, const char *name, int slot, int then, bool until_clip_ends, double remaining_s,
                     r2_holo_state_Scene_0_1 *out);
/* Whether two made by holo_scene_part() differ but for the time left (whether there is one counts): a change for
 * Status */
bool holo_scene_differs(const r2_holo_state_Scene_0_1 *a, const r2_holo_state_Scene_0_1 *b);

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
