/*
 * The NeoPixel (WS2812/WS2811) strip on the P2 header: a solid colour, off, or a pattern -- a
 * colour wipe or the rainbow, played once and then off or looped; a flicker, a pulse or a blink,
 * until something else. Any of them can be timed: for a while, and then back to what the ring was
 * doing. Configured under "LED strip (NeoPixel)" in menuconfig.
 *
 * The ring is also the holo's light (the Cyphal r2.holo.Light service): its `on` is SOLID, and its
 * pulse, flicker and blink are the modes here of the same names, at a `level` that is a share of
 * the ring's brightness, so that the light at full is the ring at the brightness it is set to (the
 * power limit, on VSYS) and never more.
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

/* The numbers are the host link's LEDS frame's and r2.holo.state.RingSetting's */
typedef enum {
    LEDS_OFF,
    LEDS_SOLID,
    LEDS_WIPE,      /* each LED in turn to the colour, 250 ms apart; a second lit; off */
    LEDS_RAINBOW,   /* the colour wheel five times round the strip (12.8 s); a second; off */
    LEDS_FLICKER,   /* the colour, flickering like a failing hologram, until something else */
    LEDS_PULSE,     /* the colour fading up and down again once a period, until something else */
    LEDS_BLINK,     /* the colour for half a period and off for the other half, until something else */
} leds_mode_t;
#define LEDS_MODES          7

#define LEDS_PERIOD_MIN_MS  200     /* pulse, blink */
#define LEDS_PERIOD_MAX_MS  60000
#define LEDS_PULSE_MS       2000    /* the defaults, as the holo engine's light has them */
#define LEDS_BLINK_MS       1000
#define LEDS_TIMED_MIN_MS   100     /* a timed setting */
#define LEDS_TIMED_MAX_MS   3600000
#define LEDS_LEVEL_MAX      100     /* a setting's level, percent */
#define LEDS_BRIGHTNESS_MIN 1       /* the ring's brightness, percent */
#define LEDS_BRIGHTNESS_MAX 100

/* The limits, checked here for every interface (each says it in its own words): a pulse's or a blink's period,
 * a level, a brightness */
static inline bool leds_period_ok(long ms)
{
    return ms >= LEDS_PERIOD_MIN_MS && ms <= LEDS_PERIOD_MAX_MS;
}
static inline bool leds_level_ok(long percent)
{
    return percent >= 0 && percent <= LEDS_LEVEL_MAX;
}
static inline bool leds_brightness_ok(double percent)
{
    return percent >= LEDS_BRIGHTNESS_MIN && percent <= LEDS_BRIGHTNESS_MAX;
}
/* A timed setting's time, in seconds as given (0.1..3600), as its milliseconds, rounded: false out of range */
bool leds_timed_ms(double seconds, uint32_t *ms);

/* What the ring is set to do */
typedef struct {
    leds_mode_t mode;
    uint8_t rgb[3];         /* the colour, for every mode but rainbow; kept through off and rainbow */
    bool loop;              /* wipe, rainbow: again and again, rather than once */
    uint16_t period_ms;     /* pulse, blink: LEDS_PERIOD_MIN_MS..MAX; 0 for the other modes */
    uint8_t level;          /* 0..100: the share of the brightness this lights at (0 lights nothing); 100 but for
                             * the holo's light */
} leds_setting_t;

/* The ring now */
typedef struct {
    leds_setting_t now;
    leds_setting_t after;   /* meaningful while timed */
    int64_t until_us;       /* a timed setting's end, when it goes back to `after`, on esp_timer's clock; 0 when not
                             * timed (leds_remaining_ms()) */
    bool leia;              /* the timed setting is a leia's (its caller said so), until it ends or is replaced */
    int brightness;         /* 1..100 */
} leds_state_t;

/* LEDs on the strip, before leds_init(): a saved setting, over CONFIG_LEDS_COUNT. Ignored once
 * the strip is made, and outside 1..1024. */
void leds_set_count(int count);
/* Create the strip and turn it off, whatever it latched at power-up. */
esp_err_t leds_init(void);

/* A mode's name ("off", "solid", ... "blink"), and the mode a name names: false for none */
const char *leds_mode_name(leds_mode_t mode);
bool leds_mode_of(const char *name, leds_mode_t *out);

/* A setting, for good: it ends a timed one, and its wait. A period of 0 is the mode's default
 * (LEDS_PULSE_MS, LEDS_BLINK_MS); one out of range for a pulse or a blink, a level over 100 or no
 * mode is ESP_ERR_INVALID_ARG. A loop matters only to a wipe or a rainbow, a period only to a pulse
 * or a blink: elsewhere they are cleared. */
esp_err_t leds_set(const leds_setting_t *s);
/* A setting for `ms` (LEDS_TIMED_MIN_MS..MAX), and then back to what the ring was doing before it,
 * or, during another timed setting, to what that one would have gone back to, so that a chain of
 * timed settings unwinds in one. A wipe or a rainbow played once that it interrupts goes back to off
 * (it would have ended). `leia` marks it as a leia's (leds_state_t.leia). Checked as leds_set(). */
esp_err_t leds_set_timed(const leds_setting_t *s, uint32_t ms, bool leia);
/* Off, for good, keeping the colour for the next request without one; at level 100. It ends a timed setting, as
 * leds_set() does */
esp_err_t leds_off(void);

void leds_state(leds_state_t *out);
/* A timed setting's time left, from now: UINT32_MAX when not timed, 0 once its end has come */
uint32_t leds_remaining_ms(const leds_state_t *st);
/* Put back what leds_state() gave earlier (a scene's restore): its brightness, and its setting. A timed
 * setting whose end hasn't come yet runs on to that end, then goes back to its `after`, as it would have;
 * one whose end has passed is its `after`. A wipe or a rainbow played once goes back to off. */
esp_err_t leds_restore(const leds_state_t *s);

/* Percent, 1-100; applied at once to a solid colour and from the next frame to a pattern. */
void leds_set_brightness(int percent);
int leds_get_brightness(void);
/* LEDs on the strip. */
int leds_count(void);
/* What is showing; its colour, and whether a pattern loops. */
leds_mode_t leds_mode(uint8_t rgb[3], bool *loop);

/* The `leds` console command. */
void register_leds_commands(void);

#ifdef __cplusplus
}
#endif
