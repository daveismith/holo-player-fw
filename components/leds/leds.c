/*
 * The NeoPixel strip: Espressif's led_strip on the RMT, and the patterns that run on it.
 *
 * One task, the pattern task, runs a setting while the ring animates (wipe, rainbow, flicker, pulse,
 * blink) or a timed setting waits for its end; a solid colour or off, for good, needs none, and the
 * task then blocks, holding nothing but its stack. It is made the first time a setting wants it and
 * kept, never deleted: a burst of changes would otherwise make and delete a task each, and a deleted
 * task's stack is freed only later, by the idle task, so several could be held at once. Its 3 KB
 * stack is internal RAM from then on, as it was while any pattern ran before.
 *
 * A change asks the task to let go of what it runs (s_stop, and a notification), waits for it to say
 * it has (s_stopped), sets the new setting and hands it over (s_busy, and a notification), all under
 * s_ctl, so that two callers (the console and the API) never interleave. The task sleeps on its
 * notification until the next thing it has to do (sleep_until()): a frame, a blink's next edge, a
 * timed setting's end; a stop and a brightness change wake it at once. It makes its own transitions:
 * a timed setting's end (to `after`) and a pattern played once (to off); nothing else changes the
 * setting while it runs, so it reads the setting without the lock, and writes it under the lock.
 */
#include <math.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "events.h"
#include "leds.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "leds";

/* LEDs on the strip: CONFIG_LEDS_COUNT, unless leds_set_count() said otherwise before leds_init() */
static int s_count = CONFIG_LEDS_COUNT;
#define N s_count

/* The RMT's DMA buffer holds the whole frame twice over, so a frame never waits on a refill:
 * a refill is an interrupt, and while flash is being written (a console history save, say)
 * interrupts outside IRAM wait, and the RMT would send stale symbols meanwhile. 24 symbols
 * an LED, plus the reset. */
#define RMT_SYMBOLS      ((2 * (24 * N + 8) + 63) / 64 * 64)

#define TASK_STACK       3072
#define TASK_PRIO        3      /* under the video player's 5 */
#define FRAME_MS         10     /* the rainbow's step; one tick at CONFIG_FREERTOS_HZ=100 */
#define WAVE_MS          20     /* a pulse's frame */
/* The same in ticks, for next_frame(): one at least */
#define TICKS_OF(ms)     (pdMS_TO_TICKS(ms) > 0 ? pdMS_TO_TICKS(ms) : 1)
#define FRAME_TICKS      TICKS_OF(FRAME_MS)
#define WAVE_TICKS       TICKS_OF(WAVE_MS)

static const char *const NAMES[LEDS_MODES] = { "off", "solid", "wipe", "rainbow", "flicker", "pulse", "blink" };

static led_strip_handle_t s_strip;
static SemaphoreHandle_t s_lock;   /* the strip, and the state below */
/* Held across a whole change -- stopping a pattern, then starting the next -- so two callers
 * (the console and the API) cannot each start one. */
static SemaphoreHandle_t s_ctl;
static TaskHandle_t s_task;         /* from the first setting that wants it on, never deleted */
static SemaphoreHandle_t s_stopped; /* given by the task when a stop has made it let go */
static volatile bool s_stop;        /* a change asks the task to let go; under s_lock */
static volatile bool s_busy;        /* the task has a setting to run; under s_lock */
static leds_setting_t s_set = { .mode = LEDS_OFF, .level = 100 };
static leds_setting_t s_after;      /* what a timed setting goes back to */
static int64_t s_until;             /* a timed setting's end, esp_timer's clock; 0 for none */
static bool s_leia;
static int64_t s_t0;                /* when s_set began: a pulse's and a blink's phase */
static int s_brightness = CONFIG_LEDS_BRIGHTNESS;
static uint8_t s_gamma[256];

const char *leds_mode_name(leds_mode_t mode)
{
    return (unsigned)mode < LEDS_MODES ? NAMES[mode] : "?";
}

bool leds_mode_of(const char *name, leds_mode_t *out)
{
    for (int i = 0; name != NULL && i < LEDS_MODES; i++) {
        if (strcmp(name, NAMES[i]) == 0) {
            *out = (leds_mode_t)i;
            return true;
        }
    }
    return false;
}

