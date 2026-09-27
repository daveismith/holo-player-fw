/*
 * Scenes and settings over the API: /api/v1/scenes and /api/v1/settings, over the scenes
 * component.
 */
#include <stdio.h>
#include <string.h>
#include "leds.h"
#include "scenes.h"
#include "video_player.h"
#include "web_server.h"
#include "api.h"

esp_err_t api_send_scene_error(httpd_req_t *req, int err, const char *why)
{
    switch ((scene_err_t)err) {
    case SCENE_BAD:        return web_send_error(req, 400, "bad_request", "%s", why);
    case SCENE_BAD_PATH:   return web_send_error(req, 400, "bad_path", "%s", why);
    case SCENE_MISSING:    return web_send_error(req, 404, "not_found", "%s", why);
    case SCENE_UNPLAYABLE: return web_send_error(req, 422, "not_playable", "%s", why);
    case SCENE_NOT_READY:  return web_send_error(req, 409, "not_ready", "%s", why);
    case SCENE_FULL:       return web_send_error(req, 409, "full", "%s", why);
    default:               return web_send_error(req, 500, "failed", "%s", why);
    }
}

/* The query's scene name: false after replying. */
static bool query_name(httpd_req_t *req, char *name, size_t len)
{
    if (!web_query(req, "name", name, len) || !scene_name_ok(name)) {
        web_send_error(req, 400, "bad_request", "give `name`: 1-%d letters, digits, spaces, _ . and -", SCENE_NAME_MAX);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ /scenes */

static cJSON *active_json(void)
{
    scene_active_t a;
    if (!scene_active(&a)) {
        return cJSON_CreateNull();
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", a.name);
    cJSON_AddStringToObject(o, "then", SCENE_THEN_NAMES[a.then]);
    cJSON_AddBoolToObject(o, "until_clip_ends", a.clip);
    if (a.remaining_s >= 0) {
        cJSON_AddNumberToObject(o, "remaining_s", (double)(int)(a.remaining_s * 10 + 0.5) / 10);
    } else {
        cJSON_AddNullToObject(o, "remaining_s");
    }
    return o;
}

static esp_err_t scenes_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "scenes", scene_load_all());
    cJSON_AddItemToObject(root, "active", active_json());
    return web_send_json(req, 200, root);
}

static cJSON *now_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "screen", api_screen_json());
    cJSON_AddItemToObject(root, "leds", api_leds_json());
    cJSON_AddItemToObject(root, "holo", api_holo_json());
    return root;
}

static esp_err_t end_post(httpd_req_t *req)
{
    cJSON *body = web_read_json(req, 64);
    if (body == NULL) {
        return ESP_OK;
    }
    const bool empty = body->child == NULL;
    cJSON_Delete(body);
    if (!empty) {
        return web_send_error(req, 400, "bad_request", "send {}");
    }
    char why[96];
    if (scene_end(why, sizeof(why)) != SCENE_OK) {
        return web_send_error(req, 409, "not_running", "%s", why);
    }
    return web_send_json(req, 200, now_json());
}

static esp_err_t scene_put(httpd_req_t *req)
{
    char name[SCENE_NAME_MAX + 8];
    if (!query_name(req, name, sizeof(name))) {
        return ESP_OK;
    }
    cJSON *body = web_read_json(req, SCENE_JSON_MAX + 512);
    if (body == NULL) {
        return ESP_OK;
    }
    const cJSON *given = cJSON_GetObjectItem(body, "name");
    if (given != NULL && !(cJSON_IsString(given) && strcmp(given->valuestring, name) == 0)) {
        cJSON_Delete(body);
        return web_send_error(req, 400, "bad_request", "the body's `name` must be the query's, '%s'", name);
    }
    if (given == NULL) {
        /* First, where a reader looks for it */
        cJSON *item = cJSON_CreateString(name);
        cJSON_AddItemToObject(body, "name", item);
        cJSON_DetachItemViaPointer(body, item);
        cJSON_InsertItemInArray(body, 0, item);
    }
    bool replaced = false;
    char why[128];
    const scene_err_t err = scene_store(body, &replaced, why, sizeof(why));
    if (err != SCENE_OK) {
        cJSON_Delete(body);
        return api_send_scene_error(req, err, why);
    }
    return web_send_json(req, replaced ? 200 : 201, body);
}

