/*
 * The screen over the API: /api/v1/screen and /api/v1/media, over the video component. See the
 * OpenAPI description (manual/reference/openapi.json).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "fs_ops.h"
#include "scenes.h"
#include "video_player.h"
#include "board_api.h"

#define REL_MAX (FS_PATH_MAX + 8)

/* ------------------------------------------------------------------ JSON */

static const char *const SHOWING[] = { "nothing", "colour", "calibration", "image", "clip" };
static const char *const KINDS[] = { "clip", "image", "animation" };
static const char *const FORMATS[] = { "mov", "png", "jpeg", "gif" };

static void add_rel(cJSON *o, const char *key, const char *abs)
{
    char rel[REL_MAX];
    fs_rel(abs, rel, sizeof(rel));
    cJSON_AddStringToObject(o, key, rel);
}

static void add_colour(cJSON *o, const char *key, const uint8_t rgb[3])
{
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
    cJSON_AddStringToObject(o, key, hex);
}

cJSON *api_screen_json(void)
{
    screen_state_t st;
    screen_get_state(&st);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "powered", st.powered);
    cJSON_AddNumberToObject(o, "backlight", st.backlight);
    cJSON_AddStringToObject(o, "showing", SHOWING[st.showing]);
    if (st.showing == SCREEN_COLOUR) {
        add_colour(o, "colour", st.rgb);
    }
    if (st.showing == SCREEN_IMAGE || st.showing == SCREEN_CLIP) {
        add_rel(o, "path", st.path);
    }
    if (st.showing == SCREEN_CLIP) {
        cJSON_AddBoolToObject(o, "loop", st.loop);
        cJSON *c = cJSON_AddObjectToObject(o, "clip");
        cJSON_AddNumberToObject(c, "width", st.width);
        cJSON_AddNumberToObject(c, "height", st.height);
        cJSON_AddNumberToObject(c, "fps", st.fps);
        cJSON_AddNumberToObject(c, "frames", st.frames);
        cJSON_AddNumberToObject(c, "shown", st.shown);
        cJSON_AddNumberToObject(c, "loops", st.loops);
        cJSON_AddNumberToObject(c, "plays", st.plays);
        cJSON_AddNumberToObject(c, "late", st.late);
        cJSON_AddNumberToObject(c, "elapsed_ms", (double)st.elapsed_ms);
    }
    return o;
}

/* ------------------------------------------------------------------ /screen */

static api_reply_t screen_get(const api_req_t *req)
{
    (void)req;
    return api_json(200, api_screen_json());
}

static api_reply_t screen_patch(const api_req_t *req)
{
    const cJSON *body = req->body;
    const cJSON *bl = cJSON_GetObjectItem(body, "backlight");
    const bool only = body->child != NULL && body->child->next == NULL && bl != NULL;
    if (!(only && cJSON_IsNumber(bl) && bl->valuedouble >= 0 && bl->valuedouble <= 100)) {
        return api_error(400, "bad_request", "send {\"backlight\": 0-100}");
    }
    screen_set_backlight((int)bl->valuedouble);
    return api_json(200, api_screen_json());
}

static api_reply_t screen_delete(const api_req_t *req)
{
    (void)req;
    screen_clear();
    return api_json(200, api_screen_json());
}

static api_reply_t show_post(const api_req_t *req)
{
    char why[128];
    const scene_err_t err = cJSON_GetObjectItem(req->body, "clear") != NULL
                                ? (snprintf(why, sizeof(why), "DELETE /api/v1/screen shows nothing"), SCENE_BAD)
                                : scene_do_screen(req->body, false, false, why, sizeof(why));
    return err == SCENE_OK ? api_json(200, api_screen_json()) : api_scene_error(err, why);
}

/* ------------------------------------------------------------------ /media */

static int kind_by_name(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return -1;
    }
    if (strcasecmp(dot, ".mov") == 0) {
        return MEDIA_CLIP;
    }
    if (strcasecmp(dot, ".gif") == 0) {
        return MEDIA_ANIMATION;
    }
    if (strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) {
        return MEDIA_IMAGE;
    }
    return -1;
}

typedef struct {
    fs_entry_t *items;
    size_t n, cap;
} entries_t;

static bool gather(const fs_entry_t *e, void *ctx)
{
    entries_t *l = ctx;
    if (l->n == l->cap) {
        const size_t cap = l->cap ? 2 * l->cap : 16;
        fs_entry_t *items = realloc(l->items, cap * sizeof(*items));
        if (items == NULL) {
            return false;
        }
        l->items = items;
        l->cap = cap;
    }
    l->items[l->n++] = *e;
    return true;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(((const fs_entry_t *)a)->name, ((const fs_entry_t *)b)->name);
}