static bool animated(leds_mode_t m)
{
    return m != LEDS_OFF && m != LEDS_SOLID;
}

/* The brightness and the setting's level, both percent */
static uint8_t dim(uint8_t v)
{
    return (uint8_t)((v * s_brightness * s_set.level + 5000) / 10000);
}

static void put(int i, uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(s_strip, i, dim(r), dim(g), dim(b));
}

static esp_err_t fill_locked(const uint8_t rgb[3])
{
    for (int i = 0; i < N; i++) {
        put(i, rgb[0], rgb[1], rgb[2]);
    }
    return led_strip_refresh(s_strip);
}

/* Every LED the colour scaled by l (0..1) */
static void fill_scaled_locked(float l)
{
    const uint8_t *c = s_set.rgb;
    for (int i = 0; i < N; i++) {
        put(i, (uint8_t)(c[0] * l), (uint8_t)(c[1] * l), (uint8_t)(c[2] * l));
    }
    led_strip_refresh(s_strip);
}

/* A setting that doesn't animate, drawn: the colour, or dark */
static esp_err_t draw_static_locked(void)
{
    return s_set.mode == LEDS_SOLID ? fill_locked(s_set.rgb) : led_strip_clear(s_strip);
}

/* Adafruit_NeoPixel::ColorHSV() at full saturation and value: a 16-bit hue round a wheel of
 * 1530 steps, red to green to blue and back. */
static void wheel(uint16_t hue16, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const uint32_t hue = ((uint32_t)hue16 * 1530u + 32768u) / 65536u;
    if (hue < 510) {
        *b = 0;
        if (hue < 255) {
            *r = 255;
            *g = (uint8_t)hue;
        } else {
            *r = (uint8_t)(510 - hue);
            *g = 255;
        }
    } else if (hue < 1020) {
        *r = 0;
        if (hue < 765) {
            *g = 255;
            *b = (uint8_t)(hue - 510);
        } else {
            *g = (uint8_t)(1020 - hue);
            *b = 255;
        }
    } else if (hue < 1530) {
        *g = 0;
        if (hue < 1275) {
            *r = (uint8_t)(hue - 1020);
            *b = 255;
        } else {
            *r = 255;
            *b = (uint8_t)(1530 - hue);
        }
    } else {
        *r = 255;
        *g = *b = 0;
    }
}

/* Whether the running setting is over for the task: a change has stopped it, or its time is up */
static bool halted(void)
{
    return s_stop || (s_until != 0 && esp_timer_get_time() >= s_until);
}

/* Asleep until `when` (esp_timer's clock), the setting's end if that comes first, or a notification (a change
 * stopping it, a brightness to show), whichever is soonest; to the tick, so it can wake a little early. The
 * caller looks at halted() and the clock again */
static void sleep_until(int64_t when)
{
    if (s_until != 0 && s_until < when) {
        when = s_until;
    }
    const int64_t left = when - esp_timer_get_time();
    if (left > 0 && !s_stop) {
        const int64_t tick_us = 1000000 / configTICK_RATE_HZ;
        ulTaskNotifyTake(pdTRUE, (TickType_t)((left + tick_us - 1) / tick_us));
    }
}

/* Until `when`, or halted */
static void pause_until(int64_t when)
{
    while (!halted() && esp_timer_get_time() < when) {
        sleep_until(when);
    }
}

static void pause_ms(int ms)
{
    pause_until(esp_timer_get_time() + (int64_t)ms * 1000);
}

/*
 * A frame schedule on the tick's own clock, as vTaskDelayUntil() keeps one, but woken by a stop or a brightness
 * and ending with the setting's time: frames a whole number of ticks apart come out evenly. (On esp_timer's clock,
 * slept in whole ticks, a frame due just after a tick woke the task at that tick, a little early, for a whole
 * tick more, and the next frame then came straight after it: two at once, every other tick, for the whole pass.)
 * `*due` is the tick the last frame was due at; this waits for the next, `period` ticks on, or halted. A late
 * frame doesn't put the rest back, as with vTaskDelayUntil(), unless it is a whole period late: then the
 * schedule starts again from now
 */
