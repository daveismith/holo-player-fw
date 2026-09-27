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
    if (calib) {
        if (!dry && screen_show_calibration() != ESP_OK) {
            return BAD(SCENE_FAILED, "the panel did not take it");
        }
        return SCENE_OK;
    }
    if (clear) {
        if (!dry) {
            screen_clear();
        }
        return SCENE_OK;
    }
    if (colour) {
        uint8_t rgb[3];
        if (!cJSON_IsString(colour) || !board_parse_rgb(colour->valuestring, rgb)) {
            return BAD(SCENE_BAD, "not a colour: '%s' (#rrggbb, R,G,B, or a name: red, orange, ...)",
                       cJSON_IsString(colour) ? colour->valuestring : "?");
        }
        if (!dry && screen_show_rgb(rgb) != ESP_OK) {
            return BAD(SCENE_FAILED, "the panel did not take it");
        }
        return SCENE_OK;
    }
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
    if (loops && cJSON_IsTrue(loop)) {
        return BAD(SCENE_BAD, "give `loop` (forever) or `loops` (a number of times), not both");
    }
    /* Times through: forever, a number, or the default (a clip once, a GIF as the file says) */
    const int plays = cJSON_IsTrue(loop) ? 0 : loops ? (int)loops->valuedouble : -1;
    char abs[FS_ABS_MAX], rel[FS_PATH_MAX + 8];
    char reason[112];
    if (fs_path(path->valuestring, abs, sizeof(abs), reason, sizeof(reason)) != 0) {
        return BAD(SCENE_BAD_PATH, "%s", reason);
    }
    if (dry) {
        return SCENE_OK;
    }
    fs_rel(abs, rel, sizeof(rel));
    char msg[96] = "";
    const esp_err_t err = screen_show_file(abs, plays, cJSON_IsTrue(whole), msg, sizeof(msg));
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

/* ------------------------------------------------------------------ the LEDs */

static const char *const LED_MODES[] = { "off", "solid", "wipe", "rainbow", "flicker" };

scene_err_t scene_do_leds(const cJSON *patch, bool dry, char *why, size_t why_len)
{
    static const char *const KEYS[] = { "mode", "colour", "loop", "brightness" };
    const char *bad = NULL;
    if (!cJSON_IsObject(patch) || patch->child == NULL) {
        return BAD(SCENE_BAD, "send mode, colour, loop or brightness");
    }
    if (!only_keys(patch, KEYS, sizeof(KEYS) / sizeof(KEYS[0]), &bad)) {
        return BAD(SCENE_BAD, "unknown field `%s`", bad);
    }
    const cJSON *mode = cJSON_GetObjectItem(patch, "mode");
    const cJSON *colour = cJSON_GetObjectItem(patch, "colour");
    const cJSON *loop = cJSON_GetObjectItem(patch, "loop");
    const cJSON *bright = cJSON_GetObjectItem(patch, "brightness");

    uint8_t rgb[3];
    bool cur_loop = false;
    leds_mode_t m = leds_mode(rgb, &cur_loop);
    if (mode != NULL) {
        int found = -1;
        for (int i = 0; cJSON_IsString(mode) && i < 5; i++) {
            if (strcmp(mode->valuestring, LED_MODES[i]) == 0) {
                found = i;
            }
        }
        if (found < 0) {
            return BAD(SCENE_BAD, "`mode` is off, solid, wipe, rainbow or flicker");
        }
        m = (leds_mode_t)found;
    }
    if (colour != NULL && !(cJSON_IsString(colour) && board_parse_rgb(colour->valuestring, rgb))) {
        return BAD(SCENE_BAD, "not a colour: '%s' (#rrggbb, R,G,B, or a name: red, orange, ...)",
                   cJSON_IsString(colour) ? colour->valuestring : "?");
    }
    if (loop != NULL && !cJSON_IsBool(loop)) {
        return BAD(SCENE_BAD, "`loop` is true or false");
    }
    if (bright != NULL && !(cJSON_IsNumber(bright) && bright->valuedouble >= 1 && bright->valuedouble <= 100)) {
        return BAD(SCENE_BAD, "`brightness` is 1..100");
    }
    if (dry) {
        return SCENE_OK;
    }

    if (bright != NULL) {
        leds_set_brightness((int)bright->valuedouble);
    }
    if (mode == NULL && colour == NULL && loop == NULL) {
        return SCENE_OK;        /* the brightness alone: what is running carries on */
    }
    if (colour == NULL && rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0) {
        rgb[0] = rgb[1] = rgb[2] = 255;     /* never given one: white, as `leds wipe` */
    }
    /* A new mode starts fresh: looping only if asked. A colour alone keeps the pattern as it is. */
    const bool lp = loop != NULL ? cJSON_IsTrue(loop) : mode == NULL && cur_loop;
    esp_err_t err;
    switch (m) {
    case LEDS_OFF:     err = leds_off(); break;
    case LEDS_SOLID:   err = leds_solid(rgb[0], rgb[1], rgb[2]); break;
    default:           err = leds_play(m, rgb[0], rgb[1], rgb[2], lp); break;
    }
    if (err != ESP_OK) {
        return BAD(SCENE_FAILED, "%s", esp_err_to_name(err));
    }
    return SCENE_OK;
}

/* ------------------------------------------------------------------ the holo */

static const struct { const char *name; holo_motion_kind_t kind; } MOTIONS[] = {
    { "center", HOLO_CENTER }, { "move", HOLO_MOVE }, { "nudge", HOLO_NUDGE }, { "twitch", HOLO_TWITCH },
    { "wag", HOLO_WAG }, { "nod", HOLO_NOD }, { "scan", HOLO_SCAN }, { "circle", HOLO_CIRCLE },
    { "stop", HOLO_STOP }, { "off", HOLO_OFF },
};

/* A whole number, if present: false when present and not one. */
static bool get_int(const cJSON *o, const char *key, int *out)
{
    const cJSON *v = cJSON_GetObjectItem(o, key);
    if (v == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(v) || v->valuedouble != floor(v->valuedouble)) {
        return false;
    }
    *out = (int)v->valuedouble;
    return true;
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
        return BAD(SCENE_BAD, "`motion` is center, move, nudge, twitch, wag, nod, scan, circle, stop or off");
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
    if (!holo_motion_check(&m, why, why_len)) {
        return SCENE_BAD;
    }
    if (dry) {
        return SCENE_OK;
    }
    const esp_err_t err = holo_motion(0, &m, why, why_len);
    if (err == ESP_ERR_INVALID_ARG) {
        return SCENE_BAD;
    }
    if (err != ESP_OK) {
        return SCENE_NOT_READY;     /* refused, or no holo: `why` says */
    }
    return SCENE_OK;
}
