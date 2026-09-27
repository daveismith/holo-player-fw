/*
 * The screen over the API: /api/v1/screen and /api/v1/media, over the video component. See the
 * OpenAPI description (manual/reference/openapi.json).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "board.h"
#include "video_player.h"
#include "web_fs.h"
#include "web_server.h"
#include "api.h"

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
        cJSON_AddNumberToObject(c, "late", st.late);
        cJSON_AddNumberToObject(c, "elapsed_ms", (double)st.elapsed_ms);
    }
    return o;
}

/* ------------------------------------------------------------------ showing */

/* Send an error; false, for returning from api_screen_show(). */
#define FAIL(...) (web_send_error(__VA_ARGS__), false)

bool api_screen_show(httpd_req_t *req, const cJSON *show, const char *part)
{
    const char *prefix = part != NULL ? part : "";
    const char *sep = part != NULL ? ": " : "";
    int keys = 0;
    for (const cJSON *k = show->child; k != NULL; k = k->next) {
        if (strcmp(k->string, "path") && strcmp(k->string, "loop") && strcmp(k->string, "whole_frame") &&
            strcmp(k->string, "colour") && strcmp(k->string, "calibration") && strcmp(k->string, "clear")) {
            return FAIL(req, 400, "bad_request", "%s%sunknown field `%s`", prefix, sep, k->string);
        }
        keys += !strcmp(k->string, "path") + !strcmp(k->string, "colour") + !strcmp(k->string, "calibration") +
                !strcmp(k->string, "clear");
    }
    if (keys != 1) {
        return FAIL(req, 400, "bad_request", "%s%sgive one of `path`, `colour` or `calibration`%s", prefix,
                    sep, part != NULL ? " (or `clear`)" : "");
    }
    const cJSON *path = cJSON_GetObjectItem(show, "path");
    const cJSON *colour = cJSON_GetObjectItem(show, "colour");
    esp_err_t err;
    if (cJSON_IsTrue(cJSON_GetObjectItem(show, "calibration"))) {
        err = screen_show_calibration();
    } else if (cJSON_IsTrue(cJSON_GetObjectItem(show, "clear")) && part != NULL) {
        err = screen_clear();
    } else if (cJSON_IsString(colour)) {
        uint8_t rgb[3];
        if (!board_parse_rgb(colour->valuestring, rgb)) {
            return FAIL(req, 400, "bad_request", "%s%snot a colour: '%s' (#rrggbb, R,G,B, or a name: red, "
                        "orange, ...)", prefix, sep, colour->valuestring);
        }
        err = screen_show_rgb(rgb);
    } else if (cJSON_IsString(path)) {
        char abs[FS_ABS_MAX];
        if (!web_fs_resolve(req, path->valuestring, abs, sizeof(abs))) {
            return false;
        }
        char why[96] = "";
        err = screen_show_file(abs, cJSON_IsTrue(cJSON_GetObjectItem(show, "loop")),
                               cJSON_IsTrue(cJSON_GetObjectItem(show, "whole_frame")), why, sizeof(why));
        char rel[REL_MAX];
        fs_rel(abs, rel, sizeof(rel));
        if (err == ESP_ERR_NOT_FOUND) {
            return FAIL(req, part != NULL ? 422 : 404, part != NULL ? "not_playable" : "not_found",
                        "%s%sno such file: %s", prefix, sep, rel);
        }
        if (err == ESP_ERR_INVALID_ARG) {
            return FAIL(req, 422, "not_playable", "%s%s%s: %s", prefix, sep, rel, why);
        }
        if (err != ESP_OK) {
            return FAIL(req, 500, "failed", "%s%s%s: %s", prefix, sep, rel, why);
        }
        return true;
    } else {
        return FAIL(req, 400, "bad_request", "%s%s`path` and `colour` are strings; `calibration` is true",
                    prefix, sep);
    }
    if (err != ESP_OK) {
        return FAIL(req, 500, "failed", "%s%sthe screen: %s", prefix, sep, esp_err_to_name(err));
    }
    return true;
}

