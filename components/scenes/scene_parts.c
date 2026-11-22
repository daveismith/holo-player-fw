/*
 * The three parts of a scene -- screen, LEDs, holo -- from JSON, checked and then done. The
 * API's /screen/show, PATCH /leds and /holo/motion are these too.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "fs_ops.h"
#include "holo.h"
#include "leds.h"
#include "video_player.h"
#include "scenes.h"

#define BAD(code, ...) (snprintf(why, why_len, __VA_ARGS__), (code))

static bool only_keys(const cJSON *o, const char *const *keys, size_t n, const char **bad)
{
    for (const cJSON *k = o->child; k != NULL; k = k->next) {
        bool ok = false;
        for (size_t i = 0; i < n && !ok; i++) {
            ok = strcmp(k->string, keys[i]) == 0;
        }
        if (!ok) {
            *bad = k->string;
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ the screen */

/* A colour's or the calibration pattern's show, as a scene's result: refused because what was playing did not stop
 * in time (the video component's ESP_ERR_TIMEOUT, in its words for a file), or by the panel */
static scene_err_t shown(esp_err_t err, char *why, size_t why_len)
{
    if (err == ESP_ERR_TIMEOUT) {
        return BAD(SCENE_FAILED, "what was playing did not stop within 2 s; the screen is off, try again");
    }
    if (err != ESP_OK) {
        return BAD(SCENE_FAILED, "the panel did not take it");
    }
    return SCENE_OK;
}

scene_err_t scene_screen_change(const scene_screen_t *s, bool dry, char *why, size_t why_len)
{
    switch (s->show) {
    case SCENE_SCREEN_CALIBRATION:
        return dry ? SCENE_OK : shown(screen_show_calibration(), why, why_len);
    case SCENE_SCREEN_CLEAR:
        if (!dry) {
            screen_clear();
        }
        return SCENE_OK;
    case SCENE_SCREEN_COLOUR:
        return dry ? SCENE_OK : shown(screen_show_rgb(s->rgb), why, why_len);
    case SCENE_SCREEN_FILE:
        break;
    default:
        return BAD(SCENE_BAD, "give one of `path`, `colour` or `calibration`");
    }
    if (s->loops < 0 || s->loops > 1000) {
        return BAD(SCENE_BAD, "`loops` is how many times it plays: 1..1000");
    }
    if (s->loops > 0 && s->loop) {
        return BAD(SCENE_BAD, "give `loop` (forever) or `loops` (a number of times), not both");
    }
    /* Times through: forever, a number, or the default (a clip once, a GIF as the file says) */
    const int plays = s->loop ? 0 : s->loops > 0 ? s->loops : -1;
    char abs[FS_ABS_MAX], rel[FS_PATH_MAX + 8];
    char reason[112];
    if (fs_path(s->path, abs, sizeof(abs), reason, sizeof(reason)) != 0) {
        return BAD(SCENE_BAD_PATH, "%s", reason);
    }
    if (dry) {
        return SCENE_OK;
    }
    fs_rel(abs, rel, sizeof(rel));
    char msg[96] = "";
    const esp_err_t err = screen_show_file(abs, plays, s->whole_frame, msg, sizeof(msg));
    if (err == ESP_ERR_NOT_FOUND) {
        return BAD(SCENE_MISSING, "no such file: %s", rel);
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return BAD(SCENE_UNPLAYABLE, "%s: %s", rel, msg);
    }
    if (err != ESP_OK) {
        return BAD(SCENE_FAILED, "%s: %s", rel, msg);
    }
    return SCENE_OK;
}

