/*
 * The LED strip over the API: /api/v1/leds, over the leds component.
 */
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "leds.h"
#include "web_server.h"
#include "api.h"

static const char *const MODES[] = { "off", "solid", "wipe", "rainbow" };

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

#define FAIL(...) (web_send_error(__VA_ARGS__), false)

bool api_leds_apply(httpd_req_t *req, const cJSON *patch, const char *part)
{
    const char *prefix = part != NULL ? part : "";
    const char *sep = part != NULL ? ": " : "";
    for (const cJSON *k = patch->child; k != NULL; k = k->next) {
        if (strcmp(k->string, "mode") && strcmp(k->string, "colour") && strcmp(k->string, "loop") &&
            strcmp(k->string, "brightness")) {
            return FAIL(req, 400, "bad_request", "%s%sunknown field `%s`", prefix, sep, k->string);
        }
    }
    if (patch->child == NULL) {
        return FAIL(req, 400, "bad_request", "%s%ssend mode, colour, loop or brightness", prefix, sep);
    }
    const cJSON *mode = cJSON_GetObjectItem(patch, "mode");
    const cJSON *colour = cJSON_GetObjectItem(patch, "colour");
    const cJSON *loop = cJSON_GetObjectItem(patch, "loop");
    const cJSON *bright = cJSON_GetObjectItem(patch, "brightness");

    uint8_t rgb[3];
    bool cur_loop = false;
    leds_mode_t m = leds_mode(rgb, &cur_loop);
    if (mode != NULL) {
        int found = -1;
        for (int i = 0; cJSON_IsString(mode) && i < 4; i++) {
            if (strcmp(mode->valuestring, MODES[i]) == 0) {
                found = i;
            }
        }
        if (found < 0) {
            return FAIL(req, 400, "bad_request", "%s%s`mode` is off, solid, wipe or rainbow", prefix, sep);
        }
        m = (leds_mode_t)found;
    }
    if (colour != NULL && !(cJSON_IsString(colour) && board_parse_rgb(colour->valuestring, rgb))) {
        return FAIL(req, 400, "bad_request", "%s%snot a colour: '%s' (#rrggbb, R,G,B, or a name: red, orange, ...)",
                    prefix, sep, cJSON_IsString(colour) ? colour->valuestring : "?");
    }
    if (loop != NULL && !cJSON_IsBool(loop)) {
        return FAIL(req, 400, "bad_request", "%s%s`loop` is true or false", prefix, sep);
    }
    if (bright != NULL && !(cJSON_IsNumber(bright) && bright->valuedouble >= 1 && bright->valuedouble <= 100)) {
        return FAIL(req, 400, "bad_request", "%s%s`brightness` is 1..100", prefix, sep);
    }

    if (bright != NULL) {
        leds_set_brightness((int)bright->valuedouble);
    }
    if (mode == NULL && colour == NULL && loop == NULL) {
        return true;        /* the brightness alone: what is running carries on */
    }
    if (colour == NULL && rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0) {
        rgb[0] = rgb[1] = rgb[2] = 255;     /* never given one: white, as `leds wipe` */
    }
    /* A new mode starts fresh: looping only if asked. A colour alone keeps the pattern as it is. */
    const bool lp = loop != NULL ? cJSON_IsTrue(loop) : mode == NULL && cur_loop;
    esp_err_t err;
    switch (m) {
    case LEDS_OFF:     err = leds_off(); break;
    case LEDS_SOLID:   err = leds_solid(rgb[0], rgb[1], rgb[2]); break;
    default:           err = leds_play(m, rgb[0], rgb[1], rgb[2], lp); break;
    }
    if (err != ESP_OK) {
        return FAIL(req, 500, "failed", "%s%sthe LEDs: %s", prefix, sep, esp_err_to_name(err));
    }
    return true;
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
    const bool ok = api_leds_apply(req, body, NULL);
    cJSON_Delete(body);
    return ok ? web_send_json(req, 200, api_leds_json()) : ESP_OK;
}

esp_err_t api_leds_register(void)
{
    web_server_add_feature("leds");
    esp_err_t err = ESP_OK;
    err |= web_register("/api/v1/leds", HTTP_GET, leds_get, 0);
    err |= web_register("/api/v1/leds", HTTP_PATCH, leds_patch, WEB_AUTH);
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}
