/*
 * The holoprojector over the API: /api/v1/holo, for this board's one holo (the holo engine's
 * first), and the servos behind it (the kit's web_servo).
 */
#include <math.h>
#include <string.h>
#include "holo.h"
#include "scenes.h"
#include "web_server.h"
#include "web_servo.h"
#include "api.h"

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

static esp_err_t holo_get(httpd_req_t *req)
{
    return web_send_json(req, 200, api_holo_json());
}

static esp_err_t motion_post(httpd_req_t *req)
{
    cJSON *body = web_read_json(req, 512);
    if (body == NULL) {
        return ESP_OK;
    }
    char why[128];
    const scene_err_t err = scene_do_holo(body, false, why, sizeof(why));
    cJSON_Delete(body);
    return err == SCENE_OK ? web_send_json(req, 202, api_holo_json()) : api_send_scene_error(req, err, why);
}

/* A servo driven directly: the holo's own motion would fight it, so it stops. */
static void stop_holo(void)
{
    const holo_motion_t stop = { .kind = HOLO_STOP };
    char why[64];
    holo_motion(0, &stop, why, sizeof(why));
}

esp_err_t api_holo_register(void)
{
    web_server_add_feature("holo");
    esp_err_t err = ESP_OK;
    err |= web_register("/api/v1/holo", HTTP_GET, holo_get, 0);
    err |= web_register("/api/v1/holo/motion", HTTP_POST, motion_post, WEB_AUTH);
    err |= web_servo_register(stop_holo);
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}
