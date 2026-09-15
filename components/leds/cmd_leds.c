/*
 * `leds`: the NeoPixel strip from the console.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "esp_console.h"
#include "leds.h"

static const char *const k_modes[] = { "off", "solid", "wipe", "rainbow" };

static int usage(void)
{
    printf("usage: leds [colour <c> | off | wipe [<c>] [loop] | rainbow [loop] | bright <1-100>]\n"
           "patterns, played once and then off, or over and over with `loop` until `leds off`:\n"
           "  wipe [<c>]  each LED to the colour in turn, 250 ms apart (white if none given)\n"
           "  rainbow     the colour wheel five times round the ring, 12.8 s\n"
           "a colour <c> is a name (red, orange, ...), #RRGGBB, R,G,B or R G B\n");
    return 1;
}

static int cmd_leds(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "status";
    if (strcmp(sub, "status") == 0) {
        uint8_t rgb[3];
        bool loop;
        const leds_mode_t mode = leds_mode(rgb, &loop);
        printf("%s", k_modes[mode]);
        if (mode == LEDS_SOLID || mode == LEDS_WIPE) {
            printf(" #%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
        }
        if ((mode == LEDS_WIPE || mode == LEDS_RAINBOW) && loop) {
            printf(", looping");
        }
        printf("; brightness %d%%; %d LEDs on GPIO%d, %s, %s\n", leds_get_brightness(),
               CONFIG_LEDS_COUNT, CONFIG_LEDS_GPIO, LEDS_SPEED_NAME, LEDS_ORDER_NAME);
        return 0;
    }
    if ((strcmp(sub, "colour") == 0 || strcmp(sub, "color") == 0) && argc >= 3) {
        uint8_t rgb[3];
        if (!board_parse_rgb_args(argc - 2, argv + 2, rgb)) {
            return usage();
        }
        return leds_solid(rgb[0], rgb[1], rgb[2]) == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "off") == 0 || strcmp(sub, "stop") == 0) {
        return leds_off() == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "wipe") == 0 || strcmp(sub, "rainbow") == 0) {
        /* an optional trailing "loop"; for a wipe, the colour's words before it */
        int words = argc - 2;
        const bool loop = words > 0 && strcmp(argv[argc - 1], "loop") == 0;
        if (loop) {
            words--;
        }
        uint8_t rgb[3] = { 255, 255, 255 };   /* a wipe with no colour given is white */
        if (sub[0] == 'w') {
            if (words > 0 && !board_parse_rgb_args(words, argv + 2, rgb)) {
                return usage();
            }
            return leds_play(LEDS_WIPE, rgb[0], rgb[1], rgb[2], loop) == ESP_OK ? 0 : 1;
        }
        if (words > 0) {
            return usage();
        }
        return leds_play(LEDS_RAINBOW, 0, 0, 0, loop) == ESP_OK ? 0 : 1;
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
        .help = "The NeoPixel strip: a solid colour, off, brightness, or a pattern played once "
                "then off, or looped. Patterns: wipe [<c>] (each LED to the colour in turn), "
                "rainbow (the colour wheel round the ring)",
        .hint = "[colour <c> | off | wipe [<c>] [loop] | rainbow [loop] | bright <1-100>]",
        .func = cmd_leds,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
