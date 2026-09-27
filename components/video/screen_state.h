/*
 * What the panel is showing, for the parts of this component that are not video_player.cpp.
 *
 * Every way of putting something on the panel -- a clip, a colour, the alignment pattern, an
 * image -- has to stop whatever was there first, and `screen` has to be able to say what won.
 * That state lives in video_player.cpp, next to the player task that half of it is about;
 * this is the door into it. Private to the component.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "video_player.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Take the panel for something else: stop any clip -- leaving the panel lit, rather than
 * blinking it off and back on -- and forget any colour, pattern or image that was showing.
 * Call it before drawing, and set what is showing afterwards.
 */
void screen_take_panel(void);

/* What `screen` reports from here on: the image at `path`, w x h. */
void screen_set_image(const char *path, uint32_t w, uint32_t h);

/* A console argument against the storage volume: absolute paths as given, the rest under it. */
void screen_resolve_path(const char *in, char *out, size_t len);

/* The lock everything that draws holds (recursive): for `image`, whose command is not in
 * video_player.cpp. */
void screen_lock(void);
void screen_unlock(void);

/* A clip's header, as media_probe() reports it: ESP_ERR_NOT_FOUND, ESP_ERR_NOT_SUPPORTED (not a
 * QuickTime file), or ESP_OK -- playable or not. */
esp_err_t screen_probe_clip(const char *path, media_info_t *out);

#ifdef __cplusplus
}
#endif
