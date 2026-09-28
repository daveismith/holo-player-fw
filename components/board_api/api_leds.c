/*
 * The LED strip over the API: /api/v1/leds, over the leds component.
 */
#include <stdio.h>
#include <string.h>
#include "leds.h"
#include "scenes.h"
#include "board_api.h"

static const char *const MODES[] = { "off", "solid", "wipe", "rainbow", "flicker" };

cJSON *api_leds_json(void)
{
    uint8_t rgb[3];
    bool loop = false;
    const leds_mode_t mode = leds_mode(rgb, &loop);
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "mode", MODES[mode]);
    cJSON_AddStringToObject(o, "colour", hex);
    cJSON_AddBoolToObject(o, "loop", loop);
    cJSON_AddNumberToObject(o, "brightness", leds_get_brightness());
    cJSON_AddNumberToObject(o, "count", leds_count());
    cJSON_AddNumberToObject(o, "gpio", CONFIG_LEDS_GPIO);
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
