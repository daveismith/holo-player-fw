/*
 * The console history, and the console task as the only thing that ever saves it. See
 * console_history.h.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/select.h>
#include <unistd.h>
#include "console_history.h"
#include "esp_log.h"
#include "esp_vfs_eventfd.h"
#include "linenoise/linenoise.h"

static const char *TAG = "history";

/* linenoise reads the file a line at a time into a buffer of the console's maximum command
 * length (CONSOLE_MAX_CMDLINE_LENGTH in console_settings.c); the copy does the same. */
#define LINE_BUF 256

static const char *s_path;
static char **s_lines;
static int s_max, s_len;
static bool s_unsaved;
static int s_request_fd = -1;   /* an eventfd: written by any task, read by the console task */

/* linenoiseHistoryAdd()'s rules, so the two lists stay the same: never the same line twice in
 * a row, and the oldest out when full. */
static bool record(const char *line)
{
    if (s_len > 0 && strcmp(s_lines[s_len - 1], line) == 0) {
        return false;
    }
    char *copy = strdup(line);
    if (copy == NULL) {
        return false;
    }
    if (s_len == s_max) {
        free(s_lines[0]);
        memmove(s_lines, s_lines + 1, sizeof(char *) * (s_max - 1));
        s_len--;
    }
    s_lines[s_len++] = copy;
    return true;
}

/*
 * linenoise's read: wait for a keystroke, as a plain read() would, but also for a save
 * request, which is carried out here and the wait resumed. The line being typed is untouched;
 * the request costs the typist nothing but the write itself.
 */
static ssize_t read_or_save(int fd, void *buf, size_t len)
{
    for (;;) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        FD_SET(s_request_fd, &fds);
        if (select(MAX(fd, s_request_fd) + 1, &fds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (FD_ISSET(s_request_fd, &fds)) {
            uint64_t n;
            read(s_request_fd, &n, sizeof(n));
            console_history_save();
        }
        if (FD_ISSET(fd, &fds)) {
            return read(fd, buf, len);
        }
    }
}

esp_err_t console_history_init(const char *path, int max_lines)
{
    s_lines = calloc(max_lines, sizeof(char *));
    if (s_lines == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_path = path;
    s_max = max_lines;

    FILE *f = fopen(path, "r");
    if (f != NULL) {
        char buf[LINE_BUF];
        while (fgets(buf, sizeof(buf), f) != NULL) {
            /* as linenoiseHistoryLoad(): cut at the first CR, or failing that the first LF */
            char *p = strchr(buf, '\r');
            if (p == NULL) {
                p = strchr(buf, '\n');
            }
            if (p != NULL) {
                *p = '\0';
            }
            record(buf);
        }
        fclose(f);
    }

    /* Without the eventfd, requests go unheard but everything else stands: the console task
     * still saves after each command run with no clip playing. */
    const esp_vfs_eventfd_config_t config = ESP_VFS_EVENTD_CONFIG_DEFAULT();
    const esp_err_t err = esp_vfs_eventfd_register(&config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {   /* INVALID_STATE: registered already */
        ESP_LOGW(TAG, "eventfd: %s; saves wait for the next command", esp_err_to_name(err));
        return ESP_OK;
    }
    s_request_fd = eventfd(0, 0);
    if (s_request_fd < 0) {
        ESP_LOGW(TAG, "eventfd: errno %d; saves wait for the next command", errno);
        return ESP_OK;
    }
    linenoiseSetReadFunction(read_or_save);
    return ESP_OK;
}

void console_history_add(const char *line)
{
    if (linenoiseHistoryAdd(line) && s_lines != NULL && record(line)) {
        s_unsaved = true;
    }
}

void console_history_save(void)
{
    if (s_lines == NULL || !s_unsaved) {
        return;
    }
    /* the format linenoiseHistorySave() writes: one line each */
    FILE *f = fopen(s_path, "w");
    if (f == NULL) {
        return;   /* still unsaved; tried again next time */
    }
    for (int i = 0; i < s_len; i++) {
        fprintf(f, "%s\n", s_lines[i]);
    }
    fclose(f);
    s_unsaved = false;
}

void console_history_request_save(void)
{
    if (s_request_fd >= 0) {
        const uint64_t one = 1;
        write(s_request_fd, &one, sizeof(one));
    }
}