static esp_err_t scene_delete(httpd_req_t *req)
{
    char name[SCENE_NAME_MAX + 8];
    if (!query_name(req, name, sizeof(name))) {
        return ESP_OK;
    }
    if (scene_remove(name) != SCENE_OK) {
        return web_send_error(req, 404, "unknown_scene", "no scene '%s'", name);
    }
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t apply_post(httpd_req_t *req)
{
    cJSON *body = web_read_json(req, SCENE_JSON_MAX + 512);
    if (body == NULL) {
        return ESP_OK;
    }
    const cJSON *name = cJSON_GetObjectItem(body, "name");
    const cJSON *given = cJSON_GetObjectItem(body, "scene");
    if (body->child == NULL || body->child->next != NULL || (name == NULL && given == NULL)) {
        cJSON_Delete(body);
        return web_send_error(req, 400, "bad_request", "send {\"name\": ...} or {\"scene\": {...}}");
    }
    cJSON *scene = NULL;
    if (name != NULL) {
        scene = cJSON_IsString(name) ? scene_load(name->valuestring) : NULL;
        if (scene == NULL) {
            const esp_err_t sent = web_send_error(req, 404, "unknown_scene", "no scene '%s'",
                                                  cJSON_IsString(name) ? name->valuestring : "?");
            cJSON_Delete(body);
            return sent;
        }
    } else {
        scene = cJSON_Duplicate(given, true);
        if (cJSON_IsObject(scene) && cJSON_GetObjectItem(scene, "name") == NULL) {
            cJSON_AddStringToObject(scene, "name", "now");     /* a scene given whole needs no name */
        }
    }
    cJSON_Delete(body);
    char why[128];
    const scene_err_t err = scene_apply(scene, why, sizeof(why));
    cJSON_Delete(scene);
    if (err != SCENE_OK) {
        return api_send_scene_error(req, err, why);
    }
    return web_send_json(req, 200, now_json());
}

/* ------------------------------------------------------------------ /settings */

static cJSON *settings_json(const settings_t *s)
{
    cJSON *o = cJSON_CreateObject();
    if (s->boot_scene[0] != '\0') {
        cJSON_AddStringToObject(o, "boot_scene", s->boot_scene);
    } else {
        cJSON_AddNullToObject(o, "boot_scene");
    }
    cJSON *screen = cJSON_AddObjectToObject(o, "screen");
    cJSON_AddNumberToObject(screen, "backlight", s->backlight);
    cJSON *leds = cJSON_AddObjectToObject(o, "leds");
    cJSON_AddNumberToObject(leds, "brightness", s->led_brightness);
    cJSON_AddNumberToObject(leds, "count", s->led_count);
    return o;
}

static esp_err_t send_settings_result(httpd_req_t *req, const settings_t *s)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "settings", settings_json(s));
    cJSON_AddBoolToObject(root, "restart_required", s->led_count != leds_count());
    return web_send_json(req, 200, root);
}

static esp_err_t settings_get_route(httpd_req_t *req)
{
    settings_t s;
    settings_get(&s);
    return web_send_json(req, 200, settings_json(&s));
}

/* A number within lo..hi at o[key], if there: false when there and not. */
static bool in_range(const cJSON *o, const char *key, int lo, int hi, int *out)
{
    const cJSON *v = cJSON_GetObjectItem(o, key);
    if (v == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(v) || v->valuedouble < lo || v->valuedouble > hi || v->valuedouble != (int)v->valuedouble) {
        return false;
    }
    *out = (int)v->valuedouble;
    return true;
}

static bool only(const cJSON *o, const char *a, const char *b, const char *c)
{
    for (const cJSON *k = o->child; k != NULL; k = k->next) {
        if (strcmp(k->string, a) && (b == NULL || strcmp(k->string, b)) && (c == NULL || strcmp(k->string, c))) {
            return false;
        }
    }
    return true;
}