scene_err_t scene_do_screen(const cJSON *show, bool in_scene, bool dry, char *why, size_t why_len)
{
    static const char *const KEYS[] = { "path", "loop", "loops", "whole_frame", "colour", "calibration", "clear" };
    const char *bad = NULL;
    if (!cJSON_IsObject(show)) {
        return BAD(SCENE_BAD, "an object: {path}, {colour} or {calibration: true}");
    }
    if (!only_keys(show, KEYS, sizeof(KEYS) / sizeof(KEYS[0]), &bad)) {
        return BAD(SCENE_BAD, "unknown field `%s`", bad);
    }
    const cJSON *path = cJSON_GetObjectItem(show, "path");
    const cJSON *colour = cJSON_GetObjectItem(show, "colour");
    const cJSON *calib = cJSON_GetObjectItem(show, "calibration");
    const cJSON *clear = cJSON_GetObjectItem(show, "clear");
    if ((path != NULL) + (colour != NULL) + (calib != NULL) + (clear != NULL) != 1 || (clear && !in_scene)) {
        return BAD(SCENE_BAD, "give one of `path`, `colour` or `calibration`%s", in_scene ? " (or `clear`)" : "");
    }
    if ((calib && !cJSON_IsTrue(calib)) || (clear && !cJSON_IsTrue(clear))) {
        return BAD(SCENE_BAD, "`%s` is true", calib ? "calibration" : "clear");
    }
    scene_screen_t s = { .show = calib ? SCENE_SCREEN_CALIBRATION : clear ? SCENE_SCREEN_CLEAR
                                 : colour ? SCENE_SCREEN_COLOUR : SCENE_SCREEN_FILE };
    if (colour && !(cJSON_IsString(colour) && board_parse_rgb(colour->valuestring, s.rgb))) {
        return BAD(SCENE_BAD, "not a colour: '%s' (#rrggbb, R,G,B, or a name: red, orange, ...)",
                   cJSON_IsString(colour) ? colour->valuestring : "?");
    }
    if (path) {
        const cJSON *loop = cJSON_GetObjectItem(show, "loop");
        const cJSON *loops = cJSON_GetObjectItem(show, "loops");
        const cJSON *whole = cJSON_GetObjectItem(show, "whole_frame");
        if (!cJSON_IsString(path) || (loop && !cJSON_IsBool(loop)) || (whole && !cJSON_IsBool(whole))) {
            return BAD(SCENE_BAD, "`path` is a string; `loop` and `whole_frame` are true or false");
        }
        if (loops && !(cJSON_IsNumber(loops) && loops->valuedouble >= 1 && loops->valuedouble <= 1000 &&
                       loops->valuedouble == (int)loops->valuedouble)) {
            return BAD(SCENE_BAD, "`loops` is how many times it plays: 1..1000");
        }
        s.path = path->valuestring;
        s.loop = cJSON_IsTrue(loop);
        s.loops = loops ? (int)loops->valuedouble : 0;
        s.whole_frame = cJSON_IsTrue(whole);
    }
    return scene_screen_change(&s, dry, why, why_len);
}

/* ------------------------------------------------------------------ the LEDs */

scene_err_t scene_leds_change(const scene_leds_t *c, bool dry, char *why, size_t why_len)
{
    uint32_t ms = 0;
    if (c->has_brightness && !leds_brightness_ok(c->brightness)) {
        return BAD(SCENE_BAD, "`brightness` is %d..%d", LEDS_BRIGHTNESS_MIN, LEDS_BRIGHTNESS_MAX);
    }
    if (c->has_period && !leds_period_ok(c->period_ms)) {
        return BAD(SCENE_BAD, "`period_ms` is %d..%d", LEDS_PERIOD_MIN_MS, LEDS_PERIOD_MAX_MS);
    }
    if (c->has_level && !leds_level_ok(c->level)) {
        return BAD(SCENE_BAD, "`level` is 0..%d", LEDS_LEVEL_MAX);
    }
    if (c->has_time && !leds_timed_ms(c->time_s, &ms)) {
        return BAD(SCENE_BAD, "`time_s` is 0.1..3600");
    }
    if (c->has_mode && (c->mode < 0 || c->mode >= LEDS_MODES)) {
        return BAD(SCENE_BAD, "`mode` is off, solid, wipe, rainbow, flicker, pulse or blink");
    }
    if (dry) {
        return SCENE_OK;
    }

    if (c->has_brightness) {
        leds_set_brightness(c->brightness);
    }
    if (!c->has_mode && !c->has_colour && !c->has_loop && !c->has_period && !c->has_level && !c->has_time) {
        return SCENE_OK;        /* the brightness alone: what is running carries on */
    }
    leds_state_t st;
    leds_state(&st);
    leds_setting_t s = st.now;
    if (c->has_mode) {
        /* A new mode starts fresh: looping only if asked, at its default period, at full level */
        s.mode = (leds_mode_t)c->mode;
        s.loop = false;
        s.period_ms = 0;
        s.level = 100;
    }
    if (c->has_colour) {
        memcpy(s.rgb, c->rgb, 3);
    } else if (s.rgb[0] == 0 && s.rgb[1] == 0 && s.rgb[2] == 0) {
        s.rgb[0] = s.rgb[1] = s.rgb[2] = 255;     /* never given one: white, as `leds wipe` */
    }
    if (c->has_loop) {
        s.loop = c->loop;
    }
    if (c->has_period) {
        s.period_ms = (uint16_t)c->period_ms;
    }
    if (c->has_level) {
        s.level = (uint8_t)c->level;
    }
    const esp_err_t err = c->has_time ? leds_set_timed(&s, ms, c->leia) : leds_set(&s);
    if (err != ESP_OK) {
        return BAD(SCENE_FAILED, "%s", esp_err_to_name(err));
    }
    return SCENE_OK;
}

