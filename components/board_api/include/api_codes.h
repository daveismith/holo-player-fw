/*
 * The status codes a request is answered with where there is no HTTP status: the host link's native
 * frames (manual/reference/host-protocol.md) and the Cyphal node's r2.holo.Error (manual/reference/cyphal.md)
 * number them the same. One mapping from a route's reply to them, for both. Plain C, no ESP-IDF:
 * tests/host builds it.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    API_CODE_OK, API_CODE_BAD_REQUEST, API_CODE_NOT_FOUND, API_CODE_NOT_PLAYABLE, API_CODE_NOT_READY,
    API_CODE_BUSY, API_CODE_FAILED,
} api_code_t;

/* A route's reply (its HTTP status and its JSON `error`, NULL for none) as the code: 2xx OK; bad_request and
 * bad_path BAD_REQUEST; not_found, unknown_scene and not_running NOT_FOUND; not_playable; not_ready; busy;
 * anything else FAILED */
api_code_t api_code_of(int status, const char *error);

#ifdef __cplusplus
}
#endif
