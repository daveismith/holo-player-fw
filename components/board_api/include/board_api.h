/*
 * board_api -- the screen, LEDs, holo, scenes and settings as API routes (the kit's api_core):
 * what /api/v1/screen, /leds, /holo, /scenes and /settings do, with nothing HTTP in it. The web
 * server serves them (webui), and the host link the ones marked API_LINK. Also the resources as
 * events (the kit's `events`). See the OpenAPI description (manual/reference/openapi.json).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "api_codes.h"
#include "api_core.h"
#include "cJSON.h"
#include "esp_err.h"
#include "scenes.h"
#include "video_player.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Add the routes, declare the resources as events, and start watching what changes by itself. */
esp_err_t board_api_start(void);

/* A servo driven directly: the holo's own motion would fight it, so it stops. */
void board_api_stop_holo(void);

/* The resources, as their GETs return them. */
cJSON *api_screen_json(void);
cJSON *api_leds_json(void);
cJSON *api_holo_json(void);
cJSON *api_active_scene_json(void);     /* SceneList.active: the scene watched, or null */
cJSON *api_settings_json(const settings_t *s);

/* ---- for the routes ---- */

/* A scene_err_t as the API's error: 400 bad_request or bad_path, 404, 422, 409, 500. */
api_reply_t api_scene_error(int err, const char *why);
/* The same as the host link's and Cyphal's code, without the reply: api_code_of() of it, OK for SCENE_OK */
api_code_t api_scene_code(int err);

/* `rel`, from the volume's root, as an absolute path: false with 400 bad_path in `err`. */
bool api_fs_path(const char *rel, char *abs, size_t abs_len, api_reply_t *err);

/* An fs_ops errno as the API's error for `abs`: 404 not_found, 400 not_a_directory, ... */
api_reply_t api_fs_errno(int err, const char *abs);

/* What GET /api/v1/media/info finds, for the route and for the Cyphal node's r2.holo.MediaInfo alike, so that both
 * check a path and word the outcome the same: `rel` (a path from the volume's root, as given; "" for none) checked
 * as the HTTP API checks a path, then probed (media_probe()). Its HTTP status: 200 with *m filled, playable or not
 * (when not, `why` is "<path>: <m->why>", as a 422's message); or 400 (`error` bad_request or bad_path), 404
 * not_found, 422 not_playable (not a clip or an image at all; *m as media_probe() left it), with the message in
 * `why`. `error` is NULL with 200. `out` is the path as the volume has it, from 404 on */
int api_media_lookup(const char *rel, char *out, size_t out_len, media_info_t *m, const char **error, char *why,
                     size_t why_len);

/* The route tables, each file's (board_api_start() adds them) */
extern const api_route_t API_SCREEN_ROUTES[];
extern const size_t API_SCREEN_ROUTES_N;
extern const api_route_t API_LEDS_ROUTES[];
extern const size_t API_LEDS_ROUTES_N;
extern const api_route_t API_HOLO_ROUTES[];
extern const size_t API_HOLO_ROUTES_N;
extern const api_route_t API_SCENES_ROUTES[];
extern const size_t API_SCENES_ROUTES_N;

esp_err_t api_events_start(void);

#ifdef __cplusplus
}
#endif
