/*
 * The scene running: one applied with a `then` other than stay is watched until it ends -- its
 * clip played out, or its time up -- and then the screen, LEDs and holo go back to what they
 * were doing before it (restore), or all off. See scenes.h.
 *
 * A task of its own does the ending: the player tells of a clip's end from its own task, which
 * may not call back into the player, and putting things back can mean decoding an image.
 *
 * Events (components `events`): `clip_ended` for every clip that stops, `scene_ended` for a
 * watched scene that ends -- its clip done, its time up, ended, or replaced -- and `scene` (the
 * state: which one is watched) whenever that changes.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "events.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fs_ops.h"
#include "holo.h"
#include "leds.h"
#include "video_player.h"
#include "scenes.h"

static const char *TAG = "scene";

#define STACK 10240     /* restoring an image decodes it */

const char *const SCENE_THEN_NAMES[] = { "stay", "restore", "off" };

scene_err_t scene_apply_parts(const cJSON *scene, char *why, size_t why_len);   /* scenes.c */

/* What the board was doing before a scene: enough to put it back. */
typedef struct {
    screen_state_t screen;
    leds_mode_t led_mode;
    uint8_t led_rgb[3];
    bool led_loop;
    int led_brightness;
    bool holo_ok;
    holo_status_t holo;
    holo_motion_t holo_last;
} snapshot_t;

typedef struct {
    char path[160];
    bool finished;
} clip_end_t;

static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_apply;      /* one scene_apply() or scene_end() at a time: the console and the API */
static QueueHandle_t s_queue;
static struct {
    bool on;
    char name[SCENE_NAME_MAX + 1];
    scene_then_t then;
    char clip[FS_ABS_MAX];      /* ends when this finishes; "" for none */
    int64_t until_us;           /* or at this time; 0 for none */
    snapshot_t snap;
} s_run;

