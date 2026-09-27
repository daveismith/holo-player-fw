/*
 * Scenes and settings: what the screen, the LEDs and the holo do together, saved by name, and
 * what the board starts with. Nothing here is HTTP: the console (`scene`, `settings`), the
 * start of the board and the API (components/webui) all use it. See scenes.c.
 *
 * A scene is JSON, as the API describes it (the Scene schema):
 *   {"name": "cantina", "description": "...",
 *    "screen": {"path": "/clips/cantina.mov", "loop": true} | {"colour": "#ff8000"}
 *              | {"calibration": true} | {"clear": true},
 *    "leds":   {"mode": "solid", "colour": "orange", "loop": false, "brightness": 30},
 *    "holo":   {"motion": "twitch", "range": 50, ...},
 *    "then": "stay" | "restore" | "off", "duration_s": 30}
 * Each part is optional; a part left out leaves that alone. A scene ends when its clip has played
 * (once, or its `loops`; not with `loop`), or after `duration_s`, whichever is first; `then` is what happens then: nothing
 * (stay), the screen, LEDs and holo back to what they did before it (restore), or all off.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What went wrong, for an API to turn into a status: 400, 400, 404, 422, 409, 409, 500. */
typedef enum {
    SCENE_OK,
    SCENE_BAD,          /* not what the part takes */
    SCENE_BAD_PATH,     /* a path fs_path() refuses */
    SCENE_MISSING,      /* no such file, or no such scene */
    SCENE_UNPLAYABLE,   /* a file the screen cannot show */
    SCENE_NOT_READY,    /* the holo cannot move */
    SCENE_FULL,         /* SCENE_MAX saved already */
    SCENE_FAILED,
} scene_err_t;

#define SCENE_MAX       16
#define SCENE_NAME_MAX  32
#define SCENE_JSON_MAX  1024    /* a saved scene, as text */

/*
 * The parts: each checked, then done -- or, with `dry`, only checked (a file is not looked
 * for). `why` is a sentence, without the part's name. `in_scene` lets the screen take
 * {"clear": true}.
 */
scene_err_t scene_do_screen(const cJSON *show, bool in_scene, bool dry, char *why, size_t why_len);
scene_err_t scene_do_leds(const cJSON *patch, bool dry, char *why, size_t why_len);
scene_err_t scene_do_holo(const cJSON *motion, bool dry, char *why, size_t why_len);

bool scene_name_ok(const char *name);

/* A scene's structure (every part dry-run), and its name. `why` names the part at fault. */
scene_err_t scene_check(const cJSON *scene, char *why, size_t why_len);

/* The screen, then the LEDs, then the holo; a part that fails stops the rest, and `why` starts
 * with its name ("screen: ..."). A missing file is SCENE_UNPLAYABLE here. A scene with `then`
 * other than stay is watched until it ends; any other scene applied first cancels that. */
scene_err_t scene_apply(const cJSON *scene, char *why, size_t why_len);

typedef enum { SCENE_THEN_STAY, SCENE_THEN_RESTORE, SCENE_THEN_OFF } scene_then_t;
extern const char *const SCENE_THEN_NAMES[];
int scene_then_of(const cJSON *then);      /* a scene_then_t, or -1 for none of them */

/* The scene being watched to its end, if one is. */
typedef struct {
    char name[SCENE_NAME_MAX + 1];
    scene_then_t then;
    bool clip;                  /* ends when its clip is done */
    double remaining_s;         /* until its time is up; -1 when it has no duration */
} scene_active_t;
bool scene_active(scene_active_t *out);

/* End the scene being watched now, doing its `then`. SCENE_MISSING when none is. */
scene_err_t scene_end(char *why, size_t why_len);

/* Start the watcher (a task of its own). Once, before any scene is applied. */
esp_err_t scenes_start(void);

/* The saved scenes. Each returns a new cJSON the caller deletes; NULL for none. */
cJSON *scene_load(const char *name);
cJSON *scene_load_all(void);        /* an array, in the order they were first saved */

/* Save (checked first); SCENE_FULL when SCENE_MAX are saved and this is a new one. */
scene_err_t scene_store(const cJSON *scene, bool *replaced, char *why, size_t why_len);
/* SCENE_MISSING for none by that name. Clears the boot scene if it was this one. */
scene_err_t scene_remove(const char *name);

/* `scene` */
void scene_register_commands(void);

/* ---- settings: what the board starts with ---- */

typedef struct {
    char boot_scene[SCENE_NAME_MAX + 1];    /* "" for none */
    int backlight;                          /* 1..100 */
    int led_brightness;                     /* 1..100 */
    int led_count;                          /* 1..256 */
} settings_t;

void settings_defaults(settings_t *out);
void settings_get(settings_t *out);
esp_err_t settings_save(const settings_t *s);

/* The LED count to make the strip with (before leds_init()). */
int settings_led_count(void);

/* At the start, once the screen, LEDs and holo are up: the backlight and LED brightness, then
 * the boot scene, if one is set. */
void settings_apply_boot(void);

/* `settings` */
void settings_register_commands(void);

#ifdef __cplusplus
}
#endif