/* A whole number, if present: false when present and not one. */
static bool get_int(const cJSON *o, const char *key, int *out)
{
    const cJSON *v = cJSON_GetObjectItem(o, key);
    if (v == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(v) || v->valuedouble != floor(v->valuedouble) || fabs(v->valuedouble) > 1e9) {
        return false;
    }
    *out = (int)v->valuedouble;
    return true;
}

scene_err_t scene_do_leds(const cJSON *patch, bool dry, char *why, size_t why_len)
{
    static const char *const KEYS[] = { "mode", "colour", "loop", "brightness", "period_ms", "level", "time_s" };
    const char *bad = NULL;
    if (!cJSON_IsObject(patch) || patch->child == NULL) {
        return BAD(SCENE_BAD, "send mode, colour, loop, brightness, period_ms, level or time_s");
    }
    if (!only_keys(patch, KEYS, sizeof(KEYS) / sizeof(KEYS[0]), &bad)) {
        return BAD(SCENE_BAD, "unknown field `%s`", bad);
    }
    const cJSON *mode = cJSON_GetObjectItem(patch, "mode");
    const cJSON *colour = cJSON_GetObjectItem(patch, "colour");
    const cJSON *loop = cJSON_GetObjectItem(patch, "loop");
    const cJSON *bright = cJSON_GetObjectItem(patch, "brightness");
    const cJSON *time = cJSON_GetObjectItem(patch, "time_s");
    scene_leds_t c = {
        .has_mode = mode != NULL, .has_colour = colour != NULL, .has_loop = loop != NULL,
        .has_brightness = bright != NULL, .has_period = cJSON_GetObjectItem(patch, "period_ms") != NULL,
        .has_level = cJSON_GetObjectItem(patch, "level") != NULL, .has_time = time != NULL,
    };
    leds_mode_t m;
    if (mode != NULL && !(cJSON_IsString(mode) && leds_mode_of(mode->valuestring, &m))) {
        return BAD(SCENE_BAD, "`mode` is off, solid, wipe, rainbow, flicker, pulse or blink");
    }
    c.mode = mode != NULL ? (int)m : 0;
    if (colour != NULL && !(cJSON_IsString(colour) && board_parse_rgb(colour->valuestring, c.rgb))) {
        return BAD(SCENE_BAD, "not a colour: '%s' (#rrggbb, R,G,B, or a name: red, orange, ...)",
                   cJSON_IsString(colour) ? colour->valuestring : "?");
    }
    if (loop != NULL && !cJSON_IsBool(loop)) {
        return BAD(SCENE_BAD, "`loop` is true or false");
    }
    c.loop = cJSON_IsTrue(loop);
    if (bright != NULL && !(cJSON_IsNumber(bright) && leds_brightness_ok(bright->valuedouble))) {
        return BAD(SCENE_BAD, "`brightness` is %d..%d", LEDS_BRIGHTNESS_MIN, LEDS_BRIGHTNESS_MAX);
    }
    c.brightness = bright != NULL ? (int)bright->valuedouble : 0;
    if (!get_int(patch, "period_ms", &c.period_ms) || !get_int(patch, "level", &c.level)) {
        return BAD(SCENE_BAD, "`period_ms` and `level` are whole numbers");
    }
    if (time != NULL && !cJSON_IsNumber(time)) {
        return BAD(SCENE_BAD, "`time_s` is 0.1..3600");
    }
    c.time_s = time != NULL ? time->valuedouble : 0;
    return scene_leds_change(&c, dry, why, why_len);
}

/* ------------------------------------------------------------------ the holo */

static const struct { const char *name; holo_motion_kind_t kind; } MOTIONS[] = {
    { "center", HOLO_CENTER }, { "move", HOLO_MOVE }, { "nudge", HOLO_NUDGE }, { "twitch", HOLO_TWITCH },
    { "wag", HOLO_WAG }, { "nod", HOLO_NOD }, { "scan", HOLO_SCAN }, { "circle", HOLO_CIRCLE },
    { "stop", HOLO_STOP }, { "off", HOLO_OFF },
};

