/*
 * JPEGDEC (bitbank2), the way the Flash_PNG sketch used it: openRAM(), big-endian RGB565, and
 * a draw callback copying each block of pixels into a frame.
 */
#include <string.h>
#include "sdkconfig.h"
#include "JPEGDEC.h"
#include "jpeg_bench_priv.h"

namespace {

struct Target {
    uint16_t *fb;
    int w, h;
};

/* ~20 KB of decoder state: static, so it lives in internal RAM and costs no allocation. */
JPEGDEC s_jpeg;

/*
 * Blocks arrive MCU-sized: for a 120-pixel-wide 4:2:0 image that is 16 pixels at a time,
 * so the last block in a row is 8 pixels of image and 8 of padding (iWidthUsed says which).
 * The sketch sized its buffers to the padded 128 x 128 instead of clipping.
 */
int draw_cb(JPEGDRAW *d)
{
    const Target *t = static_cast<const Target *>(d->pUser);
    const int used = (d->iWidthUsed > 0 && d->iWidthUsed < d->iWidth) ? d->iWidthUsed : d->iWidth;
    for (int row = 0; row < d->iHeight; row++) {
        const int y = d->y + row;
        if (y >= t->h) {
            break;
        }
        int n = used;
        if (d->x + n > t->w) {
            n = t->w - d->x;
        }
        if (n > 0) {
            memcpy(t->fb + y * t->w + d->x, d->pPixels + row * d->iWidth, n * sizeof(uint16_t));
        }
    }
    return 1;
}

}  // namespace

extern "C" int bench_jpegdec_decode(const uint8_t *jpg, size_t len, uint16_t *fb, int w, int h)
{
    Target t = { fb, w, h };
    if (!s_jpeg.openRAM(const_cast<uint8_t *>(jpg), static_cast<int>(len), draw_cb)) {
        return -1;
    }
    s_jpeg.setPixelType(RGB565_BIG_ENDIAN);
    s_jpeg.setUserPointer(&t);
    const int ok = s_jpeg.decode(0, 0, 0);
    const int err = s_jpeg.getLastError();
    s_jpeg.close();
    return ok ? 0 : (err ? err : -2);
}

extern "C" const char *bench_jpegdec_label(void)
{
#if CONFIG_JPEGDEC_S3_SIMD
    return "JPEGDEC " JPEGDEC_VERSION_STR " (S3 SIMD)";
#else
    return "JPEGDEC " JPEGDEC_VERSION_STR " (no SIMD)";
#endif
}
