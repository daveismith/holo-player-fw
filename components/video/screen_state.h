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

#ifdef __cplusplus
}
#endif
