/*
 * Colours as the console takes them, for every command that shows one (`screen colour`,
 * `leds colour`): a name, #RRGGBB (or RRGGBB), or R,G,B in decimal as the Flash_PNG sketch's
 * RGB command took it -- also as three separate words.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "board.h"

bool board_parse_rgb(const char *s, uint8_t rgb[3])
{
    static const struct {
        const char *name;
        uint8_t r, g, b;
    } names[] = {
        { "black", 0, 0, 0 },       { "white", 255, 255, 255 }, { "red", 255, 0, 0 },
        { "green", 0, 255, 0 },     { "blue", 0, 0, 255 },      { "yellow", 255, 255, 0 },
        { "cyan", 0, 255, 255 },    { "magenta", 255, 0, 255 }, { "orange", 255, 128, 0 },
        { "purple", 128, 0, 255 },  { "pink", 255, 105, 180 },  { "grey", 128, 128, 128 },
        { "gray", 128, 128, 128 },
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcasecmp(s, names[i].name) == 0) {
            rgb[0] = names[i].r;
            rgb[1] = names[i].g;
            rgb[2] = names[i].b;
            return true;
        }
    }
    unsigned r, g, b;
    char tail;
    if (sscanf(s, "%u,%u,%u%c", &r, &g, &b, &tail) == 3 && r < 256 && g < 256 && b < 256) {
        rgb[0] = (uint8_t)r;
        rgb[1] = (uint8_t)g;
        rgb[2] = (uint8_t)b;
        return true;
    }
    const char *hex = s[0] == '#' ? s + 1 : s;
    if (strlen(hex) == 6 && strspn(hex, "0123456789abcdefABCDEF") == 6) {
        const unsigned long v = strtoul(hex, NULL, 16);
        rgb[0] = (uint8_t)(v >> 16);
        rgb[1] = (uint8_t)(v >> 8);
        rgb[2] = (uint8_t)v;
        return true;
    }
    return false;
}

bool board_parse_rgb_args(int argc, char **argv, uint8_t rgb[3])
{
    if (argc == 3) {
        /* "255 128 0" is "255,128,0" */
        char joined[48];
        snprintf(joined, sizeof(joined), "%s,%s,%s", argv[0], argv[1], argv[2]);
        return board_parse_rgb(joined, rgb);
    }
    return argc == 1 && board_parse_rgb(argv[0], rgb);
}
