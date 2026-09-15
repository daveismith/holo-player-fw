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

/* Start playing `path` (absolute) in the background, stopping anything already playing. */
esp_err_t video_play(const char *path, bool loop);
/* Stop playback and wait for it to end. */
void video_stop(void);
bool video_playing(void);

#ifdef __cplusplus
}
#endif
