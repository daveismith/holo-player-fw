/*
 * The holoprojector over the API: /api/v1/holo, for this board's one holo (the holo engine's
 * first). The servos behind it are the kit's (web_servo).
 */
#include <math.h>
#include <string.h>
#include "holo.h"
#include "scenes.h"
#include "board_api.h"

cJSON *api_holo_json(void)
{
    holo_status_t st;
    cJSON *o = cJSON_CreateObject();
    if (holo_status(0, &st) != ESP_OK) {
        cJSON_AddBoolToObject(o, "ready", false);
        cJSON_AddStringToObject(o, "why", "not fitted");
        cJSON_AddStringToObject(o, "motion", "hold");
        cJSON *p = cJSON_AddObjectToObject(o, "position");
        cJSON_AddNumberToObject(p, "x", 0);
        cJSON_AddNumberToObject(p, "y", 0);
        return o;
    }
    cJSON_AddBoolToObject(o, "ready", st.ready);
    if (st.why != NULL) {
        cJSON_AddStringToObject(o, "why", st.why);
    }
    cJSON_AddStringToObject(o, "motion", st.motion);
    if (st.stopped != NULL) {
        cJSON_AddStringToObject(o, "stopped", st.stopped);
    }
    cJSON *p = cJSON_AddObjectToObject(o, "position");
    cJSON_AddNumberToObject(p, "x", round(st.x * 100.0) / 100.0);
    cJSON_AddNumberToObject(p, "y", round(st.y * 100.0) / 100.0);
    return o;
}

static api_reply_t holo_get(const api_req_t *req)
{
    (void)req;
    return api_json(200, api_holo_json());
}

static api_reply_t motion_post(const api_req_t *req)
{
    char why[128];
    const scene_err_t err = scene_do_holo(req->body, false, why, sizeof(why));
    return err == SCENE_OK ? api_json(202, api_holo_json()) : api_scene_error(err, why);
}

void board_api_stop_holo(void)
{
    const holo_motion_t stop = { .kind = HOLO_STOP };
    char why[64];
    holo_motion(0, &stop, why, sizeof(why));
}

const api_route_t API_HOLO_ROUTES[] = {
    API_ROUTE(API_GET, "/api/v1/holo", holo_get, 0, API_LINK),
    API_ROUTE(API_POST, "/api/v1/holo/motion", motion_post, 512, API_LINK),
};
const size_t API_HOLO_ROUTES_N = sizeof(API_HOLO_ROUTES) / sizeof(API_HOLO_ROUTES[0]);
