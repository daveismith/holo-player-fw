/*
 * Still images on the panel: the `image` console command. Private to this component --
 * register_video_commands() registers it along with `video` and `screen`.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void register_image_command(void);

/* `image show`, for screen_show_file(): 0, or 1 having printed why, or IMAGE_NOT_STOPPED when what
 * was playing did not stop within 2 s (screen_take_panel(): nothing drawn, the screen off). An
 * animated GIF plays `plays` times (0 forever, -1 as the file says). */
#define IMAGE_NOT_STOPPED 2
int image_show_file(const char *path, int plays);

#ifdef __cplusplus
}
#endif
