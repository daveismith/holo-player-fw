/*
 * Settings: what the board starts with, in NVS (namespace holo_cfg: boot, backlight,
 * led_bright, led_count, touch), and the `settings` console command. See scenes.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "esp_console.h"
#include "esp_log.h"
#include "events.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "leds.h"
#include "video_player.h"
#include "scenes.h"

#define NVS_NS "holo_cfg"

static const char *TAG = "settings";

#if CONFIG_BOARD_TOUCH_AUTOSTART
#define TOUCH_DEFAULT true
#else
#define TOUCH_DEFAULT false     /* and in a build without touch at all */
#endif

void settings_defaults(settings_t *out)
{
    memset(out, 0, sizeof(*out));
    out->backlight = 100;
    out->led_brightness = CONFIG_LEDS_BRIGHTNESS;
    out->led_count = CONFIG_LEDS_COUNT;
    out->touch = TOUCH_DEFAULT;
}

void settings_get(settings_t *out)
{
    settings_defaults(out);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(out->boot_scene);
    if (nvs_get_str(h, "boot", out->boot_scene, &len) != ESP_OK) {
        out->boot_scene[0] = '\0';
    }
    uint8_t u8;
    uint16_t u16;
    if (nvs_get_u8(h, "backlight", &u8) == ESP_OK && u8 >= 1 && u8 <= 100) {
        out->backlight = u8;
    }
    if (nvs_get_u8(h, "led_bright", &u8) == ESP_OK && u8 >= 1 && u8 <= 100) {
        out->led_brightness = u8;
    }
    if (nvs_get_u16(h, "led_count", &u16) == ESP_OK && u16 >= 1 && u16 <= 256) {
        out->led_count = u16;
    }
#if CONFIG_BOARD_TOUCH_ENABLE
    if (nvs_get_u8(h, "touch", &u8) == ESP_OK && u8 <= 1) {
        out->touch = u8 != 0;
    }
#endif
    nvs_close(h);
}

esp_err_t settings_save(const settings_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    if (s->boot_scene[0] != '\0') {
        err = nvs_set_str(h, "boot", s->boot_scene);
    } else if ((err = nvs_erase_key(h, "boot")) == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "backlight", (uint8_t)s->backlight);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "led_bright", (uint8_t)s->led_brightness);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(h, "led_count", (uint16_t)s->led_count);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "touch", s->touch ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    events_changed("settings");
    return err;
}

esp_err_t settings_apply(const settings_t *s, bool touch)
{
    screen_set_backlight(s->backlight);
    leds_set_brightness(s->led_brightness);
    esp_err_t err = ESP_OK;
#if CONFIG_BOARD_TOUCH_ENABLE
    if (!touch) {
        return ESP_OK;
    }
    if (s->touch) {
        err = board_touch_start();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "touch: %s", esp_err_to_name(err));
        }
    } else {
        board_touch_stop();
    }
#else
    (void)touch;
#endif
    return err;
}

esp_err_t settings_reset(settings_t *out)
{
    settings_defaults(out);
    const esp_err_t err = settings_save(out);
    if (err == ESP_OK) {
        settings_apply(out, true);  /* nothing applied unless all of it is saved */
    }
    return err;
}

int settings_led_count(void)
{
    settings_t s;
    settings_get(&s);
    return s.led_count;
}

void settings_apply_boot(void)
{
    settings_t s;
    settings_get(&s);
    settings_apply(&s, true);
    if (s.boot_scene[0] == '\0') {
        return;
    }
    cJSON *scene = scene_load(s.boot_scene);
    if (scene == NULL) {
        ESP_LOGW(TAG, "the boot scene '%s' is gone", s.boot_scene);
        return;
    }
    char why[128];
    if (scene_apply(scene, why, sizeof(why)) != SCENE_OK) {
        ESP_LOGW(TAG, "the boot scene '%s': %s", s.boot_scene, why);
    } else {
        ESP_LOGI(TAG, "started with '%s'", s.boot_scene);
    }
    cJSON_Delete(scene);
}

/* ------------------------------------------------------------------ console */

static void print_settings(const settings_t *s)
{
    printf("at start: %s\n", s->boot_scene[0] ? s->boot_scene : "nothing");
    printf("backlight %d%%, LED brightness %d%%, %d LEDs%s\n", s->backlight, s->led_brightness, s->led_count,
           s->led_count != leds_count() ? " (from the next start)" : "");
#if CONFIG_BOARD_TOUCH_ENABLE
    const bool now = board_touch_running();
    printf("touch reporting %s%s\n", s->touch ? "on" : "off",
           now == s->touch ? "" : now ? " (on until the next start: `touch on`)" : " (off until the next start: `touch off`)");
#endif
}

static int cmd_settings(int argc, char **argv)
{
    settings_t s;
    settings_get(&s);
    if (argc == 1) {
        print_settings(&s);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "reset") == 0) {
        settings_defaults(&s);
    } else if (argc == 3) {
        char *end;
        const long v = strtol(argv[2], &end, 10);
        const bool num = *end == '\0';
        if (strcmp(argv[1], "backlight") == 0 && num && v >= 1 && v <= 100) {
            s.backlight = (int)v;
        } else if (strcmp(argv[1], "brightness") == 0 && num && v >= 1 && v <= 100) {
            s.led_brightness = (int)v;
        } else if (strcmp(argv[1], "leds") == 0 && num && v >= 1 && v <= 256) {
            s.led_count = (int)v;
#if CONFIG_BOARD_TOUCH_ENABLE
        } else if (strcmp(argv[1], "touch") == 0 && (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
            s.touch = strcmp(argv[2], "on") == 0;
#endif
        } else {
            printf("settings: backlight and brightness are 1-100; leds is 1-256; touch is on or off\n");
            return 1;
        }
    } else {
        printf("usage: settings [backlight <1-100> | brightness <1-100> | leds <count> | touch on|off | reset]\n");
        return 1;
    }
    const esp_err_t err = settings_save(&s);
    if (err != ESP_OK) {
        printf("settings: not saved: %s\n", esp_err_to_name(err));
        return 1;
    }
    settings_apply(&s, argc == 2 || strcmp(argv[1], "touch") == 0);     /* reset, or touch: touch too */
    print_settings(&s);
    return 0;
}

void settings_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "settings",
        .help = "What the board starts with: the backlight, the LEDs' brightness and count, touch reporting, and "
                "(`scene boot`) the scene. Saved; the backlight, brightness and touch reporting change now too.",
        .hint = "[backlight <1-100> | brightness <1-100> | leds <count> | touch on|off | reset]",
        .func = cmd_settings,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
