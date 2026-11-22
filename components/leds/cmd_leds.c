/*
 * `leds`: the NeoPixel strip from the console.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "esp_console.h"
#include "leds.h"

#define USAGE "[colour <c> | off | wipe [<c>] [loop] | rainbow [loop] | flicker [<c>] | pulse [<c>] [-p ms] | " \
              "blink [<c>] [-p ms] | bright <1-100>] [-l pct] [-t s]"

static int usage(void)
{
    printf("usage: leds " USAGE "\n"
           "patterns, played once and then off, or over and over with `loop` until `leds off`:\n"
           "  wipe [<c>]     each LED to the colour in turn, 250 ms apart (white if none given)\n"
           "  rainbow        the colour wheel five times round the ring, 12.8 s\n"
           "until something else (white if no colour is given):\n"
           "  flicker [<c>]  the colour flickering like a failing hologram\n"
           "  pulse [<c>]    the colour fading up and down, once a period (-p, 200-60000 ms; 2000)\n"
           "  blink [<c>]    the colour on for half a period and off for the other half (-p; 1000)\n"
           "every mode but off also takes, as the holo's light does:\n"
           "  -l <pct>       the level, a share of the brightness, 0-100 (100)\n"
           "colour, off, flicker, pulse and blink also take:\n"
           "  -t <s>         for this long, 0.1-3600 s, and then back to what the ring was doing\n"
           "a colour <c> is a name (red, orange, ...), #RRGGBB, R,G,B or R G B\n");
    return 1;
}

/* A setting as `leds` shows it: "pulse #ffffff, every 2000 ms, at 40%" */
static void print_setting(const leds_setting_t *s)
{
    printf("%s", leds_mode_name(s->mode));
    if (s->mode != LEDS_OFF && s->mode != LEDS_RAINBOW) {
        printf(" #%02x%02x%02x", s->rgb[0], s->rgb[1], s->rgb[2]);
    }
    if ((s->mode == LEDS_WIPE || s->mode == LEDS_RAINBOW) && s->loop) {
        printf(", looping");
    }
    if (s->period_ms != 0) {
        printf(", every %u ms", (unsigned)s->period_ms);
    }
    if (s->level != 100 && s->mode != LEDS_OFF) {
        printf(", at %u%%", (unsigned)s->level);
    }
}

static int status(void)
{
    leds_state_t st;
    leds_state(&st);
    print_setting(&st.now);
    printf("; brightness %d%%", st.brightness);
    if (st.until_us != 0) {
        printf("; for %.1f s more%s, then ", leds_remaining_ms(&st) / 1000.0, st.leia ? " (a leia)" : "");
        print_setting(&st.after);
    }
    printf("; %d LEDs on GPIO%d, %s, %s\n", leds_count(), CONFIG_LEDS_GPIO, LEDS_SPEED_NAME, LEDS_ORDER_NAME);
    return 0;
}

static bool parse_long(const char *s, long *out)
{
    char *end;
    *out = strtol(s, &end, 10);
    return *s != '\0' && *end == '\0';
}

/*
 * A mode with its words and options: `leds colour|off|flicker|pulse|blink [<c>] [-p ms] [-l pct] [-t s]`, and
 * `leds wipe [<c>] [loop] [-l pct]`, `leds rainbow [loop] [-l pct]`. The colour's words (and a pattern's `loop`)
 * come before the options; `allowed` lists the option letters this mode takes.
 */
static int light_mode(leds_mode_t mode, int argc, char **argv, const char *allowed)
{
    int words = 0;
    while (2 + words < argc && argv[2 + words][0] != '-') {
        words++;
    }
    leds_setting_t s = { .mode = mode, .rgb = { 255, 255, 255 }, .level = 100 };
    if (mode == LEDS_WIPE || mode == LEDS_RAINBOW) {
        /* played once, or with a trailing `loop` over and over */
        s.loop = words > 0 && strcmp(argv[1 + words], "loop") == 0;
        words -= s.loop;
    }
    if (mode == LEDS_OFF || mode == LEDS_RAINBOW) {
        leds_mode(s.rgb, NULL);     /* kept through off and a rainbow, for the next colour-less request */
        if (words > 0) {
            return usage();
        }
    } else if (words > 0 && !board_parse_rgb_args(words, argv + 2, s.rgb)) {
        return usage();
    } else if (words == 0 && mode == LEDS_SOLID) {
        return usage();
    }
    uint32_t ms = 0;            /* -t's, 0 for good */
    for (int i = 2 + words + s.loop; i < argc; i += 2) {
        const char *o = argv[i];
        long v = 0;
        if (o[0] != '-' || o[1] == '\0' || o[2] != '\0' || strchr(allowed, o[1]) == NULL) {
            printf("leds: '%s' is not an option here (this takes -%s)\n", o, allowed);
            return 1;
        }
        if (i + 1 >= argc) {
            printf("leds: %s needs a value\n", o);
            return 1;
        }
        const char *val = argv[i + 1];
        bool ok = false;
        switch (o[1]) {
        case 'p': ok = parse_long(val, &v) && leds_period_ok(v); s.period_ms = (uint16_t)v; break;
        case 'l': ok = parse_long(val, &v) && leds_level_ok(v); s.level = (uint8_t)v; break;
        case 't': {
            char *end;
            const float time_s = strtof(val, &end);
            ok = *val != '\0' && *end == '\0' && leds_timed_ms(time_s, &ms);
            break;
        }
        }
        if (!ok) {
            printf("leds: %s %s is out of range\n", o, val);
            return 1;
        }
    }
    const esp_err_t err = ms > 0 ? leds_set_timed(&s, ms, false) : leds_set(&s);
    if (err != ESP_OK) {
        printf("leds: %s\n", esp_err_to_name(err));
        return 1;
    }
    return 0;
}

static int cmd_leds(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "status";
    if (strcmp(sub, "status") == 0) {
        return status();
    }
    if ((strcmp(sub, "colour") == 0 || strcmp(sub, "color") == 0) && argc >= 3) {
        return light_mode(LEDS_SOLID, argc, argv, "lt");
    }
    if (strcmp(sub, "off") == 0 || strcmp(sub, "stop") == 0) {
        return light_mode(LEDS_OFF, argc, argv, "t");
    }
    if (strcmp(sub, "flicker") == 0) {
        return light_mode(LEDS_FLICKER, argc, argv, "lt");
    }
    if (strcmp(sub, "pulse") == 0) {
        return light_mode(LEDS_PULSE, argc, argv, "plt");
    }
    if (strcmp(sub, "blink") == 0) {
        return light_mode(LEDS_BLINK, argc, argv, "plt");
    }
    if (strcmp(sub, "wipe") == 0) {
        return light_mode(LEDS_WIPE, argc, argv, "l");
    }
    if (strcmp(sub, "rainbow") == 0) {
        return light_mode(LEDS_RAINBOW, argc, argv, "l");
    }
    if (strcmp(sub, "bright") == 0 && argc == 3) {
        leds_set_brightness(atoi(argv[2]));
        return 0;
    }
    return usage();
}

void register_leds_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "leds",
        .help = "The NeoPixel strip: a solid colour, off, brightness, or a pattern. Played once then off, "
                "or looped: wipe [<c>] (each LED to the colour in turn), rainbow (the colour wheel round "
                "the ring). Until something else: flicker [<c>] (a failing hologram), pulse [<c>] (fading up "
                "and down), blink [<c>]. -l: a level; -t: for a time, then back",
        .hint = USAGE,
        .func = cmd_leds,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