static esp_err_t settings_patch(httpd_req_t *req)
{
    cJSON *body = web_read_json(req, 512);
    if (body == NULL) {
        return ESP_OK;
    }
    settings_t s;
    settings_get(&s);
    const cJSON *boot = cJSON_GetObjectItem(body, "boot_scene");
    const cJSON *screen = cJSON_GetObjectItem(body, "screen");
    const cJSON *leds = cJSON_GetObjectItem(body, "leds");
    const char *bad = NULL;
    if (body->child == NULL || !only(body, "boot_scene", "screen", "leds")) {
        bad = "send boot_scene, screen {backlight} or leds {brightness, count}";
    } else if (screen && !(cJSON_IsObject(screen) && only(screen, "backlight", NULL, NULL) &&
                           in_range(screen, "backlight", 1, 100, &s.backlight))) {
        bad = "`screen` is {\"backlight\": 1-100}";
    } else if (leds && !(cJSON_IsObject(leds) && only(leds, "brightness", "count", NULL) &&
                         in_range(leds, "brightness", 1, 100, &s.led_brightness) &&
                         in_range(leds, "count", 1, 256, &s.led_count))) {
        bad = "`leds` is {\"brightness\": 1-100, \"count\": 1-256}";
    } else if (boot && !cJSON_IsNull(boot) && !(cJSON_IsString(boot) && scene_name_ok(boot->valuestring))) {
        bad = "`boot_scene` is a scene's name, or null";
    }
    if (bad != NULL) {
        cJSON_Delete(body);
        return web_send_error(req, 400, "bad_request", "%s", bad);
    }
    if (boot != NULL) {
        if (cJSON_IsNull(boot)) {
            s.boot_scene[0] = '\0';
        } else {
            cJSON *scene = scene_load(boot->valuestring);
            if (scene == NULL) {
                const esp_err_t sent = web_send_error(req, 404, "unknown_scene", "no scene '%s'", boot->valuestring);
                cJSON_Delete(body);
                return sent;
            }
            cJSON_Delete(scene);
            strlcpy(s.boot_scene, boot->valuestring, sizeof(s.boot_scene));
        }
    }
    cJSON_Delete(body);
    const esp_err_t err = settings_save(&s);
    if (err != ESP_OK) {
        return web_send_error(req, 500, "failed", "not saved: %s", esp_err_to_name(err));
    }
    screen_set_backlight(s.backlight);
    leds_set_brightness(s.led_brightness);
    return send_settings_result(req, &s);
}

static esp_err_t settings_delete(httpd_req_t *req)
{
    settings_t s;
    settings_defaults(&s);
    const esp_err_t err = settings_save(&s);
    if (err != ESP_OK) {
        return web_send_error(req, 500, "failed", "not saved: %s", esp_err_to_name(err));
    }
    screen_set_backlight(s.backlight);
    leds_set_brightness(s.led_brightness);
    return send_settings_result(req, &s);
}

esp_err_t api_scenes_register(void)
{
    web_server_add_feature("scenes");
    web_server_add_feature("settings");
    esp_err_t err = ESP_OK;
    err |= web_register("/api/v1/scenes", HTTP_GET, scenes_get, 0);
    err |= web_register("/api/v1/scenes/scene", HTTP_PUT, scene_put, WEB_AUTH);
    err |= web_register("/api/v1/scenes/scene", HTTP_DELETE, scene_delete, WEB_AUTH);
    err |= web_register("/api/v1/scenes/apply", HTTP_POST, apply_post, WEB_AUTH);
    err |= web_register("/api/v1/scenes/end", HTTP_POST, end_post, WEB_AUTH);
    err |= web_register("/api/v1/settings", HTTP_GET, settings_get_route, 0);
    err |= web_register("/api/v1/settings", HTTP_PATCH, settings_patch, WEB_AUTH);
    err |= web_register("/api/v1/settings", HTTP_DELETE, settings_delete, WEB_AUTH);
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}
