/*
 * The holo on Cyphal: the lists (manual/reference/cyphal.md). r2.holo.ListScenes, the saved scenes one a call;
 * uavcan.file.List and uavcan.file.GetInfo, the standard services on their fixed IDs (406, 405), over the board's
 * files; r2.holo.MediaInfo, a clip's or an image's details. Each is read only: nothing on Cyphal writes, moves or
 * deletes a file or a scene (the plan's E9).
 *
 * ListScenes counts the saved scenes by their stored lengths alone and reads and parses only the one asked for
 * (scene_list_entry()), in GET /api/v1/scenes' order; on the worker.
 *
 * The file services see the storage volume as `fs ls` does, its root the volume's ("/" is /data on the board), with
 * the HTTP API's path rules (fs_path(): at most 159 bytes, no "..", no empty part; the volume's own root in front is
 * taken too). List gives a directory's entries one a call by index, in byte order of their names, directories
 * included, each with "/" after its name, as uavcan.file.Path's convention has it. That is the order littlefs keeps a
 * directory in (its SPEC: each metadata pair's entries in order of their names, compared as bytes) and so the order
 * readdir() gives, `fs ls`'s and GET /api/v1/media's walk: a call walks readdir() to its index, names only (no stat,
 * nothing allocated, nothing sorted). An index past the last, a path that isn't a directory and a path refused give
 * the empty name, which is all the type can say; with the board's queue full there is no answer at all (an empty name
 * would read as the end of the list, and List carries no count), and the client asks again. GetInfo is one stat: the
 * size, the time of the last change when the clock was set as it was made (0 otherwise), file or directory, readable,
 * and never writable (no Cyphal service writes). Both are slow handlers, on the second worker: either waits for the
 * volume's lock, which an HTTP upload holds through each write (measured 2026-11-21 during a 1 MB upload: List of the
 * root's last entry 82 ms median, 528 ms at most, GetInfo 33 and 60 ms; on the worker they held Status 1.36 s apart,
 * against 1.03 s with the upload alone). At rest a List costs about 0.1 ms an entry before its index on the board, a
 * GetInfo about 1.5 ms.
 *
 * MediaInfo runs GET /api/v1/media/info's lookup (api_media_lookup(): its checks, its probe, its words), which reads
 * a JPEG whole and a GIF's every frame header: a slow handler, on the second worker. A file the screen can't show is
 * NOT_PLAYABLE with why, in the words of the HTTP API's 422, and is still described when it is a clip or an image of
 * a kind the board knows.
 */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "board_api.h"
#include "esp_log.h"
#include "fs_ops.h"
#include "holo_cyphal.h"
#include "holo_cyphal_internal.h"
#include "holo_cyphal_util.h"
#include "r2/holo/ListScenes_0_1.h"
#include "r2/holo/MediaInfo_0_1.h"
#include "r2/holo/ports.h"
#include "scenes.h"
#include "uavcan/file/GetInfo_0_2.h"
#include "uavcan/file/List_0_2.h"
#include "video_player.h"

static const char *TAG = "holo_cyphal";

_Static_assert(sizeof(uavcan_file_List_Request_0_2) <= CYPHAL_APP_REQUEST_MAX &&
               sizeof(uavcan_file_GetInfo_Request_0_2) <= CYPHAL_APP_REQUEST_MAX &&
               sizeof(r2_holo_MediaInfo_Request_0_1) <= CYPHAL_APP_REQUEST_MAX &&
               sizeof(r2_holo_ListScenes_Request_0_1) <= CYPHAL_APP_REQUEST_MAX, "a request fits a job slot");
_Static_assert(r2_holo_ListScenes_Response_0_1_name_ARRAY_CAPACITY_ == SCENE_NAME_MAX, "a scene's name fits");
_Static_assert((int)MEDIA_CLIP == 0 && (int)MEDIA_IMAGE == 1 && (int)MEDIA_ANIMATION == 2 && (int)MEDIA_MOV == 0 &&
               (int)MEDIA_PNG == 1 && (int)MEDIA_JPEG == 2 && (int)MEDIA_GIF == 3,
               "holo_media_info() numbers the kinds and formats as video_player.h does");

#define BUSY_WORDS "busy: the board is still working through earlier requests"

/* ------------------------------------------------------------------ ListScenes, on the worker */

static size_t list_scenes_respond(const r2_holo_ListScenes_Response_0_1 *r, uint8_t *out, size_t room)
{
    return r2_holo_ListScenes_Response_0_1_serialize_(r, out, &room) < 0 ? 0 : room;
}

bool holo_list_scenes_decode(const uint8_t *payload, size_t size, void *in)
{
    return r2_holo_ListScenes_Request_0_1_deserialize_(in, payload, &size) >= 0;
}