static void next_frame(TickType_t *due, TickType_t period)
{
    *due += period;
    for (;;) {
        if (halted()) {
            return;
        }
        const TickType_t now = xTaskGetTickCount();
        const TickType_t left = *due - now;
        if (left == 0 || left > period) {       /* due, or past it (the difference wraps) */
            if (left != 0 && now - *due >= period) {
                *due = now;
            }
            return;
        }
        TickType_t wait = left;
        if (s_until != 0) {
            /* the setting's end, if sooner: to the tick after it */
            const int64_t us = s_until - esp_timer_get_time();
            const int64_t tick_us = 1000000 / configTICK_RATE_HZ;
            const TickType_t end = us <= 0 ? 0 : (TickType_t)((us + tick_us - 1) / tick_us);
            wait = end < wait ? end : wait;
        }
        if (wait > 0) {
            ulTaskNotifyTake(pdTRUE, wait);
        }
    }
}

static void wipe_pass(void)
{
    /* wipe: each LED to the colour in turn, 250 ms apart. */
    for (int i = 0; i < N && !halted(); i++) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        put(i, s_set.rgb[0], s_set.rgb[1], s_set.rgb[2]);
        led_strip_refresh(s_strip);
        xSemaphoreGive(s_lock);
        pause_ms(250);
    }
}

static void rainbow_pass(void)
{
    /* rainbow, as Adafruit_NeoPixel's example draws it: the first LED's hue goes five times round the
     * wheel, 256/65536 of a turn a frame at 10 ms a frame (12.8 s), the others spread once
     * round the strip after it, each gamma-corrected as gamma32() did. The frames are on a fixed
     * schedule (next_frame()), so a late one doesn't put the rest back */
    TickType_t due = xTaskGetTickCount();
    for (uint32_t first = 0; first < 5 * 65536 && !halted(); first += 256) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < N; i++) {
            uint8_t r, g, b;
            wheel((uint16_t)(first + (uint32_t)i * 65536u / N), &r, &g, &b);
            put(i, s_gamma[r], s_gamma[g], s_gamma[b]);
        }
        led_strip_refresh(s_strip);
        xSemaphoreGive(s_lock);
        next_frame(&due, FRAME_TICKS);
    }
}

/* Wipe or rainbow: true when it played out (once, without `loop`), false when halted */
static bool run_pattern(void)
{
    for (;;) {
        if (s_set.mode == LEDS_WIPE) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            led_strip_clear(s_strip);   /* each pass starts from dark */
            xSemaphoreGive(s_lock);
            wipe_pass();
        } else {
            rainbow_pass();
        }
        pause_ms(1000);   /* a second's hold after each pass */
        if (halted()) {
            return false;
        }
        if (!s_set.loop) {
            return true;
        }
    }
}

/* [0, 1) */
static float frand(void)
{
    return (float)(esp_random() >> 8) / 16777216.0f;
}

/*
 * flicker: a failing hologram, as the holo's own light flickers -- restless and mostly bright,
 * each LED a little apart from the rest, with the odd dropout to near dark and, now and then, a
 * short stutter of them. Until halted.
 */
static void run_flicker(void)
{
    int stutter = 0;
    while (!halted()) {
        float level;
        if (stutter > 0) {
            stutter--;
            level = (stutter & 1) ? 0.15f + 0.2f * frand() : 0.6f + 0.4f * frand();
        } else if (frand() < 0.04f) {
            stutter = 2 + (int)(frand() * 5);
            level = 0.1f + 0.2f * frand();
        } else {
            level = 0.55f + 0.45f * frand();
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < N; i++) {
            const float l = level * (0.85f + 0.15f * frand());
            put(i, (uint8_t)(s_set.rgb[0] * l), (uint8_t)(s_set.rgb[1] * l), (uint8_t)(s_set.rgb[2] * l));
        }
        led_strip_refresh(s_strip);
        xSemaphoreGive(s_lock);
        pause_ms(stutter > 0 ? 30 + (int)(frand() * 30) : 40 + (int)(frand() * 80));
    }
}

