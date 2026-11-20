/*
 * The parts of the Cyphal node with no hardware in them: plain C, no ESP-IDF, so that
 * tests/host builds and tests them on the computer (`make unit-test`).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CYPHAL_UNIQUE_ID_SIZE   16
#define CYPHAL_UID_PREFIX_SIZE  8      /* the constant part of the unique ID: names the firmware */
#define CYPHAL_NAME_MAX         50     /* uavcan.node.GetInfo.1.0's name: uint8[<=50] */
#define CYPHAL_PSEUDO_ID_MASK   0xFFFFFFFFFFFFull   /* uavcan.pnp.NodeIDAllocationData.1.0: truncated uint48 */
/* A read that finds the receive queue empty proves that every frame stamped this long before it
 * was read: the ISR stamps a frame a moment before it queues it, maybe on the other core */
#define CYPHAL_RX_SLACK_US      1000

typedef struct {
    uint8_t node_id;                    /* 0-127, or CYPHAL_NODE_ID_AUTO (cyphal.h) */
    char name[CYPHAL_NAME_MAX + 1];     /* bytes after the terminator are not part of it */
    uint32_t bitrate;                   /* 125000, 250000, 500000 or 1000000 */
} cyphal_settings_t;

/* The same settings: the name compared as a string, whatever follows its terminator */
bool cyphal_settings_equal(const cyphal_settings_t *a, const cyphal_settings_t *b);

/* A fixed node-ID's listen before going online (D9), in esp_timer microseconds. It is over once
 * every frame stamped before its end has been checked, not merely once the time has passed: a
 * node task held off past the end (a flash write stalls both cores) may still have frames from
 * before it in the receive queue, and one of them may carry its own node-ID. The queue is in
 * stamp order, so a frame checked whose stamp is at or after the end proves it, as does a read
 * that finds the queue empty CYPHAL_RX_SLACK_US after the end; a busy bus can't hold it back. */
typedef struct {
    int64_t until_us;       /* the end */
    int64_t checked_us;     /* every frame stamped before this has been checked */
} cyphal_listen_t;

void cyphal_listen_start(cyphal_listen_t *l, int64_t now_us, int64_t length_us);
/* A frame stamped stamp_us (canbus_frame_t.timestamp_us) has been checked */
void cyphal_listen_frame(cyphal_listen_t *l, int64_t stamp_us);
/* A read begun at read_us found the receive queue empty */
void cyphal_listen_empty(cyphal_listen_t *l, int64_t read_us);
bool cyphal_listen_over(const cyphal_listen_t *l);
/* When an empty read would end it: the time to look again on a quiet bus */
int64_t cyphal_listen_wake_us(const cyphal_listen_t *l);

/* CRC-64/WE: poly 0x42F0E1EBA9EA3693, init and xorout all ones, not reflected. Its check value,
 * over "123456789", is 0x62EC59E3F1A4F00A. */
uint64_t cyphal_crc64we(const void *data, size_t len);

/* The pseudo-ID plug-and-play sends for a unique ID: the low 48 bits of its CRC-64/WE */
uint64_t cyphal_pseudo_id(const uint8_t unique_id[CYPHAL_UNIQUE_ID_SIZE]);

/* The unique ID (manual/use/cyphal.md, "The unique ID"): bytes 0-7 the prefix, 8-9 zero, 10-15
 * the chip's base MAC address. Changing this changes every board's node-ID. */
void cyphal_unique_id(uint8_t out[CYPHAL_UNIQUE_ID_SIZE], const char prefix[CYPHAL_UID_PREFIX_SIZE],
                      const uint8_t mac[6]);

/* Whether `name` is a node name: 1 to 50 characters of a-z, 0-9, `_` and `.`, with no empty part
 * between dots and no part starting with a digit. False with the reason in `why`. */
bool cyphal_name_valid(const char *name, char *why, size_t why_len);

/* The source node-ID of a Cyphal/CAN v1.0 frame, or -1 when it has none to compare: an anonymous
 * message (its low bits are a pseudo-ID), or not a Cyphal frame at all (11-bit, remote, empty,
 * or a reserved bit set). */
int cyphal_frame_source(uint32_t can_id, bool ext, bool rtr, uint8_t len);

/* uavcan.node.GetInfo's software version and VCS revision from the firmware's version, which is
 * `git describe --always --tags --dirty`: a release (vMAJOR.MINOR.PATCH, or a -rcN pre-release)
 * gives its major and minor, anything else 0.0; a commit hash in it (`-g<hash>`, or a bare hash)
 * gives the VCS revision, otherwise 0. */
void cyphal_software_version(const char *version, uint8_t *major, uint8_t *minor, uint64_t *vcs_revision);

#ifdef __cplusplus
}
#endif
