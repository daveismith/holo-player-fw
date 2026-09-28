/*
 * Scenes and settings over the API: /api/v1/scenes and /api/v1/settings, over the scenes
 * component.
 */
#include <stdio.h>
#include <string.h>
#include "leds.h"
#include "scenes.h"
#include "video_player.h"
#include "board_api.h"

api_reply_t api_scene_error(int err, const char *why)
{
    switch ((scene_err_t)err) {
    case SCENE_BAD:        return api_error(400, "bad_request", "%s", why);
    case SCENE_BAD_PATH:   return api_error(400, "bad_path", "%s", why);
    case SCENE_MISSING:    return api_error(404, "not_found", "%s", why);
    case SCENE_UNPLAYABLE: return api_error(422, "not_playable", "%s", why);
    case SCENE_NOT_READY:  return api_error(409, "not_ready", "%s", why);
    case SCENE_FULL:       return api_error(409, "full", "%s", why);
    case SCENE_TAKEN:      return api_error(409, "slot_taken", "%s", why);
    default:               return api_error(500, "failed", "%s", why);
    }
}

/* The query's scene name: false with the error in `err`. */
static bool query_name(const api_req_t *req, char *name, size_t len, api_reply_t *err)
{
    if (!api_query(req, "name", name, len) || !scene_name_ok(name)) {
        *err = api_error(400, "bad_request", "give `name`: 1-%d letters, digits, spaces, _ . and -", SCENE_NAME_MAX);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ /scenes */

cJSON *api_active_scene_json(void)
{
    scene_active_t a;
    if (!scene_active(&a)) {
        return cJSON_CreateNull();
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", a.name);
    if (a.slot > 0) {
        cJSON_AddNumberToObject(o, "slot", a.slot);
    }
    cJSON_AddStringToObject(o, "then", SCENE_THEN_NAMES[a.then]);
    cJSON_AddBoolToObject(o, "until_clip_ends", a.clip);
    if (a.remaining_s >= 0) {
        cJSON_AddNumberToObject(o, "remaining_s", (double)(int)(a.remaining_s * 10 + 0.5) / 10);
    } else {
        cJSON_AddNullToObject(o, "remaining_s");
    }
    return o;
}

static api_reply_t scenes_get(const api_req_t *req)
{
    (void)req;
    cJSON *root = cJSON_CreateObject();
    cJSON *all = scene_load_all();
    cJSON_AddItemToObject(root, "scenes", all != NULL ? all : cJSON_CreateArray());
    cJSON_AddItemToObject(root, "active", api_active_scene_json());
    return api_json(200, root);
}

static cJSON *now_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "screen", api_screen_json());
    cJSON_AddItemToObject(root, "leds", api_leds_json());
    cJSON_AddItemToObject(root, "holo", api_holo_json());
    return root;
}

static api_reply_t end_post(const api_req_t *req)
{
    if (req->body->child != NULL) {
        return api_error(400, "bad_request", "send {}");
    }
    char why[96];
    if (scene_end(why, sizeof(why)) != SCENE_OK) {
        return api_error(409, "not_running", "%s", why);
    }
    return api_json(200, now_json());
}

static api_reply_t scene_put(const api_req_t *req)
{
    char name[SCENE_NAME_MAX + 8];
    api_reply_t bad;
    if (!query_name(req, name, sizeof(name), &bad)) {
        return bad;
    }
    const cJSON *given = cJSON_GetObjectItem(req->body, "name");
    if (given != NULL && !(cJSON_IsString(given) && strcmp(given->valuestring, name) == 0)) {
        return api_error(400, "bad_request", "the body's `name` must be the query's, '%s'", name);
    }
    cJSON *body = cJSON_Duplicate(req->body, true);
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
        return api_scene_error(err, why);
    }
    return api_json(replaced ? 200 : 201, body);
}

static api_reply_t scene_delete(const api_req_t *req)
{
    char name[SCENE_NAME_MAX + 8];
    api_reply_t bad;
    if (!query_name(req, name, sizeof(name), &bad)) {
        return bad;
    }
    if (scene_remove(name) != SCENE_OK) {
        return api_error(404, "unknown_scene", "no scene '%s'", name);
    }
    return api_no_content();
}