/* pulse: up from dark to the level and down again, once a period, through gamma 2 so that it looks
 * even, as the holo engine's light pulses: a frame every WAVE_MS, on the tick (next_frame()). blink: on for the first half of each
 * period, off for the second: drawn at each edge, and asleep between. Until halted. A brightness change
 * wakes it, so that it shows at once */
static void run_wave(void)
{
    const uint32_t period = s_set.period_ms ? s_set.period_ms : 1000;
    TickType_t due = xTaskGetTickCount();
    while (!halted()) {
        const int64_t now = esp_timer_get_time();
        const uint32_t ms = (uint32_t)((now - s_t0) / 1000) % period;
        float l;
        int64_t next = 0;
        if (s_set.mode == LEDS_PULSE) {
            const float f = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)ms / (float)period));
            l = f * f;
        } else {
            l = ms < period / 2 ? 1.0f : 0.0f;
            next = now + (int64_t)((ms < period / 2 ? period / 2 : period) - ms) * 1000;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        fill_scaled_locked(l);
        xSemaphoreGive(s_lock);
        if (s_set.mode == LEDS_PULSE) {
            next_frame(&due, WAVE_TICKS);
        } else {
            sleep_until(next);
        }
    }
}

/* A setting that doesn't animate, timed: drawn already; nothing to do until halted */
static void run_wait(void)
{
    while (!halted()) {
        sleep_until(s_until);
    }
}

/* A wipe or a rainbow that plays once: what the ring goes back to in its place, where it would have ended. The
 * one place that says so: the task's own end of one, a timed setting's `after`, and a restore */
static leds_setting_t ended(leds_setting_t s)
{
    if ((s.mode == LEDS_WIPE || s.mode == LEDS_RAINBOW) && !s.loop) {
        s.mode = LEDS_OFF;
    }
    return s;
}

static void pattern_task(void *arg)
{
    (void)arg;
    for (;;) {
        while (!s_busy) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);     /* until apply() hands over a setting */
        }
        bool played_out = false;
        switch (s_set.mode) {
        case LEDS_WIPE:
        case LEDS_RAINBOW: played_out = run_pattern(); break;
        case LEDS_FLICKER: run_flicker(); break;
        case LEDS_PULSE:
        case LEDS_BLINK:   run_wave(); break;
        default:           run_wait(); break;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_stop) {
            /* A change: it sets what comes next, once told this has let go */
            s_busy = false;
            xSemaphoreGive(s_lock);
            xSemaphoreGive(s_stopped);
            continue;
        }
        /* Its own transition: a timed setting's end, or a pattern played once */
        const int64_t now = esp_timer_get_time();
        if (s_until != 0 && now >= s_until) {
            s_set = s_after;
            s_until = 0;
            s_leia = false;
        } else if (played_out) {
            s_set = ended(s_set);
        }
        s_t0 = now;
        if (!animated(s_set.mode)) {
            draw_static_locked();
        }
        s_busy = animated(s_set.mode) || s_until != 0;
        xSemaphoreGive(s_lock);
        events_changed("leds");
    }
}

/* The task to let go of what it runs, if it runs anything: false if it didn't say it had within 2 s. Under s_ctl */
static bool stop_pattern(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool busy = s_busy;
    if (busy) {
        xSemaphoreTake(s_stopped, 0);       /* none left over from a stop that timed out */
        s_stop = true;
    }
    xSemaphoreGive(s_lock);
    if (!busy) {
        return true;
    }
    xTaskNotifyGive(s_task);
    return xSemaphoreTake(s_stopped, pdMS_TO_TICKS(2000)) == pdTRUE;
}

/* s, checked and made whole: its period's default, or ESP_ERR_INVALID_ARG */
static esp_err_t normalise(const leds_setting_t *in, leds_setting_t *out)
{
    if (in == NULL || (unsigned)in->mode >= LEDS_MODES || !leds_level_ok(in->level)) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = *in;
    if (out->mode == LEDS_PULSE || out->mode == LEDS_BLINK) {
        if (out->period_ms == 0) {
            out->period_ms = out->mode == LEDS_PULSE ? LEDS_PULSE_MS : LEDS_BLINK_MS;
        }
        if (!leds_period_ok(out->period_ms)) {
            return ESP_ERR_INVALID_ARG;
        }
    } else {
        out->period_ms = 0;
    }
    if (out->mode != LEDS_WIPE && out->mode != LEDS_RAINBOW) {
        out->loop = false;
    }
    return ESP_OK;
}