/* ------------------------------------------------------------------ /screen */

static esp_err_t screen_get(httpd_req_t *req)
{
    return web_send_json(req, 200, api_screen_json());
}

static esp_err_t screen_patch(httpd_req_t *req)
{
    cJSON *body = web_read_json(req, 256);
    if (body == NULL) {
        return ESP_OK;
    }
    const cJSON *bl = cJSON_GetObjectItem(body, "backlight");
    const bool only = body->child != NULL && body->child->next == NULL && bl != NULL;
    const bool ok = only && cJSON_IsNumber(bl) && bl->valuedouble >= 0 && bl->valuedouble <= 100;
    const int pct = ok ? (int)bl->valuedouble : 0;
    cJSON_Delete(body);
    if (!ok) {
        return web_send_error(req, 400, "bad_request", "send {\"backlight\": 0-100}");
    }
    screen_set_backlight(pct);
    return web_send_json(req, 200, api_screen_json());
}

static esp_err_t screen_delete(httpd_req_t *req)
{
    screen_clear();
    return web_send_json(req, 200, api_screen_json());
}

static esp_err_t show_post(httpd_req_t *req)
{
    cJSON *body = web_read_json(req, 512);
    if (body == NULL) {
        return ESP_OK;
    }
    bool shown = false;
    if (cJSON_GetObjectItem(body, "clear") != NULL) {
        web_send_error(req, 400, "bad_request", "DELETE /api/v1/screen shows nothing");
    } else {
        shown = api_screen_show(req, body, NULL);
    }
    cJSON_Delete(body);
    return shown ? web_send_json(req, 200, api_screen_json()) : ESP_OK;     /* else the error is sent */
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

static esp_err_t media_get(httpd_req_t *req)
{
    char rel[256];
    if (!web_query(req, "path", rel, sizeof(rel)) || rel[0] == '\0') {
        strlcpy(rel, "/", sizeof(rel));
    }
    char abs[FS_ABS_MAX];
    if (!web_fs_resolve(req, rel, abs, sizeof(abs))) {
        return ESP_OK;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_AddArrayToObject(root, "items");
    const int err = walk(abs, items, 0);
    if (err != 0) {
        cJSON_Delete(root);
        return web_fs_send_errno(req, err, abs);
    }
    return web_send_json(req, 200, root);
}

static esp_err_t media_info_get(httpd_req_t *req)
{
    char rel[256];
    web_query(req, "path", rel, sizeof(rel));
    char abs[FS_ABS_MAX];
    if (rel[0] == '\0') {
        return web_send_error(req, 400, "bad_request", "give `path`, from the root of the volume: /clips/intro.mov");
    }
    if (!web_fs_resolve(req, rel, abs, sizeof(abs))) {
        return ESP_OK;
    }
    fs_rel(abs, rel, sizeof(rel));
    media_info_t m;
    const esp_err_t err = media_probe(abs, &m);
    if (err == ESP_ERR_NOT_FOUND) {
        return web_send_error(req, 404, "not_found", "no such file or directory: %s", rel);
    }
    if (err != ESP_OK) {
        return web_send_error(req, 422, "not_playable", "%s: %s", rel, m.why);
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
    return web_send_json(req, 200, o);
}

/* ------------------------------------------------------------------ */

esp_err_t api_screen_register(void)
{
    web_server_add_feature("screen");
    esp_err_t err = ESP_OK;
    err |= web_register("/api/v1/screen", HTTP_GET, screen_get, 0);
    err |= web_register("/api/v1/screen", HTTP_PATCH, screen_patch, WEB_AUTH);
    err |= web_register("/api/v1/screen", HTTP_DELETE, screen_delete, WEB_AUTH);
    err |= web_register("/api/v1/screen/show", HTTP_POST, show_post, WEB_AUTH);
    err |= web_register("/api/v1/media", HTTP_GET, media_get, 0);
    err |= web_register("/api/v1/media/info", HTTP_GET, media_info_get, 0);
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}
