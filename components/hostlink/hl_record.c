/*
 * A happening as the host link's native event record. See hl_record.h.
 */
#include "hl_record.h"

#include <string.h>

bool hl_record(const happening_t *h, uint8_t r[8])
{
    memset(r, 0, 8);
    r[1] = (uint8_t)(h->seq & 0xFF);
    r[2] = (uint8_t)(h->seq >> 8);
    switch (h->kind) {
    case HAPPENING_CLIP_ENDED:
        r[0] = 1;
        r[3] = h->finished;
        break;
    case HAPPENING_SCENE_ENDED:
        r[0] = 2;
        r[3] = (uint8_t)(int)h->slot;
        r[4] = (uint8_t)h->then;
        r[5] = (uint8_t)h->by;
        break;
    case HAPPENING_CLIP_STARTED: {
        r[0] = 4;
        r[3] = h->loop;
        r[4] = (uint8_t)(h->plays > 255 ? 255 : h->plays);
        const uint16_t f = (uint16_t)(h->frames > 65535 ? 65535 : h->frames);
        r[5] = (uint8_t)(f & 0xFF);
        r[6] = (uint8_t)(f >> 8);
        r[7] = (uint8_t)(h->fps > 255 ? 255 : h->fps + 0.5);
        break;
    }
    case HAPPENING_SCENE_STARTED: {
        r[0] = 5;
        r[3] = (uint8_t)(int)h->slot;
        r[4] = (uint8_t)h->then;
        r[5] = h->until_clip_ends;
        /* Whole seconds, rounded up; none is 0 */
        const double d = h->duration_s + 0.999;
        const uint16_t secs = (uint16_t)(d > 65535 ? 65535 : d);
        r[6] = (uint8_t)(secs & 0xFF);
        r[7] = (uint8_t)(secs >> 8);
        break;
    }
    case HAPPENING_TOUCH:
        r[0] = 3;
        r[3] = h->up;
        r[4] = (uint8_t)h->x;
        r[5] = (uint8_t)h->y;
        break;
    default:
        return false;
    }
    return true;
}
