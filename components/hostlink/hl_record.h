/*
 * A happening (happening.h) as the host link's 8-byte native event record (manual/reference/host-protocol.md,
 * "Native frames": EVENTS_GET's records and the EVENT frame's): kind, seq (low byte first), then the kind's own
 * bytes. Plain C, no ESP-IDF: tests/host builds it.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "happening.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Into r (all 8 bytes written, its seq even when false): false for a happening that has no record */
bool hl_record(const happening_t *h, uint8_t r[8]);

#ifdef __cplusplus
}
#endif
