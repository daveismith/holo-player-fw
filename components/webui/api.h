/*
 * The application's own API routes, beside the kit's (web_ota, web_fs, ...). Private to webui.
 */
#pragma once

#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "scenes.h"

/* /api/v1/screen and /api/v1/media */
esp_err_t api_screen_register(void);

/* The screen as the API describes it (ScreenState). */
cJSON *api_screen_json(void);

/* A scene_err_t as the API's error: 400 bad_request or bad_path, 404, 422, 409, 500. */
esp_err_t api_send_scene_error(httpd_req_t *req, int err, const char *why);

/* /api/v1/leds */
esp_err_t api_leds_register(void);
cJSON *api_leds_json(void);

/* /api/v1/holo, and /api/v1/servos behind it */
esp_err_t api_holo_register(void);
cJSON *api_holo_json(void);

/* /api/v1/scenes and /api/v1/settings */
esp_err_t api_scenes_register(void);
cJSON *api_active_scene_json(void);     /* SceneList.active: the scene watched, or null */
cJSON *api_settings_json(const settings_t *s);

/* The screen, LEDs, holo, scenes and settings as events (the kit's `events`) */
esp_err_t api_events_start(void);
