/*
 * See cyphal_util.h. Plain C only: tests/host builds this file on the computer.
 */
#include <stdio.h>
#include <string.h>
#include "cyphal_util.h"

#define CRC64WE_POLY 0x42F0E1EBA9EA3693ull

uint64_t cyphal_crc64we(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint64_t crc = ~0ull;
    while (len--) {
        crc ^= (uint64_t)*p++ << 56;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & (1ull << 63)) ? (crc << 1) ^ CRC64WE_POLY : crc << 1;
        }
    }
    return ~crc;
}

uint64_t cyphal_pseudo_id(const uint8_t unique_id[CYPHAL_UNIQUE_ID_SIZE])
{
    return cyphal_crc64we(unique_id, CYPHAL_UNIQUE_ID_SIZE) & CYPHAL_PSEUDO_ID_MASK;
}

void cyphal_unique_id(uint8_t out[CYPHAL_UNIQUE_ID_SIZE], const char prefix[CYPHAL_UID_PREFIX_SIZE],
                      const uint8_t mac[6])
{
    memcpy(out, prefix, CYPHAL_UID_PREFIX_SIZE);
    out[8] = out[9] = 0;
    memcpy(&out[10], mac, 6);
}

bool cyphal_settings_equal(const cyphal_settings_t *a, const cyphal_settings_t *b)
{
    return a->node_id == b->node_id && a->bitrate == b->bitrate &&
           strncmp(a->name, b->name, sizeof(a->name)) == 0;
}

void cyphal_listen_start(cyphal_listen_t *l, int64_t now_us, int64_t length_us)
{
    l->until_us = now_us + length_us;
    l->checked_us = now_us;
}

void cyphal_listen_frame(cyphal_listen_t *l, int64_t stamp_us)
{
    if (stamp_us > l->checked_us) {
        l->checked_us = stamp_us;
    }
}

void cyphal_listen_empty(cyphal_listen_t *l, int64_t read_us)
{
    cyphal_listen_frame(l, read_us - CYPHAL_RX_SLACK_US);
}

bool cyphal_listen_over(const cyphal_listen_t *l)
{
    return l->checked_us >= l->until_us;
}

int64_t cyphal_listen_wake_us(const cyphal_listen_t *l)
{
    return l->until_us + CYPHAL_RX_SLACK_US;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_hex(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f');
}

bool cyphal_name_valid(const char *name, char *why, size_t why_len)
{
    const size_t n = strlen(name);
    if (n == 0 || n > CYPHAL_NAME_MAX) {
        snprintf(why, why_len, "a name is 1 to %d characters, and this is %u", CYPHAL_NAME_MAX, (unsigned)n);
        return false;
    }
    const char *part = name;    /* where the current part between dots starts */
    for (const char *p = name; ; p++) {
        const char c = *p;
        if (c == '.' || c == '\0') {
            if (p == part) {
                snprintf(why, why_len, "a part between dots is empty");
                return false;
            }
            if (c == '\0') {
                return true;
            }
            part = p + 1;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || is_digit(c) || c == '_')) {
            if (c > ' ' && c < 0x7f) {
                snprintf(why, why_len, "`%c` is not allowed: only lower-case letters, digits, `_` and `.`", c);
            } else {
                snprintf(why, why_len, "character 0x%02X is not allowed: only lower-case letters, digits, `_` and `.`",
                         (unsigned)(unsigned char)c);
            }
            return false;
        }
        if (p == part && is_digit(c)) {
            snprintf(why, why_len, "a part can't start with a digit: `%.*s`", (int)strcspn(part, "."), part);
            return false;
        }
    }
}

int cyphal_frame_source(uint32_t can_id, bool ext, bool rtr, uint8_t len)
{
    /* Cyphal/CAN v1.0 frames are 29-bit data frames ending in a tail byte */
    if (!ext || rtr || len == 0 || (can_id & (1u << 23))) {     /* bit 23 is reserved, zero */
        return -1;
    }
    const bool service = can_id & (1u << 25);
    if (!service && (can_id & ((1u << 24) | (1u << 7)))) {     /* anonymous, or reserved bit 7 set */
        return -1;
    }
    return (int)(can_id & 0x7F);
}

/* Decimal digits at *p, advancing past them: false if there are none or too many */
static bool take_number(const char **p, unsigned *out)
{
    unsigned v = 0;
    const char *s = *p;
    while (is_digit(**p) && *p - s < 6) {
        v = v * 10 + (unsigned)(**p - '0');
        (*p)++;
    }
    *out = v;
    return *p > s && !is_digit(**p);
}

static uint8_t clamp8(unsigned v)
{
    return v > 255 ? 255 : (uint8_t)v;
}

/* Up to 16 hex digits from `s`, which must hold at least 4 and then end, or go on with `-` */
static bool take_hash(const char *s, uint64_t *out)
{
    size_t n = 0;
    uint64_t v = 0;
    while (is_hex(s[n])) {
        if (n < 16) {
            v = v << 4 | (uint64_t)(is_digit(s[n]) ? s[n] - '0' : s[n] - 'a' + 10);
        }
        n++;
    }
    if (n < 4 || (s[n] != '\0' && s[n] != '-')) {
        return false;
    }
    *out = v;
    return true;
}

void cyphal_software_version(const char *version, uint8_t *major, uint8_t *minor, uint64_t *vcs_revision)
{
    *major = *minor = 0;
    *vcs_revision = 0;
    if (version == NULL) {
        return;
    }
    /* A release is the tag alone: vMAJOR.MINOR.PATCH, or vMAJOR.MINOR.PATCH-rcN */
    const char *p = version;
    unsigned a, b, c, rc;
    if (*p++ == 'v' && take_number(&p, &a) && *p++ == '.' && take_number(&p, &b) && *p++ == '.' &&
        take_number(&p, &c)) {
        if (*p == '\0' || (strncmp(p, "-rc", 3) == 0 && (p += 3, take_number(&p, &rc)) && *p == '\0')) {
            *major = clamp8(a);
            *minor = clamp8(b);
            return;
        }
    }
    /* Between releases: `<tag>-<n>-g<hash>[-dirty]`; with no tag at all, a bare `<hash>[-dirty]` */
    for (const char *g = strstr(version, "-g"); g != NULL; g = strstr(g + 2, "-g")) {
        if (take_hash(g + 2, vcs_revision)) {
            return;
        }
    }
    take_hash(version, vcs_revision);
}
