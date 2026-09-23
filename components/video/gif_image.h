/*
 * GIFs on the panel, decoded with bitbank2's AnimatedGIF. Private to this component.
 *
 * A GifFile is a decoder plus the canvas it composites onto; gif_next() decodes one frame onto
 * it and paints what changed. Timing is not here: a still is painted once by `image show`, and
 * an animation is paced by the video player's task, like a clip.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

struct GifFile;

struct GifHeader {
    uint32_t width, height;     /* the canvas, which every frame is drawn onto */
    int32_t frames;
    int32_t duration_ms;        /* one pass, by the file's own delays */
    int loop_count;             /* NETSCAPE: -1 absent (play once), 0 forever, n repeats */
};

/*
 * How long a frame stays up, from the delay its file gives (0 when it gives none). Ten
 * milliseconds or less becomes 100, which is what Chrome and Firefox do: files that ask for
 * nothing, or for the shortest delay GIF can express, were made expecting it. The decoder
 * rounds only a zero up, so the player applies this, not the decoder.
 */
inline int gif_frame_delay_ms(int delay_ms)
{
    return delay_ms <= 10 ? 100 : delay_ms;
}

/* The first bytes of a file (at least 6) are a GIF signature. */
bool gif_is_gif(const uint8_t *buf, size_t len);

/*
 * The header and a walk over the frames, without decoding any pixels or touching the panel.
 * On a canvas too wide for the decoder, hdr->width and hdr->height are still filled in, so the
 * caller can name the size it refuses.
 */
esp_err_t gif_probe(const char *path, GifHeader *hdr, char *err, size_t err_len);

/* Open `path` for playing: the decoder, a black canvas and the buffers to paint from. */
GifFile *gif_open(const char *path, char *err, size_t err_len);

/*
 * Decode the next frame onto the canvas and paint the part of it that changed, the canvas's
 * top left at (x, y) on a panel that is already on. *drew is false when there was no frame
 * left to decode; *last is true after the final one. *delay_ms is how long the frame is to
 * stay up, as the file gives it (0 when it gives none). The paint times bracket the pixels
 * going out; *decode_us is the time inside the decoder.
 */
esp_err_t gif_next(GifFile *g, int x, int y, int *delay_ms, bool *drew, bool *last,
                   int64_t *decode_us, int64_t *paint_start, int64_t *paint_end,
                   char *err, size_t err_len);

/* Back to the first frame, onto a black canvas, so every pass looks like the first. */
void gif_rewind(GifFile *g);

void gif_close(GifFile *g);

/* A playing animation, on the video player's task: it owns `g` from here, even on failure. */
esp_err_t video_play_gif(GifFile *g, const GifHeader &hdr, const char *path);