#define MOTION_WORDS "`motion` is center, move, nudge, twitch, wag, nod, scan, circle, stop or off"

scene_err_t scene_holo_change(const holo_motion_t *m, bool dry, char *why, size_t why_len)
{
    if ((unsigned)m->kind > HOLO_OFF) {
        return BAD(SCENE_BAD, MOTION_WORDS);
    }
    if (!holo_motion_check(m, why, why_len)) {
        return SCENE_BAD;
    }
    if (dry) {
        return SCENE_OK;
    }
    const esp_err_t err = holo_motion(0, m, why, why_len);
    if (err == ESP_ERR_INVALID_ARG) {
        return SCENE_BAD;
    }
    if (err != ESP_OK) {
        return SCENE_NOT_READY;     /* refused, or no holo: `why` says */
    }
    return SCENE_OK;
}

scene_err_t scene_do_holo(const cJSON *body, bool dry, char *why, size_t why_len)
{
    static const char *const KEYS[] = { "motion", "x", "y", "duration_ms", "range", "count", "period_ms",
                                        "interval_s", "time_s" };
    const char *bad = NULL;
    if (!cJSON_IsObject(body)) {
        return BAD(SCENE_BAD, "an object: {motion, ...}");
    }
    if (!only_keys(body, KEYS, sizeof(KEYS) / sizeof(KEYS[0]), &bad)) {
        return BAD(SCENE_BAD, "unknown field `%s`", bad);
    }
    const cJSON *motion = cJSON_GetObjectItem(body, "motion");
    holo_motion_t m = { .duration_ms = -1 };
    bool found = false;
    for (size_t i = 0; cJSON_IsString(motion) && i < sizeof(MOTIONS) / sizeof(MOTIONS[0]); i++) {
        if (strcmp(motion->valuestring, MOTIONS[i].name) == 0) {
            m.kind = MOTIONS[i].kind;
            found = true;
        }
    }
    if (!found) {
        return BAD(SCENE_BAD, MOTION_WORDS);
    }
    if (!get_int(body, "x", &m.x) || !get_int(body, "y", &m.y) || !get_int(body, "duration_ms", &m.duration_ms) ||
        !get_int(body, "range", &m.range) || !get_int(body, "count", &m.count) ||
        !get_int(body, "period_ms", &m.period_ms)) {
        return BAD(SCENE_BAD, "`x`, `y`, `duration_ms`, `range`, `count` and `period_ms` are whole numbers");
    }
    if ((m.kind == HOLO_MOVE || m.kind == HOLO_NUDGE) &&
        (!cJSON_GetObjectItem(body, "x") || !cJSON_GetObjectItem(body, "y"))) {
        return BAD(SCENE_BAD, "%s takes `x` and `y`", motion->valuestring);
    }
    if (cJSON_GetObjectItem(body, "range") && m.range < 1) {
        return BAD(SCENE_BAD, "`range` is 1..100");
    }
    if (cJSON_GetObjectItem(body, "count") && m.count < 1) {
        return BAD(SCENE_BAD, "`count` is 1..20");
    }
    if (cJSON_GetObjectItem(body, "duration_ms") && m.duration_ms < 0) {
        return BAD(SCENE_BAD, "`duration_ms` is 0..10000");
    }
    const cJSON *iv = cJSON_GetObjectItem(body, "interval_s");
    if (iv != NULL) {
        if (!cJSON_IsArray(iv) || cJSON_GetArraySize(iv) != 2 || !cJSON_IsNumber(cJSON_GetArrayItem(iv, 0)) ||
            !cJSON_IsNumber(cJSON_GetArrayItem(iv, 1))) {
            return BAD(SCENE_BAD, "`interval_s` is [min, max] in seconds");
        }
        m.interval_min_s = (float)cJSON_GetArrayItem(iv, 0)->valuedouble;
        m.interval_max_s = (float)cJSON_GetArrayItem(iv, 1)->valuedouble;
    }
    const cJSON *t = cJSON_GetObjectItem(body, "time_s");
    if (t != NULL) {
        if (!cJSON_IsNumber(t) || t->valuedouble <= 0) {
            return BAD(SCENE_BAD, "`time_s` is 0.1..3600");
        }
        m.time_s = (float)t->valuedouble;
    }
    return scene_holo_change(&m, dry, why, why_len);
}
