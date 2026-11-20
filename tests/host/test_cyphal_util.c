/*
 * Host-built tests for components/cyphal/cyphal_util.c: the parts of the Cyphal node with no
 * hardware in them. `make unit-test` builds and runs them with the computer's cc; no ESP-IDF.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "cyphal_util.h"

static int s_failed, s_checks;

#define CHECK(cond)                                                                                 \
    do {                                                                                            \
        s_checks++;                                                                                 \
        if (!(cond)) {                                                                              \
            s_failed++;                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                  \
        }                                                                                           \
    } while (0)

static void test_crc64we(void)
{
    /* The catalogue's check value, and the empty message (init ^ xorout) */
    CHECK(cyphal_crc64we("123456789", 9) == 0x62EC59E3F1A4F00Aull);
    CHECK(cyphal_crc64we("", 0) == 0);
}

static void test_unique_id(void)
{
    /* The example board of manual/use/cyphal.md: MAC e4:b0:63:8c:2d:b0, holo-2db0 */
    static const uint8_t mac[6] = { 0xe4, 0xb0, 0x63, 0x8c, 0x2d, 0xb0 };
    static const uint8_t want[16] = { 0x68, 0x6f, 0x6c, 0x6f, 0x70, 0x6c, 0x61, 0x79,  /* holoplay */
                                      0x00, 0x00, 0xe4, 0xb0, 0x63, 0x8c, 0x2d, 0xb0 };
    uint8_t uid[16];
    memset(uid, 0xAA, sizeof(uid));
    cyphal_unique_id(uid, "holoplay", mac);
    CHECK(memcmp(uid, want, sizeof(want)) == 0);

    /* The pseudo-ID is the low 48 bits of the CRC over all 16 bytes */
    const uint64_t crc = cyphal_crc64we(uid, sizeof(uid));
    CHECK(cyphal_pseudo_id(uid) == (crc & 0xFFFFFFFFFFFFull));
    CHECK(cyphal_pseudo_id(uid) <= 0xFFFFFFFFFFFFull);
    /* One bit of the MAC changes it */
    uint8_t other[16];
    memcpy(other, uid, sizeof(other));
    other[15] ^= 1;
    CHECK(cyphal_pseudo_id(other) != cyphal_pseudo_id(uid));
    printf("  example board: unique id 686f6c6f706c61790000e4b0638c2db0, pseudo-ID 0x%012" PRIx64 "\n",
           cyphal_pseudo_id(uid));
}

static void test_names(void)
{
    char why[128];
    static const char *const good[] = {
        "r2.dome.holo.front", "r2.dome.holo.top", "r2.dome.holo.rear", "a", "a_b.c_d", "_x", "x.y9", "abc.d_1",
        /* 50 characters */
        "abcdefghij.abcdefghij.abcdefghij.abcdefghij.abcdef",
    };
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        why[0] = '\0';
        const bool ok = cyphal_name_valid(good[i], why, sizeof(why));
        if (!ok) {
            printf("  `%s` refused: %s\n", good[i], why);
        }
        CHECK(ok);
    }
    struct {
        const char *name;
        const char *reason;     /* a part of the reason it must give */
    } bad[] = {
        { "", "1 to 50 characters, and this is 0" },
        { "abcdefghij.abcdefghij.abcdefghij.abcdefghij.abcdefg", "and this is 51" },
        { "R2.Dome", "`R` is not allowed" },
        { "r2..holo", "empty" },
        { "r2.1holo", "start with a digit: `1holo`" },
        { "1r2", "start with a digit: `1r2`" },
        { ".r2", "empty" },
        { "r2.", "empty" },
        { "r2 dome", "character 0x20" },
        { "r2-dome", "`-` is not allowed" },
        { "r2.dome\xc3\xa9", "character 0xC3" },
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        why[0] = '\0';
        const bool ok = cyphal_name_valid(bad[i].name, why, sizeof(why));
        CHECK(!ok);
        if (strstr(why, bad[i].reason) == NULL) {
            printf("  `%s`: reason \"%s\" lacks \"%s\"\n", bad[i].name, why, bad[i].reason);
            CHECK(0);
        }
    }
}

