/*
 * webui -- Holo Player's web app: its pages, the OTA API, and release channels from the
 * documentation site. See webui.c.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the web server with the app's pages and the OTA routes. After wifi_known_start(). */
esp_err_t webui_start(void);

#ifdef __cplusplus
}
#endif
