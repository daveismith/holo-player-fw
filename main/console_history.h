/*
 * The console history, and the console task as the only thing that ever saves it.
 *
 * Other tasks can ask for a save (the video player does, when the screen turns off): the
 * console task spends its idle time inside linenoise waiting for a keystroke, and the wait
 * also watches for that request, so the save happens there -- between keystrokes, in the
 * console task.
 *
 * The file is written from a copy of the history rather than with linenoiseHistorySave():
 * while a line is being edited linenoise's own list holds a placeholder for it, which would
 * be saved as if it had been run.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load the copy from `path`, as linenoiseHistoryLoad() loaded linenoise's list from it at
 * boot (`max_lines` must match linenoiseHistorySetMaxLen()), and have linenoise's wait for
 * input also answer save requests. Call from the console task, after the terminal probe. */
esp_err_t console_history_init(const char *path, int max_lines);
/* Console task: add a line to linenoise's history and to the copy. */
void console_history_add(const char *line);
/* Console task: write the copy to the file, if anything is unsaved. */
void console_history_save(void);
/* Any task: have the console task save as soon as it is next waiting for input (at once, if
 * it is waiting now). */
void console_history_request_save(void);

#ifdef __cplusplus
}
#endif
