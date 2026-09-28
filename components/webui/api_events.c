/*
 * The board's resources as events (the kit's `events`, streamed at /api/v1/events): each state
 * kind is its GET's JSON, built when it is sent. The modules say when they change -- the screen,
 * the LEDs, scenes and settings -- but the holo moves by itself, so it is looked at here, four
 * times a second while anyone is following it.
 */
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "events.h"
#include "scenes.h"
#include "api.h"

static const char *TAG = "api_events";

#define HOLO_EVERY_US (250 * 1000)

static cJSON *scenes_json(void)
{
    cJSON *list = scene_load_all();
    return list != NULL ? list : cJSON_CreateArray();
}

static cJSON *settings_now_json(void)
{
    settings_t s;
    settings_get(&s);
    return api_settings_json(&s);
}

/* The holo's JSON, against what it was: a move, or a motion starting or ending */
static void watch_holo(void *arg)
{
    (void)arg;
    static char *last;
    if (!events_wanted("holo")) {
        free(last);
        last = NULL;
        return;
    }
    cJSON *o = api_holo_json();
    char *now = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (now == NULL) {
        return;
    }
    if (last == NULL || strcmp(now, last) != 0) {
        if (last != NULL) {
            events_changed("holo");     /* not on the first look: nothing changed yet */
        }
        free(last);
        last = now;
    } else {
        cJSON_free(now);
    }
}

esp_err_t api_events_start(void)
{
    esp_err_t err = ESP_OK;
    err |= events_declare("screen", api_screen_json);
    err |= events_declare("leds", api_leds_json);
    err |= events_declare("holo", api_holo_json);
    err |= events_declare("scene", api_active_scene_json);
    err |= events_declare("scenes", scenes_json);
    err |= events_declare("settings", settings_now_json);
    esp_timer_handle_t t;
    const esp_timer_create_args_t args = { .callback = watch_holo, .name = "holo_events" };
    if (esp_timer_create(&args, &t) != ESP_OK || esp_timer_start_periodic(t, HOLO_EVERY_US) != ESP_OK) {
        ESP_LOGW(TAG, "the holo won't be followed");
    }
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}
