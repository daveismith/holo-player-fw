/*
 * The LED strip over the API: /api/v1/leds, over the leds component.
 */
#include <stdio.h>
#include <string.h>
#include "leds.h"
#include "scenes.h"
#include "board_api.h"

/* A setting's fields into o: mode, colour, loop, period_ms, level */
static void add_setting(cJSON *o, const leds_setting_t *s)
{
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02x%02x%02x", s->rgb[0], s->rgb[1], s->rgb[2]);
    cJSON_AddStringToObject(o, "mode", leds_mode_name(s->mode));
    cJSON_AddStringToObject(o, "colour", hex);
    cJSON_AddBoolToObject(o, "loop", s->loop);
    cJSON_AddNumberToObject(o, "period_ms", s->period_ms);
    cJSON_AddNumberToObject(o, "level", s->level);
}

cJSON *api_leds_json(void)
{
    leds_state_t st;
    leds_state(&st);
    cJSON *o = cJSON_CreateObject();
    add_setting(o, &st.now);
    cJSON_AddNumberToObject(o, "brightness", st.brightness);
    cJSON_AddNumberToObject(o, "count", leds_count());
    cJSON_AddNumberToObject(o, "gpio", CONFIG_LEDS_GPIO);
    if (st.until_us != 0) {
        cJSON *t = cJSON_AddObjectToObject(o, "timed");
        cJSON_AddNumberToObject(t, "remaining_s", (double)((leds_remaining_ms(&st) + 50) / 100) / 10);
        add_setting(cJSON_AddObjectToObject(t, "then"), &st.after);
    } else {
        cJSON_AddNullToObject(o, "timed");
    }
    cJSON_AddBoolToObject(o, "leia", st.leia);
    return o;
}

static api_reply_t leds_get(const api_req_t *req)
{
    (void)req;
    return api_json(200, api_leds_json());
}

static api_reply_t leds_patch(const api_req_t *req)
{
    char why[128];
    const scene_err_t err = scene_do_leds(req->body, false, why, sizeof(why));
    return err == SCENE_OK ? api_json(200, api_leds_json()) : api_scene_error(err, why);
}

const api_route_t API_LEDS_ROUTES[] = {
    API_ROUTE(API_GET, "/api/v1/leds", leds_get, 0, API_LINK),
    API_ROUTE(API_PATCH, "/api/v1/leds", leds_patch, 256, API_LINK),
};
const size_t API_LEDS_ROUTES_N = sizeof(API_LEDS_ROUTES) / sizeof(API_LEDS_ROUTES[0]);
