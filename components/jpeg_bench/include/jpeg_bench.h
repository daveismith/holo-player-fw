#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* `jpeg bench` and `jpeg show`. Paths are relative to `base_path` (the storage volume). */
void register_jpeg_bench(const char *base_path);

#ifdef __cplusplus
}
#endif