size_t holo_list_scenes_work(const void *in, uint8_t from, uint8_t *out, size_t room)
{
    (void)from;
    const r2_holo_ListScenes_Request_0_1 *q = in;
    char name[SCENE_NAME_MAX + 1];
    int slot = 0;
    const int count = scene_list_entry(q->index, name, sizeof(name), &slot);
    r2_holo_ListScenes_Response_0_1 r;
    holo_list_scenes(count, name, slot, &r);
    return list_scenes_respond(&r, out, room);
}

size_t holo_list_scenes_busy(uint8_t *out, size_t room)
{
    r2_holo_ListScenes_Response_0_1 r;
    memset(&r, 0, sizeof(r));
    r.result.code = r2_holo_Error_0_1_BUSY;
    holo_error_message(&r.result, BUSY_WORDS);
    return list_scenes_respond(&r, out, room);
}

/* ------------------------------------------------------------------ uavcan.file.List, on the worker */

/* The longest path a request carries (uavcan.file.Path's 255 bytes) and its NUL: so that one too long for the board
 * reaches fs_path(), which says so in its own words, rather than failing to fit before it */
#define REQ_PATH_MAX (uavcan_file_Path_2_0_path_ARRAY_CAPACITY_ + 1)

/* A path from a request as the volume's absolute path, checked by fs_path(): false for one refused */
static bool abs_of(const uavcan_file_Path_2_0 *p, char *abs, size_t len)
{
    char rel[REQ_PATH_MAX];
    char why[112];
    return holo_file_path(p, rel, sizeof(rel)) && fs_path(rel, abs, len, why, sizeof(why)) == 0;
}

/* The directory's index-th entry, in readdir()'s order, which is byte order of the names (littlefs keeps a directory
 * so): its name into `name` and whether it is a directory. False for none (past the last, not a directory) */
static bool nth_entry(const char *abs, uint32_t index, char name[FS_NAME_MAX + 1], bool *dir)
{
    DIR *d = opendir(abs);
    if (d == NULL) {
        return false;
    }
    bool found = false;
    uint32_t i = 0;
    const struct dirent *de;
    while (!found && (de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0 || strlen(de->d_name) > FS_NAME_MAX) {
            continue;
        }
        if (i++ == index) {
            strlcpy(name, de->d_name, FS_NAME_MAX + 1);
            *dir = de->d_type == DT_DIR;
            found = true;
        }
    }
    closedir(d);
    return found;
}

static bool file_list_decode(const uint8_t *payload, size_t size, void *in)
{
    return uavcan_file_List_Request_0_2_deserialize_(in, payload, &size) >= 0;
}

static size_t file_list_respond(const uavcan_file_List_Response_0_2 *r, uint8_t *out, size_t room)
{
    return uavcan_file_List_Response_0_2_serialize_(r, out, &room) < 0 ? 0 : room;
}

static size_t file_list_work(const void *in, uint8_t from, uint8_t *out, size_t room)
{
    (void)from;
    const uavcan_file_List_Request_0_2 *q = in;
    static uavcan_file_List_Response_0_2 r;     /* 0.3 KB, the worker's alone: off its stack */
    memset(&r, 0, sizeof(r));
    char abs[FS_ABS_MAX];
    char name[FS_NAME_MAX + 1];
    bool dir;
    if (abs_of(&q->directory_path, abs, sizeof(abs)) && nth_entry(abs, q->entry_index, name, &dir)) {
        holo_file_entry(name, dir, &r.entry_base_name);
    }
    return file_list_respond(&r, out, room);
}

/* The type has no way to say busy, and its empty name is the end of the list: no answer, and the client asks again */
static size_t file_list_busy(uint8_t *out, size_t room)
{
    (void)out;
    (void)room;
    return 0;
}

/* ------------------------------------------------------------------ uavcan.file.GetInfo, on the worker */

static bool file_info_decode(const uint8_t *payload, size_t size, void *in)
{
    return uavcan_file_GetInfo_Request_0_2_deserialize_(in, payload, &size) >= 0;
}

static size_t file_info_respond(const uavcan_file_GetInfo_Response_0_2 *r, uint8_t *out, size_t room)
{
    return uavcan_file_GetInfo_Response_0_2_serialize_(r, out, &room) < 0 ? 0 : room;
}

static size_t file_info_work(const void *in, uint8_t from, uint8_t *out, size_t room)
{
    (void)from;
    const uavcan_file_GetInfo_Request_0_2 *q = in;
    uavcan_file_GetInfo_Response_0_2 r;
    memset(&r, 0, sizeof(r));
    char abs[FS_ABS_MAX];
    fs_entry_t e;
    const int err = !abs_of(&q->path, abs, sizeof(abs)) ? EINVAL : fs_stat_entry(abs, &e);
    r._error.value = holo_file_error(err);
    if (err == 0) {
        r.size = e.dir ? 0 : e.size;
        r.unix_timestamp_of_last_modification = e.mtime > 0 ? (uint64_t)e.mtime : 0;
        r.is_file_not_directory = !e.dir;
        r.is_readable = true;
        r.is_writeable = false;     /* nothing on Cyphal writes a file */
    }
    return file_info_respond(&r, out, room);
}