static void test_frame_source(void)
{
    /* Heartbeat from node 52: priority 4, subject 7509, bits 21-22 set as transmitted */
    const uint32_t hb = (4u << 26) | (3u << 21) | (7509u << 8) | 52u;
    CHECK(cyphal_frame_source(hb, true, false, 8) == 52);
    /* Anonymous (bit 24): the low bits are a pseudo-ID, not a node-ID */
    CHECK(cyphal_frame_source(hb | (1u << 24), true, false, 8) == -1);
    /* A service request from 10 to 52 (GetInfo, 430) and its response from 52 to 10 */
    const uint32_t req = (4u << 26) | (1u << 25) | (1u << 24) | (430u << 14) | (52u << 7) | 10u;
    const uint32_t resp = (4u << 26) | (1u << 25) | (430u << 14) | (10u << 7) | 52u;
    CHECK(cyphal_frame_source(req, true, false, 1) == 10);
    CHECK(cyphal_frame_source(resp, true, false, 8) == 52);
    /* Not Cyphal: 11-bit, remote, no tail byte, reserved bit 23, or bit 7 of a message */
    CHECK(cyphal_frame_source(0x123, false, false, 8) == -1);
    CHECK(cyphal_frame_source(hb, true, true, 0) == -1);
    CHECK(cyphal_frame_source(hb, true, false, 0) == -1);
    CHECK(cyphal_frame_source(hb | (1u << 23), true, false, 8) == -1);
    CHECK(cyphal_frame_source(req | (1u << 23), true, false, 8) == -1);
    CHECK(cyphal_frame_source(hb | (1u << 7), true, false, 8) == -1);
    /* Node 0 and node 127 are node-IDs like any other */
    CHECK(cyphal_frame_source((hb & ~0x7Fu) | 0, true, false, 8) == 0);
    CHECK(cyphal_frame_source((hb & ~0x7Fu) | 127, true, false, 8) == 127);
}