static api_reply_t apply_post(const api_req_t *req)
{
    const cJSON *body = req->body;
    const cJSON *name = cJSON_GetObjectItem(body, "name");
    const cJSON *given = cJSON_GetObjectItem(body, "scene");
    if (body->child == NULL || body->child->next != NULL || (name == NULL && given == NULL)) {
        return api_error(400, "bad_request", "send {\"name\": ...} or {\"scene\": {...}}");
    }
    cJSON *scene = NULL;
    if (name != NULL) {
        scene = cJSON_IsString(name) ? scene_load(name->valuestring) : NULL;
        if (scene == NULL) {
            return api_error(404, "unknown_scene", "no scene '%s'", cJSON_IsString(name) ? name->valuestring : "?");
        }
    } else {
        scene = cJSON_Duplicate(given, true);
        if (cJSON_IsObject(scene) && cJSON_GetObjectItem(scene, "name") == NULL) {
            cJSON_AddStringToObject(scene, "name", "now");     /* a scene given whole needs no name */
        }
    }
    char why[128];
    const scene_err_t err = scene_apply(scene, why, sizeof(why));
    cJSON_Delete(scene);
    if (err != SCENE_OK) {
        return api_scene_error(err, why);
    }
    return api_json(200, now_json());
}

/* ------------------------------------------------------------------ /settings */

cJSON *api_settings_json(const settings_t *s)
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

static api_reply_t settings_result(const settings_t *s)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "settings", api_settings_json(s));
    cJSON_AddBoolToObject(root, "restart_required", s->led_count != leds_count());
    return api_json(200, root);
}

static api_reply_t settings_get_route(const api_req_t *req)
{
    (void)req;
    settings_t s;
    settings_get(&s);
    return api_json(200, api_settings_json(&s));
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

static api_reply_t settings_patch(const api_req_t *req)
{
    const cJSON *body = req->body;
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
        return api_error(400, "bad_request", "%s", bad);
    }
    if (boot != NULL) {
        if (cJSON_IsNull(boot)) {
            s.boot_scene[0] = '\0';
        } else {
            cJSON *scene = scene_load(boot->valuestring);
            if (scene == NULL) {
                return api_error(404, "unknown_scene", "no scene '%s'", boot->valuestring);
            }
            cJSON_Delete(scene);
            strlcpy(s.boot_scene, boot->valuestring, sizeof(s.boot_scene));
        }
    }
    const esp_err_t err = settings_save(&s);
    if (err != ESP_OK) {
        return api_error(500, "failed", "not saved: %s", esp_err_to_name(err));
    }
    screen_set_backlight(s.backlight);
    leds_set_brightness(s.led_brightness);
    return settings_result(&s);
}

static api_reply_t settings_delete(const api_req_t *req)
{
    (void)req;
    settings_t s;
    settings_defaults(&s);
    const esp_err_t err = settings_save(&s);
    if (err != ESP_OK) {
        return api_error(500, "failed", "not saved: %s", esp_err_to_name(err));
    }
    screen_set_backlight(s.backlight);
    leds_set_brightness(s.led_brightness);
    return settings_result(&s);
}

const api_route_t API_SCENES_ROUTES[] = {
    API_ROUTE(API_GET, "/api/v1/scenes", scenes_get, 0, API_LINK),
    API_ROUTE(API_PUT, "/api/v1/scenes/scene", scene_put, SCENE_JSON_MAX + 512, API_LINK),
    API_ROUTE(API_DELETE, "/api/v1/scenes/scene", scene_delete, 0, API_LINK),
    API_ROUTE(API_POST, "/api/v1/scenes/apply", apply_post, SCENE_JSON_MAX + 512, API_LINK),
    API_ROUTE(API_POST, "/api/v1/scenes/end", end_post, 64, API_LINK),
    API_ROUTE(API_GET, "/api/v1/settings", settings_get_route, 0, API_LINK),
    API_ROUTE(API_PATCH, "/api/v1/settings", settings_patch, 512, API_LINK),
    API_ROUTE(API_DELETE, "/api/v1/settings", settings_delete, 0, API_LINK),
};
const size_t API_SCENES_ROUTES_N = sizeof(API_SCENES_ROUTES) / sizeof(API_SCENES_ROUTES[0]);