static size_t file_info_busy(uint8_t *out, size_t room)
{
    uavcan_file_GetInfo_Response_0_2 r;
    memset(&r, 0, sizeof(r));
    r._error.value = uavcan_file_Error_1_0_IO_ERROR;    /* the type has no busy: an error that asks for a retry */
    return file_info_respond(&r, out, room);
}

/* ------------------------------------------------------------------ MediaInfo, on the second worker */

bool holo_media_info_decode(const uint8_t *payload, size_t size, void *in)
{
    return r2_holo_MediaInfo_Request_0_1_deserialize_(in, payload, &size) >= 0;
}

static size_t media_info_respond(const r2_holo_MediaInfo_Response_0_1 *r, uint8_t *out, size_t room)
{
    return r2_holo_MediaInfo_Response_0_1_serialize_(r, out, &room) < 0 ? 0 : room;
}

/* GET /api/v1/media/info's lookup, with its words: Error's code, and the details into r */
static uint8_t media_describe(const r2_holo_MediaInfo_Request_0_1 *q, r2_holo_MediaInfo_Response_0_1 *r, char *why,
                              size_t why_len)
{
    char rel[REQ_PATH_MAX];
    char path[REQ_PATH_MAX];
    if (!holo_c_string(q->path.path.elements, q->path.path.count, rel, sizeof(rel))) {
        snprintf(why, why_len, "control characters and \\ are not allowed in a path");    /* fs_path()'s words */
        return r2_holo_Error_0_1_BAD_REQUEST;
    }
    media_info_t m;
    const char *error;
    const int status = api_media_lookup(rel, path, sizeof(path), &m, &error, why, why_len);
    if (status == 200 || status == 422) {
        const holo_media_in_t in = {
            .kind = status == 200 ? (int)m.kind : -1, .format = status == 200 ? (int)m.format : -1,
            .width = m.width, .height = m.height, .frames = m.frames, .fps = m.fps, .duration_s = m.duration_s,
        };
        holo_media_info(&in, r);
    }
    if (status == 200) {
        return m.playable ? r2_holo_Error_0_1_OK : r2_holo_Error_0_1_NOT_PLAYABLE;
    }
    return (uint8_t)api_code_of(status, error);
}

size_t holo_media_info_work(const void *in, uint8_t from, uint8_t *out, size_t room)
{
    r2_holo_MediaInfo_Response_0_1 r;
    memset(&r, 0, sizeof(r));
    char why[REQ_PATH_MAX + sizeof(((media_info_t *)0)->why) + 2] = "";    /* a path and the probe's why; cut to
                                                                            * 64 bytes in the Error */
    r.result.code = media_describe(in, &r, why, sizeof(why));
    if (r.result.code != r2_holo_Error_0_1_OK) {
        ESP_LOGI(TAG, "node %u's media info: %s", from, why);
        holo_error_message(&r.result, why);
    }
    return media_info_respond(&r, out, room);
}

size_t holo_media_info_busy(uint8_t *out, size_t room)
{
    r2_holo_MediaInfo_Response_0_1 r;
    memset(&r, 0, sizeof(r));
    r.result.code = r2_holo_Error_0_1_BUSY;
    holo_error_message(&r.result, BUSY_WORDS);
    return media_info_respond(&r, out, room);
}

/* ------------------------------------------------------------------ the handlers */

const cyphal_handler_t HOLO_CYPHAL_FIXED[] = {
    { .fixed_id = uavcan_file_List_0_2_FIXED_PORT_ID_, .extent = uavcan_file_List_Request_0_2_EXTENT_BYTES_,
      .decode = file_list_decode, .in_size = sizeof(uavcan_file_List_Request_0_2), .work = file_list_work,
      .busy = file_list_busy, .slow = true },
    { .fixed_id = uavcan_file_GetInfo_0_2_FIXED_PORT_ID_, .extent = uavcan_file_GetInfo_Request_0_2_EXTENT_BYTES_,
      .decode = file_info_decode, .in_size = sizeof(uavcan_file_GetInfo_Request_0_2), .work = file_info_work,
      .busy = file_info_busy, .slow = true },
};
const size_t HOLO_CYPHAL_FIXED_COUNT = sizeof(HOLO_CYPHAL_FIXED) / sizeof(HOLO_CYPHAL_FIXED[0]);
