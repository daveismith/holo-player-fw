/*
 * The holoprojector over the API: /api/v1/holo, for this board's one holo (the holo engine's
 * first), and the servos behind it (the kit's web_servo).
 */
#include <math.h>
#include <string.h>
#include "holo.h"
#include "web_server.h"
#include "web_servo.h"
#include "api.h"

static const struct { const char *name; holo_motion_kind_t kind; } MOTIONS[] = {
    { "center", HOLO_CENTER }, { "move", HOLO_MOVE }, { "nudge", HOLO_NUDGE }, { "twitch", HOLO_TWITCH },
    { "wag", HOLO_WAG }, { "nod", HOLO_NOD }, { "scan", HOLO_SCAN }, { "circle", HOLO_CIRCLE },
    { "stop", HOLO_STOP }, { "off", HOLO_OFF },
};

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

#define FAIL(...) (web_send_error(__VA_ARGS__), false)

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

bool api_holo_apply(httpd_req_t *req, const cJSON *body, const char *part)
{
    const char *prefix = part != NULL ? part : "";
    const char *sep = part != NULL ? ": " : "";
    static const char *const KEYS[] = { "motion", "x", "y", "duration_ms", "range", "count", "period_ms",
                                        "interval_s", "time_s" };
    for (const cJSON *k = body->child; k != NULL; k = k->next) {
        bool ok = false;
        for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]) && !ok; i++) {
            ok = strcmp(k->string, KEYS[i]) == 0;
        }
        if (!ok) {
            return FAIL(req, 400, "bad_request", "%s%sunknown field `%s`", prefix, sep, k->string);
        }
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
        return FAIL(req, 400, "bad_request", "%s%s`motion` is center, move, nudge, twitch, wag, nod, scan, circle, "
                    "stop or off", prefix, sep);
    }
    if (!get_int(body, "x", &m.x) || !get_int(body, "y", &m.y) || !get_int(body, "duration_ms", &m.duration_ms) ||
        !get_int(body, "range", &m.range) || !get_int(body, "count", &m.count) ||
        !get_int(body, "period_ms", &m.period_ms)) {
        return FAIL(req, 400, "bad_request", "%s%s`x`, `y`, `duration_ms`, `range`, `count` and `period_ms` are whole "
                    "numbers", prefix, sep);
    }
    if ((m.kind == HOLO_MOVE || m.kind == HOLO_NUDGE) &&
        (!cJSON_GetObjectItem(body, "x") || !cJSON_GetObjectItem(body, "y"))) {
        return FAIL(req, 400, "bad_request", "%s%s%s takes `x` and `y`", prefix, sep, motion->valuestring);
    }
    if (cJSON_GetObjectItem(body, "range") && m.range < 1) {
        return FAIL(req, 400, "bad_request", "%s%s`range` is 1..100", prefix, sep);
    }
    if (cJSON_GetObjectItem(body, "count") && m.count < 1) {
        return FAIL(req, 400, "bad_request", "%s%s`count` is 1..20", prefix, sep);
    }
    if (cJSON_GetObjectItem(body, "duration_ms") && m.duration_ms < 0) {
        return FAIL(req, 400, "bad_request", "%s%s`duration_ms` is 0..10000", prefix, sep);
    }
    const cJSON *iv = cJSON_GetObjectItem(body, "interval_s");
    if (iv != NULL) {
        if (!cJSON_IsArray(iv) || cJSON_GetArraySize(iv) != 2 || !cJSON_IsNumber(cJSON_GetArrayItem(iv, 0)) ||
            !cJSON_IsNumber(cJSON_GetArrayItem(iv, 1))) {
            return FAIL(req, 400, "bad_request", "%s%s`interval_s` is [min, max] in seconds", prefix, sep);
        }
        m.interval_min_s = (float)cJSON_GetArrayItem(iv, 0)->valuedouble;
        m.interval_max_s = (float)cJSON_GetArrayItem(iv, 1)->valuedouble;
    }
    const cJSON *t = cJSON_GetObjectItem(body, "time_s");
    if (t != NULL) {
        if (!cJSON_IsNumber(t) || t->valuedouble <= 0) {
            return FAIL(req, 400, "bad_request", "%s%s`time_s` is 0.1..3600", prefix, sep);
        }
        m.time_s = (float)t->valuedouble;
    }
    char why[96];
    const esp_err_t err = holo_motion(0, &m, why, sizeof(why));
    if (err == ESP_ERR_INVALID_ARG) {
        return FAIL(req, 400, "bad_request", "%s%s%s", prefix, sep, why);
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return FAIL(req, 409, "not_ready", "%s%s%s", prefix, sep, why);
    }
    if (err != ESP_OK) {
        return FAIL(req, 409, "not_ready", "%s%sthe holo: %s", prefix, sep, why);
    }
    return true;
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
    const bool ok = api_holo_apply(req, body, NULL);
    cJSON_Delete(body);
    return ok ? web_send_json(req, 202, api_holo_json()) : ESP_OK;
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
