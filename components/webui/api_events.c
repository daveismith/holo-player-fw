/*
 * The board's resources as events (the kit's `events`, streamed at /api/v1/events): each state
 * kind is its GET's JSON, built when it is sent. The modules say when they change -- the screen,
 * the LEDs, scenes and settings -- but the holo moves by itself, so it is looked at here, four
 * times a second while anyone is following it. And while a clip plays or a scene runs to its end,
 * their states go out every CONFIG_WEBUI_EVENTS_SYNC_S besides, so a client counting on between
 * events (frames, time left) is put right again.
 */
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "events.h"
#include "scenes.h"
#include "sdkconfig.h"
#include "video_player.h"
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

/* A clip playing, a scene running to its end: their states again, for clients to sync to */
static void sync_running(void *arg)
{
    (void)arg;
    if (video_playing() && events_wanted("screen")) {
        events_changed("screen");
    }
    scene_active_t a;
    if (events_wanted("scene") && scene_active(&a)) {
        events_changed("scene");
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
    const esp_timer_create_args_t sync = { .callback = sync_running, .name = "sync_events" };
    if (esp_timer_create(&sync, &t) != ESP_OK ||
        esp_timer_start_periodic(t, (uint64_t)CONFIG_WEBUI_EVENTS_SYNC_S * 1000 * 1000) != ESP_OK) {
        ESP_LOGW(TAG, "a playing clip and a running scene won't be sent again");
    }
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}
