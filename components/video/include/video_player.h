/*
 * MJPEG QuickTime playback on the panel: `video play|stop|status|info`.
 */
#pragma once

#include <stdbool.h>
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
/* A centred crosshair with an up arrow at the crossing, for aligning the panel in a dome. */
esp_err_t screen_show_calibration(void);
esp_err_t screen_clear(void);

#ifdef __cplusplus
}
#endif
