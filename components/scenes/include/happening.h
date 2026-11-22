/*
 * The board's happenings as the kit's events keep them (runner.c's clip_started, clip_ended, scene_started and
 * scene_ended, touch.c's touch: their JSON, with the store's "event" and "seq"), read back into one typed struct,
 * for whoever sends them on in a form of its own: the host link's 8-byte native records (hl_native.c) and the
 * Cyphal node's r2.holo.Event (main/holo_cyphal_events.c). The numbers stay as the JSON has them; each
 * consumer rounds them to its own fields. And the names the scenes give `then` and a scene's end.
 *
 * Plain C over cJSON, no ESP-IDF: tests/host builds it.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cJSON;

/* What a scene does when it ends, and its names in the JSON (SCENE_THEN_NAMES[then]) */
typedef enum { SCENE_THEN_STAY, SCENE_THEN_RESTORE, SCENE_THEN_OFF } scene_then_t;
extern const char *const SCENE_THEN_NAMES[];
/* How a watched scene ended (scene_ended's `by`), and the names */
typedef enum { SCENE_BY_CLIP, SCENE_BY_TIME, SCENE_BY_END, SCENE_BY_REPLACED } scene_by_t;
extern const char *const SCENE_BY_NAMES[];

typedef enum {
    HAPPENING_NONE,             /* not one of these: any other kind, or not a happening */
    HAPPENING_CLIP_STARTED,
    HAPPENING_CLIP_ENDED,
    HAPPENING_SCENE_STARTED,
    HAPPENING_SCENE_ENDED,
    HAPPENING_TOUCH,
} happening_kind_t;

/*
 * One happening, field by field. A string points into the JSON it was read from (NULL when the JSON lacks it); a
 * number the JSON lacks, or that isn't one, is 0 (a slot that is none, a duration that is none); a name that isn't
 * one of the table's is its first (stay, clip).
 */
typedef struct {
    happening_kind_t kind;
    uint16_t seq;               /* the store's "seq"; 0 without one */
    const char *path;           /* clip_*: from the volume's root */
    bool loop;                  /* clip_started: forever */
    double plays, frames, fps;  /* clip_started */
    bool finished;              /* clip_ended: played out, not stopped */
    const char *name;           /* scene_* */
    double slot;                /* scene_*: 0 for none */
    scene_then_t then;          /* scene_* */
    scene_by_t by;              /* scene_ended */
    bool until_clip_ends;       /* scene_started */
    double duration_s;          /* scene_started: 0 for none */
    bool up;                    /* touch: "up" (else down) */
    double x, y;                /* touch */
} happening_t;

/* A happening's JSON, as the store keeps it, read: false (out->kind HAPPENING_NONE) for anything else */
bool happening_of(const struct cJSON *json, happening_t *out);

#ifdef __cplusplus
}
#endif