/* Every clip and image under `abs`, depth first, by name. `depth` guards a pathological tree. */
static int walk(const char *abs, cJSON *items, int depth)
{
    entries_t l = { 0 };
    int err = fs_list(abs, gather, &l);
    if (err == 0) {
        qsort(l.items, l.n, sizeof(*l.items), by_name);
    }
    char child[FS_ABS_MAX + FS_NAME_MAX + 1];
    for (size_t i = 0; err == 0 && i < l.n; i++) {
        const fs_entry_t *e = &l.items[i];
        if (snprintf(child, sizeof(child), "%s/%s", abs, e->name) >= FS_ABS_MAX) {
            continue;
        }
        if (e->dir) {
            if (depth < 8) {
                walk(child, items, depth + 1);
            }
            continue;
        }
        const int kind = kind_by_name(e->name);
        if (kind < 0) {
            continue;
        }
        cJSON *o = cJSON_CreateObject();
        add_rel(o, "path", child);
        cJSON_AddStringToObject(o, "kind", KINDS[kind]);
        cJSON_AddNumberToObject(o, "size", (double)e->size);
        cJSON_AddItemToArray(items, o);
    }
    free(l.items);
    return err;
}

static api_reply_t media_get(const api_req_t *req)
{
    char rel[256];
    if (!api_query(req, "path", rel, sizeof(rel)) || rel[0] == '\0') {
        strlcpy(rel, "/", sizeof(rel));
    }
    char abs[FS_ABS_MAX];
    api_reply_t bad;
    if (!api_fs_path(rel, abs, sizeof(abs), &bad)) {
        return bad;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_AddArrayToObject(root, "items");
    const int err = walk(abs, items, 0);
    if (err != 0) {
        cJSON_Delete(root);
        return api_fs_errno(err, abs);
    }
    return api_json(200, root);
}

static api_reply_t media_info_get(const api_req_t *req)
{
    char rel[256];
    api_query(req, "path", rel, sizeof(rel));
    char abs[FS_ABS_MAX];
    if (rel[0] == '\0') {
        return api_error(400, "bad_request", "give `path`, from the root of the volume: /clips/intro.mov");
    }
    api_reply_t bad;
    if (!api_fs_path(rel, abs, sizeof(abs), &bad)) {
        return bad;
    }
    fs_rel(abs, rel, sizeof(rel));
    media_info_t m;
    const esp_err_t err = media_probe(abs, &m);
    if (err == ESP_ERR_NOT_FOUND) {
        return api_error(404, "not_found", "no such file or directory: %s", rel);
    }
    if (err != ESP_OK) {
        return api_error(422, "not_playable", "%s: %s", rel, m.why);
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "path", rel);
    cJSON_AddStringToObject(o, "kind", KINDS[m.kind]);
    cJSON_AddStringToObject(o, "format", FORMATS[m.format]);
    cJSON_AddNumberToObject(o, "width", m.width);
    cJSON_AddNumberToObject(o, "height", m.height);
    if (m.kind != MEDIA_IMAGE) {
        cJSON_AddNumberToObject(o, "frames", m.frames);
        cJSON_AddNumberToObject(o, "fps", m.fps);
        cJSON_AddNumberToObject(o, "duration_s", m.duration_s);
    }
    if (m.kind == MEDIA_CLIP && m.codec[0]) {
        cJSON_AddStringToObject(o, "codec", m.codec);
    }
    cJSON_AddBoolToObject(o, "playable", m.playable);
    if (!m.playable) {
        cJSON_AddStringToObject(o, "why", m.why);
    }
    return api_json(200, o);
}

/* ------------------------------------------------------------------ */

const api_route_t API_SCREEN_ROUTES[] = {
    API_ROUTE(API_GET, "/api/v1/screen", screen_get, 0, API_LINK),
    API_ROUTE(API_PATCH, "/api/v1/screen", screen_patch, 256, API_LINK),
    API_ROUTE(API_DELETE, "/api/v1/screen", screen_delete, 0, API_LINK),
    API_ROUTE(API_POST, "/api/v1/screen/show", show_post, 512, API_LINK),
    API_ROUTE(API_GET, "/api/v1/media", media_get, 0, API_LINK),
    API_ROUTE(API_GET, "/api/v1/media/info", media_info_get, 0, API_LINK),
};
const size_t API_SCREEN_ROUTES_N = sizeof(API_SCREEN_ROUTES) / sizeof(API_SCREEN_ROUTES[0]);