/*
 * The one place a caller changes the ring: the pattern stopped, then s, with its end and what it goes
 * back to (until 0: for good), and the pattern task handed it if it is wanted. Under s_ctl.
 */
static esp_err_t apply(const leds_setting_t *s, int64_t until, const leds_setting_t *after, bool leia)
{
    ESP_RETURN_ON_FALSE(s_strip != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    xSemaphoreTake(s_ctl, portMAX_DELAY);
    if (!stop_pattern()) {
        xSemaphoreGive(s_ctl);
        ESP_LOGE(TAG, "the pattern task didn't stop");
        return ESP_ERR_TIMEOUT;
    }
    const bool wants_task = animated(s->mode) || until != 0;
    if (wants_task && s_task == NULL &&
        xTaskCreate(pattern_task, "leds", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        s_task = NULL;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_set = *s;
        s_set.mode = LEDS_OFF;
        s_until = 0;
        s_leia = false;
        led_strip_clear(s_strip);
        xSemaphoreGive(s_lock);
        xSemaphoreGive(s_ctl);
        events_changed("leds");
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_set = *s;
    s_until = until;
    if (after != NULL) {
        s_after = *after;
    }
    s_leia = until != 0 && leia;
    s_t0 = esp_timer_get_time();
    s_stop = false;
    const esp_err_t err = animated(s_set.mode) ? ESP_OK : draw_static_locked();
    s_busy = wants_task;
    xSemaphoreGive(s_lock);
    if (wants_task) {
        xTaskNotifyGive(s_task);
    }
    xSemaphoreGive(s_ctl);
    events_changed("leds");
    return err;
}

bool leds_timed_ms(double seconds, uint32_t *ms)
{
    /* The least as a person types it, 0.1, which a double holds as a hair over or under */
    if (!(seconds >= LEDS_TIMED_MIN_MS / 1000.0 - 1e-9 && seconds <= LEDS_TIMED_MAX_MS / 1000.0)) {
        return false;
    }
    *ms = (uint32_t)(seconds * 1000.0 + 0.5);
    return true;
}

esp_err_t leds_set(const leds_setting_t *s)
{
    leds_setting_t n;
    ESP_RETURN_ON_ERROR(normalise(s, &n), TAG, "not a setting");
    return apply(&n, 0, NULL, false);
}

esp_err_t leds_set_timed(const leds_setting_t *s, uint32_t ms, bool leia)
{
    leds_setting_t n;
    ESP_RETURN_ON_ERROR(normalise(s, &n), TAG, "not a setting");
    ESP_RETURN_ON_FALSE(ms >= LEDS_TIMED_MIN_MS && ms <= LEDS_TIMED_MAX_MS, ESP_ERR_INVALID_ARG, TAG, "not a time");
    ESP_RETURN_ON_FALSE(s_strip != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    /* Back to what it was doing, unless that was timed: then to what that would have gone back to.
     * Read without s_ctl: a change between here and apply() would be lost from `after`, as from any
     * other read-then-change of a setting; the console and the API rarely race */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const leds_setting_t after = s_until != 0 ? s_after : ended(s_set);
    xSemaphoreGive(s_lock);
    return apply(&n, esp_timer_get_time() + (int64_t)ms * 1000, &after, leia);
}

esp_err_t leds_off(void)
{
    ESP_RETURN_ON_FALSE(s_strip != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    leds_setting_t s;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s = (leds_setting_t){ .mode = LEDS_OFF, .rgb = { s_set.rgb[0], s_set.rgb[1], s_set.rgb[2] }, .level = 100 };
    xSemaphoreGive(s_lock);
    return leds_set(&s);
}

void leds_state(leds_state_t *out)
{
    memset(out, 0, sizeof(*out));
    if (s_lock == NULL) {
        out->now = s_set;
        out->brightness = s_brightness;
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out->now = s_set;
    out->until_us = s_until;
    out->after = s_after;
    out->leia = s_leia;
    out->brightness = s_brightness;
    xSemaphoreGive(s_lock);
}

uint32_t leds_remaining_ms(const leds_state_t *st)
{
    if (st->until_us == 0) {
        return UINT32_MAX;
    }
    const int64_t left = st->until_us - esp_timer_get_time();
    return left > 0 ? (uint32_t)(left / 1000) : 0;
}

esp_err_t leds_restore(const leds_state_t *st)
{
    leds_set_brightness(st->brightness);
    if (st->until_us != 0 && st->until_us > esp_timer_get_time() + LEDS_TIMED_MIN_MS * 1000) {
        leds_setting_t now, after;
        if (normalise(&st->now, &now) != ESP_OK || normalise(&st->after, &after) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }
        after = ended(after);
        return apply(&now, st->until_us, &after, st->leia);
    }
    const leds_setting_t back = ended(st->until_us != 0 ? st->after : st->now);
    return leds_set(&back);
}

void leds_set_brightness(int percent)
{
    percent = percent < LEDS_BRIGHTNESS_MIN ? LEDS_BRIGHTNESS_MIN : percent > LEDS_BRIGHTNESS_MAX ? LEDS_BRIGHTNESS_MAX
                                                                                              : percent;
    if (s_lock == NULL) {
        s_brightness = percent;
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_brightness = percent;
    if (s_set.mode == LEDS_SOLID) {
        fill_locked(s_set.rgb);
    }
    xSemaphoreGive(s_lock);
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);        /* a pattern asleep (a blink between edges) draws it now */
    }
    events_changed("leds");
}

int leds_count(void)
{
    return N;
}

void leds_set_count(int count)
{
    if (s_strip == NULL && count >= 1 && count <= 1024) {
        s_count = count;
    }
}

int leds_get_brightness(void)
{
    return s_brightness;
}

leds_mode_t leds_mode(uint8_t rgb[3], bool *loop)
{
    if (rgb != NULL) {
        memcpy(rgb, s_set.rgb, 3);
    }
    if (loop != NULL) {
        *loop = s_set.loop;
    }
    return s_set.mode;
}

esp_err_t leds_init(void)
{
    if (s_strip != NULL) {
        return ESP_OK;
    }
    /* Adafruit's gamma table: gamma 2.6 */
    for (int i = 0; i < 256; i++) {
        s_gamma[i] = (uint8_t)(powf(i / 255.0f, 2.6f) * 255.0f + 0.5f);
    }
    s_lock = xSemaphoreCreateMutex();
    s_ctl = xSemaphoreCreateMutex();
    s_stopped = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_lock != NULL && s_ctl != NULL && s_stopped != NULL, ESP_ERR_NO_MEM, TAG, "lock");

    const led_strip_config_t strip_cfg = {
        .strip_gpio_num = CONFIG_LEDS_GPIO,
        .max_leds = N,
#if CONFIG_LEDS_SPEED_400K
        .led_model = LED_MODEL_WS2811,
#else
        .led_model = LED_MODEL_WS2812,
#endif
#if CONFIG_LEDS_ORDER_GRB
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
#else
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
#endif
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = RMT_SYMBOLS,
        .flags = { .with_dma = true },
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    bool dma = true;
    if (err != ESP_OK) {
        /* No DMA channel to spare: the RMT's own memory, refilled from its interrupt. */
        ESP_LOGW(TAG, "RMT with DMA: %s; trying without", esp_err_to_name(err));
        rmt_cfg.flags.with_dma = false;
        rmt_cfg.mem_block_symbols = 0;   /* the driver's default */
        dma = false;
        err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    }
    ESP_RETURN_ON_ERROR(err, TAG, "strip");

    /* Off, whatever the LEDs latched when they powered up. */
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "%d LEDs on GPIO%d, %s, %s%s", N, CONFIG_LEDS_GPIO, LEDS_SPEED_NAME,
             LEDS_ORDER_NAME, dma ? ", RMT DMA" : "");
    return ESP_OK;
}