static void test_software_version(void)
{
    struct {
        const char *version;
        uint8_t major, minor;
        uint64_t vcs;
    } cases[] = {
        { "v1.1.0", 1, 1, 0 },
        { "v0.2.0-rc1", 0, 2, 0 },
        { "v12.34.5", 12, 34, 0 },
        { "v1.1.0-1-gac95a11-dirty", 0, 0, 0xac95a11 },
        { "v1.1.0-1-gac95a11", 0, 0, 0xac95a11 },
        { "v1.1.0-dirty", 0, 0, 0 },
        { "v0.2.0-rc1-3-g0123abcd", 0, 0, 0x0123abcd },
        { "ac95a11", 0, 0, 0xac95a11 },
        { "ac95a11-dirty", 0, 0, 0xac95a11 },
        { "0123456789abcdef0123", 0, 0, 0x0123456789abcdefull },   /* 16 digits at most */
        { "v1.1", 0, 0, 0 },
        { "", 0, 0, 0 },
        { "1.0.0", 0, 0, 0 },
        { "v1.0.0-gold", 0, 0, 0 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t major = 99, minor = 99;
        uint64_t vcs = 99;
        cyphal_software_version(cases[i].version, &major, &minor, &vcs);
        const bool ok = major == cases[i].major && minor == cases[i].minor && vcs == cases[i].vcs;
        if (!ok) {
            printf("  `%s` gave %u.%u, vcs 0x%" PRIx64 "\n", cases[i].version, major, minor, vcs);
        }
        CHECK(ok);
    }
    uint8_t major = 9, minor = 9;
    uint64_t vcs = 9;
    cyphal_software_version(NULL, &major, &minor, &vcs);
    CHECK(major == 0 && minor == 0 && vcs == 0);
}

static void test_settings_equal(void)
{
    cyphal_settings_t a, b;
    memset(&a, 0, sizeof(a));
    a.node_id = 52;
    a.bitrate = 1000000;
    strcpy(a.name, "r2.dome.holo.front");
    /* What a longer name and then the old one leave behind the terminator (the console's
     * `cyphal name`, copying into the saved settings) is not part of the name */
    memcpy(&b, &a, sizeof(b));
    strcpy(b.name, "r2.dome.holo.front.longer");
    strcpy(b.name, "r2.dome.holo.front");
    CHECK(memcmp(&a, &b, sizeof(a)) != 0);
    CHECK(cyphal_settings_equal(&a, &b));
    CHECK(cyphal_settings_equal(&b, &a));
    /* Each field counts */
    b.node_id = 53;
    CHECK(!cyphal_settings_equal(&a, &b));
    b.node_id = 52;
    b.bitrate = 500000;
    CHECK(!cyphal_settings_equal(&a, &b));
    b.bitrate = 1000000;
    strcpy(b.name, "r2.dome.holo.fron");
    CHECK(!cyphal_settings_equal(&a, &b));
    strcpy(b.name, "r2.dome.holo.front.x");
    CHECK(!cyphal_settings_equal(&a, &b));
    /* A name of the full 50 characters, which fills the buffer up to its terminator */
    memset(a.name, 'a', CYPHAL_NAME_MAX);
    a.name[CYPHAL_NAME_MAX] = '\0';
    memcpy(b.name, a.name, sizeof(b.name));
    CHECK(cyphal_settings_equal(&a, &b));
    b.name[CYPHAL_NAME_MAX - 1] = 'b';
    CHECK(!cyphal_settings_equal(&a, &b));
}

/* D9's listen, in esp_timer microseconds: started at 10 s, 2 s long */
static void test_listen(void)
{
    const int64_t t0 = 10000000, len = 2000000, end = t0 + len;
    cyphal_listen_t l;

    /* A quiet bus: reads that find nothing end it only CYPHAL_RX_SLACK_US after its end, since
     * a frame stamped just before a read may not be in the queue yet */
    cyphal_listen_start(&l, t0, len);
    CHECK(!cyphal_listen_over(&l));
    cyphal_listen_empty(&l, t0 + 1000000);
    CHECK(!cyphal_listen_over(&l));
    cyphal_listen_empty(&l, end);
    CHECK(!cyphal_listen_over(&l));
    cyphal_listen_empty(&l, end + CYPHAL_RX_SLACK_US - 1);
    CHECK(!cyphal_listen_over(&l));
    CHECK(cyphal_listen_wake_us(&l) == end + CYPHAL_RX_SLACK_US);
    cyphal_listen_empty(&l, cyphal_listen_wake_us(&l));
    CHECK(cyphal_listen_over(&l));

    /* The task held off far past the end: time alone doesn't end it. The frames from before the
     * end are read first, and one stamped at or after it, or an empty read, ends it */
    cyphal_listen_start(&l, t0, len);
    cyphal_listen_frame(&l, end - 5000);
    CHECK(!cyphal_listen_over(&l));
    cyphal_listen_frame(&l, end - 1);
    CHECK(!cyphal_listen_over(&l));
    cyphal_listen_frame(&l, end);
    CHECK(cyphal_listen_over(&l));

    cyphal_listen_start(&l, t0, len);
    cyphal_listen_frame(&l, end - 1);
    cyphal_listen_empty(&l, end + 5000000);
    CHECK(cyphal_listen_over(&l));

    /* A busy bus doesn't hold it back: the first frame stamped after the end ends it, however
     * many follow */
    cyphal_listen_start(&l, t0, len);
    for (int64_t t = t0; t < end; t += 130) {
        cyphal_listen_frame(&l, t);
    }
    CHECK(!cyphal_listen_over(&l));
    cyphal_listen_frame(&l, end + 70);
    CHECK(cyphal_listen_over(&l));

    /* Nothing moves it back: an older stamp, or an empty read just after a late frame */
    cyphal_listen_start(&l, t0, len);
    cyphal_listen_frame(&l, end + 10);
    cyphal_listen_frame(&l, t0 + 1);
    cyphal_listen_empty(&l, end + 20);
    CHECK(cyphal_listen_over(&l));
    CHECK(l.checked_us == end + 10);

    /* A stamp from before the start (a frame queued as the controller started) is checked like any
     * other and ends nothing */
    cyphal_listen_start(&l, t0, len);
    cyphal_listen_frame(&l, t0 - 100);
    CHECK(!cyphal_listen_over(&l));
    CHECK(l.checked_us == t0);
}

int main(void)
{
    test_crc64we();
    test_unique_id();
    test_names();
    test_frame_source();
    test_software_version();
    test_settings_equal();
    test_listen();
    printf("cyphal_util: %d checks, %d failed\n", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