int scene_then_of(const cJSON *then)
{
    for (int i = 0; cJSON_IsString(then) && i < 3; i++) {
        if (strcmp(then->valuestring, SCENE_THEN_NAMES[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static void take_snapshot(snapshot_t *s)
{
    memset(s, 0, sizeof(*s));
    screen_get_state(&s->screen);
    s->led_mode = leds_mode(s->led_rgb, &s->led_loop);
    s->led_brightness = leds_get_brightness();
    s->holo_ok = holo_status(0, &s->holo) == ESP_OK && holo_last_motion(0, &s->holo_last);
}

static void restore(const snapshot_t *s)
{
    char why[96];
    const screen_state_t *sc = &s->screen;
    screen_set_backlight(sc->backlight);
    switch (sc->showing) {
    case SCREEN_COLOUR:      screen_show_rgb(sc->rgb); break;
    case SCREEN_CALIBRATION: screen_show_calibration(); break;
    case SCREEN_IMAGE:       screen_show_file(sc->path, -1, false, why, sizeof(why)); break;
    case SCREEN_CLIP:
        /* A clip playing forever plays again; one that was on its way to its end, does not */
        if (sc->plays == 0) {
            screen_show_file(sc->path, 0, false, why, sizeof(why));
        } else {
            screen_clear();
        }
        break;
    default:                 screen_clear(); break;
    }

    leds_set_brightness(s->led_brightness);
    const uint8_t *c = s->led_rgb;
    switch (s->led_mode) {
    case LEDS_SOLID:   leds_solid(c[0], c[1], c[2]); break;
    case LEDS_FLICKER: leds_play(LEDS_FLICKER, c[0], c[1], c[2], false); break;
    case LEDS_WIPE:
    case LEDS_RAINBOW:
        if (s->led_loop) {
            leds_play(s->led_mode, c[0], c[1], c[2], true);
        } else {
            leds_off();
        }
        break;
    default:           leds_off(); break;
    }

    if (!s->holo_ok) {
        return;
    }
    /* A behaviour that ran until stopped runs again; limp stays limp; else back where it was */
    holo_motion_t m = { .kind = HOLO_STOP, .duration_ms = -1 };
    if (strcmp(s->holo.motion, "twitch") == 0 || strcmp(s->holo.motion, "scan") == 0) {
        m = s->holo_last;
    } else if (s->holo_last.kind == HOLO_OFF) {
        m.kind = HOLO_OFF;
    } else if (s->holo.placed) {
        m.kind = HOLO_MOVE;
        m.x = (int)lroundf(s->holo.x * 100);
        m.y = (int)lroundf(s->holo.y * 100);
    }
    holo_motion(0, &m, why, sizeof(why));
}

static void all_off(void)
{
    char why[64];
    screen_clear();
    leds_off();
    const holo_motion_t off = { .kind = HOLO_OFF };
    holo_motion(0, &off, why, sizeof(why));
}

static void finish(scene_then_t then, const snapshot_t *snap, const char *name)
{
    ESP_LOGI(TAG, "'%s' ended: %s", name, SCENE_THEN_NAMES[then]);
    if (then == SCENE_THEN_RESTORE) {
        restore(snap);
    } else if (then == SCENE_THEN_OFF) {
        all_off();
    }
}

/* A watched scene is over: `by` is clip, time, end or replaced (when its `then` was not done). */
static void publish_end(const char *name, scene_then_t then, const char *by)
{
    cJSON *f = cJSON_CreateObject();
    cJSON_AddStringToObject(f, "name", name);
    cJSON_AddStringToObject(f, "then", SCENE_THEN_NAMES[then]);
    cJSON_AddStringToObject(f, "by", by);
    events_happened("scene_ended", f);
    events_changed("scene");
}

static void runner_task(void *arg)
{
    (void)arg;
    static snapshot_t snap;     /* off the stack */
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_run.on && s_run.until_us != 0) {
            const int64_t left = s_run.until_us - esp_timer_get_time();
            wait = left <= 0 ? 0 : pdMS_TO_TICKS(left / 1000) + 1;
        }
        xSemaphoreGive(s_lock);

        clip_end_t ev;
        const bool got = xQueueReceive(s_queue, &ev, wait) == pdTRUE;
        char name[SCENE_NAME_MAX + 1];
        scene_then_t then = SCENE_THEN_STAY;
        bool end = false;
        const char *by = NULL;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_run.on) {
            if (got && s_run.clip[0] != '\0' && strcmp(ev.path, s_run.clip) == 0) {
                if (ev.finished) {
                    end = true;
                    by = "clip";
                } else {
                    s_run.on = false;       /* something else took the screen: it is over, as it is */
                    by = "replaced";
                    ESP_LOGI(TAG, "'%s' replaced", s_run.name);
                }
            }
            if (by == NULL && s_run.until_us != 0 && esp_timer_get_time() >= s_run.until_us) {
                end = true;
                by = "time";
            }
            if (by != NULL) {
                s_run.on = false;
                then = s_run.then;
                snap = s_run.snap;
                strlcpy(name, s_run.name, sizeof(name));
            }
        }
        xSemaphoreGive(s_lock);
        if (end) {
            finish(then, &snap, name);
        }
        if (by != NULL) {
            publish_end(name, then, by);
        }
    }
}

static void on_clip_end(const char *path, bool finished, void *ctx)
{
    (void)ctx;
    char rel[FS_ABS_MAX];
    fs_rel(path, rel, sizeof(rel));     /* from the root of the volume, as the API names files */
    cJSON *f = cJSON_CreateObject();
    cJSON_AddStringToObject(f, "path", rel);
    cJSON_AddBoolToObject(f, "finished", finished);
    events_happened("clip_ended", f);
    clip_end_t ev = { .finished = finished };
    strlcpy(ev.path, path, sizeof(ev.path));
    xQueueSend(s_queue, &ev, 0);
}

esp_err_t scenes_start(void)
{
    if (s_queue != NULL) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    s_apply = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(8, sizeof(clip_end_t));
    if (s_lock == NULL || s_apply == NULL || s_queue == NULL ||
        xTaskCreate(runner_task, "scene", STACK, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    events_declare("clip_ended", NULL);
    events_declare("scene_ended", NULL);
    screen_set_end_hook(on_clip_end, NULL);
    return ESP_OK;
}

static scene_err_t apply(const cJSON *scene, char *why, size_t why_len);

scene_err_t scene_apply(const cJSON *scene, char *why, size_t why_len)
{
    xSemaphoreTake(s_apply, portMAX_DELAY);
    const scene_err_t err = apply(scene, why, why_len);
    xSemaphoreGive(s_apply);
    return err;
}

static scene_err_t apply(const cJSON *scene, char *why, size_t why_len)
{
    scene_err_t err = scene_check(scene, why, why_len);
    if (err != SCENE_OK) {
        return err;
    }
    const int then_i = scene_then_of(cJSON_GetObjectItem(scene, "then"));
    const scene_then_t then = then_i < 0 ? SCENE_THEN_STAY : (scene_then_t)then_i;
    const cJSON *dur = cJSON_GetObjectItem(scene, "duration_s");
    const cJSON *screen = cJSON_GetObjectItem(scene, "screen");
    const cJSON *path = cJSON_IsObject(screen) ? cJSON_GetObjectItem(screen, "path") : NULL;
    char clip[FS_ABS_MAX] = "";
    cJSON *once = NULL;             /* the scene, with its clip made to play once, when that is needed */
    if (then != SCENE_THEN_STAY && cJSON_IsString(path) && !cJSON_IsTrue(cJSON_GetObjectItem(screen, "loop"))) {
        char reason[112];
        media_info_t m;
        if (fs_path(path->valuestring, clip, sizeof(clip), reason, sizeof(reason)) == 0 &&
            media_probe(clip, &m) == ESP_OK && m.kind == MEDIA_IMAGE) {
            clip[0] = '\0';         /* a still: it never ends by itself */
            if (dur == NULL) {
                snprintf(why, why_len, "screen: %s is a still, which never ends: give `duration_s`", path->valuestring);
                return SCENE_BAD;
            }
        }
        /* With neither `loop` nor `loops`, a clip plays once -- an animated GIF too, rather than as
         * its file says, which may be forever: the scene has to end */
        if (clip[0] != '\0' && cJSON_GetObjectItem(screen, "loops") == NULL) {
            once = cJSON_Duplicate(scene, true);
            cJSON *s2 = cJSON_GetObjectItem(once, "screen");
            cJSON_DeleteItemFromObject(s2, "loop");
            cJSON_AddNumberToObject(s2, "loops", 1);
            scene = once;
        }
    }

    /* What it goes back to: before this scene -- or, if it follows one that goes back too, before that */
    static snapshot_t snap;
    char before[SCENE_NAME_MAX + 1] = "";
    scene_then_t before_then = SCENE_THEN_STAY;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool inherit = s_run.on && s_run.then == SCENE_THEN_RESTORE && then == SCENE_THEN_RESTORE;
    if (inherit) {
        snap = s_run.snap;
    }
    if (s_run.on) {
        strlcpy(before, s_run.name, sizeof(before));
        before_then = s_run.then;
    }
    s_run.on = false;               /* any scene before this one is over */
    xSemaphoreGive(s_lock);
    if (before[0] != '\0') {
        publish_end(before, before_then, "replaced");
    }
    if (then == SCENE_THEN_RESTORE && !inherit) {
        take_snapshot(&snap);
    }

    err = scene_apply_parts(scene, why, why_len);
    if (err != SCENE_OK || then == SCENE_THEN_STAY) {
        cJSON_Delete(once);
        return err;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    xQueueReset(s_queue);           /* the ends of what this scene replaced */
    s_run.on = true;
    strlcpy(s_run.name, cJSON_GetObjectItem(scene, "name")->valuestring, sizeof(s_run.name));
    s_run.then = then;
    strlcpy(s_run.clip, clip, sizeof(s_run.clip));
    s_run.until_us = dur != NULL ? esp_timer_get_time() + (int64_t)(dur->valuedouble * 1e6) : 0;
    s_run.snap = snap;
    xSemaphoreGive(s_lock);
    cJSON_Delete(once);
    const clip_end_t kick = { .path = "" };
    xQueueSend(s_queue, &kick, 0);  /* the runner looks at the new time */
    events_changed("scene");
    return SCENE_OK;
}

bool scene_active(scene_active_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool on = s_run.on;
    if (on) {
        strlcpy(out->name, s_run.name, sizeof(out->name));
        out->then = s_run.then;
        out->clip = s_run.clip[0] != '\0';
        out->remaining_s = s_run.until_us != 0 ? (double)(s_run.until_us - esp_timer_get_time()) / 1e6 : -1;
        if (out->remaining_s < -0.5 && s_run.until_us != 0) {
            out->remaining_s = 0;
        }
    }
    xSemaphoreGive(s_lock);
    return on;
}

scene_err_t scene_end(char *why, size_t why_len)
{
    static snapshot_t snap;
    char name[SCENE_NAME_MAX + 1];
    xSemaphoreTake(s_apply, portMAX_DELAY);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool on = s_run.on;
    const scene_then_t then = s_run.then;
    if (on) {
        s_run.on = false;
        snap = s_run.snap;
        strlcpy(name, s_run.name, sizeof(name));
    }
    xSemaphoreGive(s_lock);
    if (on) {
        finish(then, &snap, name);
        publish_end(name, then, "end");
    } else {
        snprintf(why, why_len, "no scene is running to its end");
    }
    xSemaphoreGive(s_apply);
    return on ? SCENE_OK : SCENE_MISSING;
}
