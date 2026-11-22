/*
 * The board's happenings read back from their JSON. See happening.h.
 */
#include "happening.h"

#include <string.h>
#include "cJSON.h"

const char *const SCENE_THEN_NAMES[] = { "stay", "restore", "off" };
const char *const SCENE_BY_NAMES[] = { "clip", "time", "end", "replaced" };

static const char *const KINDS[] = {
    [HAPPENING_CLIP_STARTED] = "clip_started", [HAPPENING_CLIP_ENDED] = "clip_ended",
    [HAPPENING_SCENE_STARTED] = "scene_started", [HAPPENING_SCENE_ENDED] = "scene_ended", [HAPPENING_TOUCH] = "touch",
};

/* `v`'s index in names (n of them), or 0 for none of them */
static int index_in(const cJSON *v, const char *const *names, int n)
{
    for (int i = 0; cJSON_IsString(v) && i < n; i++) {
        if (names[i] != NULL && strcmp(v->valuestring, names[i]) == 0) {
            return i;
        }
    }
    return 0;
}

static double number(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItem(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}

static bool is_true(const cJSON *o, const char *key)
{
    return cJSON_IsTrue(cJSON_GetObjectItem(o, key));
}

bool happening_of(const cJSON *json, happening_t *out)
{
    memset(out, 0, sizeof(*out));
    const cJSON *seq = cJSON_GetObjectItem(json, "seq");
    out->seq = cJSON_IsNumber(seq) ? (uint16_t)seq->valueint : 0;
    out->kind = (happening_kind_t)index_in(cJSON_GetObjectItem(json, "event"), KINDS, sizeof(KINDS) / sizeof(KINDS[0]));
    switch (out->kind) {
    case HAPPENING_CLIP_STARTED:
        out->loop = is_true(json, "loop");
        out->plays = number(json, "plays");
        out->frames = number(json, "frames");
        out->fps = number(json, "fps");
        __attribute__((fallthrough));
    case HAPPENING_CLIP_ENDED:
        out->finished = is_true(json, "finished");
        out->path = cJSON_GetStringValue(cJSON_GetObjectItem(json, "path"));
        break;
    case HAPPENING_SCENE_STARTED:
    case HAPPENING_SCENE_ENDED:
        out->name = cJSON_GetStringValue(cJSON_GetObjectItem(json, "name"));
        out->slot = number(json, "slot");
        out->then = (scene_then_t)index_in(cJSON_GetObjectItem(json, "then"), SCENE_THEN_NAMES, 3);
        out->by = (scene_by_t)index_in(cJSON_GetObjectItem(json, "by"), SCENE_BY_NAMES, 4);
        out->until_clip_ends = is_true(json, "until_clip_ends");
        out->duration_s = number(json, "duration_s");
        break;
    case HAPPENING_TOUCH: {
        const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(json, "action"));
        out->up = action != NULL && strcmp(action, "up") == 0;
        out->x = number(json, "x");
        out->y = number(json, "y");
        break;
    }
    default:
        return false;
    }
    return true;
}
