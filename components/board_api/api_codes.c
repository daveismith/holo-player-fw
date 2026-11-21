/*
 * A route's reply as the host link's and Cyphal's status code. See api_codes.h.
 */
#include "api_codes.h"

#include <stddef.h>
#include <string.h>

api_code_t api_code_of(int status, const char *error)
{
    if (status >= 200 && status < 300) {
        return API_CODE_OK;
    }
    static const struct { const char *error; api_code_t code; } CODES[] = {
        { "bad_request", API_CODE_BAD_REQUEST }, { "bad_path", API_CODE_BAD_REQUEST },
        { "not_found", API_CODE_NOT_FOUND }, { "unknown_scene", API_CODE_NOT_FOUND },
        { "not_running", API_CODE_NOT_FOUND }, { "not_playable", API_CODE_NOT_PLAYABLE },
        { "not_ready", API_CODE_NOT_READY }, { "busy", API_CODE_BUSY },
    };
    for (size_t i = 0; error != NULL && i < sizeof(CODES) / sizeof(CODES[0]); i++) {
        if (strcmp(error, CODES[i].error) == 0) {
            return CODES[i].code;
        }
    }
    return API_CODE_FAILED;
}
