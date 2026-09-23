/*
 * PNG stills on the panel, decoded with libpng. Private to this component.
 *
 * Separate from image.cpp, and plain C, because libpng reports errors by longjmp: a jump out
 * of a C++ frame skips every destructor on the way, so the setjmp lives here among locals
 * that have none.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The first bytes of `buf` (at least 8) are a PNG signature. */
bool png_is_png(const uint8_t *buf, size_t len);

/*
 * Decode `path` and put it on the panel at (x, y). The panel must already be on: this draws
 * straight into it, so the caller wakes it -- black -- first.
 *
 * `err` gets a message on failure, from libpng where libpng is what failed; the caller prints
 * it. *w_out and *h_out get the size, and *mode the word for how it was decoded -- which of
 * the two ways depends on whether the file is interlaced, so only the decoder knows.
 */
esp_err_t png_show(const char *path, int x, int y, uint32_t *w_out, uint32_t *h_out,
                   const char **mode, char *err, size_t err_len);

/*
 * The header only: no pixels are decoded and the panel is not touched. *w_out and *h_out get
 * the size -- which is how a caller checks it fits before it wakes the panel -- and `desc`,
 * if given, a line like "PNG 200x150, 8-bit RGBA, non-interlaced". `err` as above.
 */
esp_err_t png_probe(const char *path, uint32_t *w_out, uint32_t *h_out, char *desc,
                    size_t desc_len, char *err, size_t err_len);

#ifdef __cplusplus
}
#endif
