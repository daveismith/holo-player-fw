/*
 * MJPEG QuickTime playback on the panel: `video play|stop|status|info`.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The `video` console command. Paths are relative to `base_path`, the storage volume. */
void register_video_commands(const char *base_path);

/*
 * Start playing `path` (absolute) in the background, stopping anything already playing.
 * Frames are decoded in 16-line blocks, each sent to the panel while the next decodes; with
 * `whole_frame`, each is decoded whole and then sent (also used when a dimension is not a
 * multiple of 8, which block decoding needs).
 */
esp_err_t video_play(const char *path, bool loop, bool whole_frame);
/* The same, `plays` times through: 0 forever, -1 once. */
esp_err_t video_play_n(const char *path, int plays, bool whole_frame);

/*
 * Told when a clip or animation stops: `finished` when it played out, false when it was stopped
 * (something else took the screen, or an error). Called from the player's task, which is ending:
 * it must not call into this component -- signal another task instead. One hook.
 */
typedef void (*screen_end_hook_t)(const char *path, bool finished, void *ctx);
void screen_set_end_hook(screen_end_hook_t hook, void *ctx);

/*
 * Told when a clip or animation starts: its path (absolute), `plays` (0 forever), and its frames
 * and frame rate from its header. Called on the task that started it, holding the screen: it may
 * read the screen's state, but not start or stop anything. One hook.
 */
typedef void (*screen_start_hook_t)(const char *path, int plays, uint32_t frames, double fps, void *ctx);
void screen_set_start_hook(screen_start_hook_t hook, void *ctx);
/* Stop playback and wait for it to end. */
void video_stop(void);
bool video_playing(void);

/*
 * What is on the screen, which is on only while something is: a clip (video_play), a solid
 * colour, or the calibration pattern. Showing either of the last two stops any clip;
 * clearing stops any clip and puts the panel to sleep with the backlight off, as does a clip
 * reaching its end.
 */
esp_err_t screen_show_colour(uint16_t rgb565);
/* The same, from 8-bit RGB, which is what the screen then reports showing. */
esp_err_t screen_show_rgb(const uint8_t rgb[3]);
/* A centred crosshair with an up arrow at the crossing, for aligning the panel in a dome. */
esp_err_t screen_show_calibration(void);
esp_err_t screen_clear(void);

/* The backlight, for whatever shows from now on: 0-100. Not saved. */
esp_err_t screen_set_backlight(int percent);

/* ---- for other callers (the HTTP API) ----
 *
 * Everything here and above is serialised: the console, the API and the player's own task
 * never draw over each other. */

typedef enum { MEDIA_CLIP, MEDIA_IMAGE, MEDIA_ANIMATION } media_kind_t;
typedef enum { MEDIA_MOV, MEDIA_PNG, MEDIA_JPEG, MEDIA_GIF } media_format_t;

typedef struct {
    media_kind_t kind;          /* a GIF of more than one frame is an animation */
    media_format_t format;
    uint32_t width, height;
    uint32_t frames;            /* clips and animations */
    double fps, duration_s;     /* the same */
    char codec[8];              /* a clip's: "jpeg" or "mjpa" -- or what it is instead */
    bool playable;
    char why[96];               /* when not playable */
} media_info_t;

/*
 * What a file is, from its header, drawing nothing. ESP_OK with `out` filled -- playable or
 * not, and why not; ESP_ERR_NOT_FOUND when there is no such file; ESP_ERR_NOT_SUPPORTED when it
 * is not a clip or an image at all (`out->why` says so).
 */
esp_err_t media_probe(const char *path, media_info_t *out);

/*
 * Show a file: a clip (QuickTime Motion-JPEG), or a PNG, JPEG or GIF. A clip or animated GIF
 * plays `plays` times: 0 forever, -1 the default (a clip once, a GIF as the file says). Checked
 * before the panel is touched:
 * ESP_ERR_NOT_FOUND, or ESP_ERR_INVALID_ARG for a file the board cannot show, with `why`
 * (a sentence without the path), and the screen unchanged.
 */
esp_err_t screen_show_file(const char *path, int plays, bool whole_frame, char *why, size_t why_len);

typedef enum { SCREEN_NOTHING, SCREEN_COLOUR, SCREEN_CALIBRATION, SCREEN_IMAGE, SCREEN_CLIP } screen_showing_t;

typedef struct {
    screen_showing_t showing;   /* SCREEN_CLIP for an animated GIF playing too */
    bool powered;
    int backlight;
    uint8_t rgb[3];             /* SCREEN_COLOUR */
    char path[160];             /* SCREEN_IMAGE, SCREEN_CLIP: absolute */
    bool loop;                  /* SCREEN_CLIP: forever */
    uint32_t plays;             /* SCREEN_CLIP: times through in all; 0 forever */
    uint32_t width, height, frames, shown, loops, late;
    double fps;
    int64_t elapsed_ms;
} screen_state_t;

void screen_get_state(screen_state_t *out);

/* A clip or animation playing from `path` -- the file, or anything under a directory -- stops,
 * and the screen clears: the file is about to be replaced, moved or deleted. True if it did. */
bool screen_release(const char *path);

#ifdef __cplusplus
}
#endif
