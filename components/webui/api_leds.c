/*
 * The LED strip over the API: /api/v1/leds, over the leds component.
 */
#include <stdio.h>
#include <string.h>
#include "leds.h"
#include "scenes.h"
#include "web_server.h"
#include "api.h"

static const char *const MODES[] = { "off", "solid", "wipe", "rainbow", "flicker" };

cJSON *api_leds_json(void)
{
    uint8_t rgb[3];
    bool loop = false;
    const leds_mode_t mode = leds_mode(rgb, &loop);
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "mode", MODES[mode]);
    cJSON_AddStringToObject(o, "colour", hex);
    cJSON_AddBoolToObject(o, "loop", loop);
    cJSON_AddNumberToObject(o, "brightness", leds_get_brightness());
    cJSON_AddNumberToObject(o, "count", leds_count());
    cJSON_AddNumberToObject(o, "gpio", CONFIG_LEDS_GPIO);
    return o;
}

static esp_err_t leds_get(httpd_req_t *req)
{
    return web_send_json(req, 200, api_leds_json());
}

static esp_err_t leds_patch(httpd_req_t *req)
{
    cJSON *body = web_read_json(req, 256);
    if (body == NULL) {
        return ESP_OK;
    }
    char why[128];
    const scene_err_t err = scene_do_leds(body, false, why, sizeof(why));
    cJSON_Delete(body);
    return err == SCENE_OK ? web_send_json(req, 200, api_leds_json()) : api_send_scene_error(req, err, why);
}

esp_err_t api_leds_register(void)
{
    web_server_add_feature("leds");
    esp_err_t err = ESP_OK;
    err |= web_register("/api/v1/leds", HTTP_GET, leds_get, 0);
    err |= web_register("/api/v1/leds", HTTP_PATCH, leds_patch, WEB_AUTH);
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}
