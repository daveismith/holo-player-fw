/*
 * board_api: the routes and events of the screen, LEDs, holo, scenes and settings. See
 * board_api.h.
 */
#include <errno.h>
#include <string.h>
#include "fs_ops.h"
#include "board_api.h"

#define REL_MAX (FS_PATH_MAX + 8)

bool api_fs_path(const char *rel, char *abs, size_t abs_len, api_reply_t *err)
{
    char why[112];
    if (fs_path(rel, abs, abs_len, why, sizeof(why)) != 0) {
        *err = api_error(400, "bad_path", "%s", why);
        return false;
    }
    return true;
}

api_reply_t api_fs_errno(int err, const char *abs)
{
    char rel[REL_MAX];
    fs_rel(abs, rel, sizeof(rel));
    switch (err) {
    case ENOENT:  return api_error(404, "not_found", "no such file or directory: %s", rel);
    case ENOTDIR: return api_error(400, "not_a_directory", "%s is not a directory", rel);
    case EISDIR:  return api_error(400, "is_a_directory", "%s is a directory", rel);
    default:      return api_error(500, "failed", "%s: %s", rel, strerror(err));
    }
}

esp_err_t board_api_start(void)
{
    esp_err_t err = ESP_OK;
    err |= api_add_routes(API_SCREEN_ROUTES, API_SCREEN_ROUTES_N);
    err |= api_add_routes(API_LEDS_ROUTES, API_LEDS_ROUTES_N);
    err |= api_add_routes(API_HOLO_ROUTES, API_HOLO_ROUTES_N);
    err |= api_add_routes(API_SCENES_ROUTES, API_SCENES_ROUTES_N);
    err |= api_events_start();
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}
