/*
 * The NeoPixel (WS2812/WS2811) strip on the P2 header: a solid colour, off, or a pattern -- a
 * colour wipe or the rainbow -- played once and then off, or looped. Configured under
 * "LED strip (NeoPixel)" in menuconfig.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

/* The menuconfig choices, for reports (an unchosen option is not defined at all). */
#if CONFIG_LEDS_SPEED_400K
#define LEDS_SPEED_NAME "400 kHz"
#else
#define LEDS_SPEED_NAME "800 kHz"
#endif
#if CONFIG_LEDS_ORDER_RGB
#define LEDS_ORDER_NAME "RGB"
#else
#define LEDS_ORDER_NAME "GRB"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LEDS_OFF,
    LEDS_SOLID,
    LEDS_WIPE,      /* each LED in turn to the colour, 250 ms apart; a second lit; off */
    LEDS_RAINBOW,   /* the colour wheel five times round the strip (12.8 s); a second; off */
} leds_mode_t;

/* Create the strip and turn it off, whatever it latched at power-up. */
esp_err_t leds_init(void);
/* Each stops any pattern first. */
esp_err_t leds_solid(uint8_t r, uint8_t g, uint8_t b);
esp_err_t leds_off(void);
/* Start LEDS_WIPE (in r,g,b) or LEDS_RAINBOW (r,g,b unused) in the background. */
esp_err_t leds_play(leds_mode_t pattern, uint8_t r, uint8_t g, uint8_t b, bool loop);
/* Percent, 1-100; applied at once to a solid colour and from the next frame to a pattern. */
void leds_set_brightness(int percent);
int leds_get_brightness(void);
/* What is showing; the colour for LEDS_SOLID and LEDS_WIPE, and whether a pattern loops. */
leds_mode_t leds_mode(uint8_t rgb[3], bool *loop);

/* The `leds` console command. */
void register_leds_commands(void);

#ifdef __cplusplus
}
#endif
