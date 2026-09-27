/*
 * The application's own API routes, beside the kit's (web_ota, web_fs, ...). Private to webui.
 */
#pragma once

#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"

/* /api/v1/screen and /api/v1/media */
esp_err_t api_screen_register(void);

/* The screen as the API describes it (ScreenState). */
cJSON *api_screen_json(void);

/*
 * Show what `show` asks -- {path, loop?, whole_frame?}, {colour}, {calibration: true}, or, for a
 * scene (`part` not NULL), {clear: true} -- checked first. False after sending the error, whose
 * message starts with `part`: ("screen").
 */
bool api_screen_show(httpd_req_t *req, const cJSON *show, const char *part);
