/*
 * Host-built tests for components/scenes/happening.c, the board's happenings read back from the JSON the kit's events
 * keep, and components/hostlink/hl_record.c, the host link's 8-byte native event records made from them
 * (manual/reference/host-protocol.md, "Native frames"). Built with the cJSON the IDF component manager fetches
 * (managed_components/espressif__cjson). `make unit-test`.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "cJSON.h"
#include "happening.h"
#include "hl_record.h"

/* The JSON read: its kind (and false for none). The strings point into the JSON, kept until the next call */
static happening_kind_t read_json(const char *text, happening_t *h)
{
    static cJSON *o;
    cJSON_Delete(o);
    o = cJSON_Parse(text);
    const bool ok = happening_of(o, h);
    return ok ? h->kind : HAPPENING_NONE;
}

static bool record_is(const char *text, const uint8_t want[8])
{
    cJSON *o = cJSON_Parse(text);
    happening_t h;
    uint8_t r[8];
    const bool ok = happening_of(o, &h) && hl_record(&h, r) && memcmp(r, want, 8) == 0;
    if (!ok) {
        printf("  %s: %02x %02x %02x %02x %02x %02x %02x %02x\n", text, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    }
    cJSON_Delete(o);
    return ok;
}

static void test_read(void)
{
    happening_t h;
    /* As runner.c makes them, with the store's event and seq (and t_ms, which nothing here reads) */
    CHECK(read_json("{\"event\":\"clip_started\",\"seq\":42,\"t_ms\":5,\"path\":\"/a.mov\",\"loop\":true,\"plays\":0,"
                    "\"frames\":300,\"fps\":29.97}", &h) == HAPPENING_CLIP_STARTED);
    CHECK(h.seq == 42 && strcmp(h.path, "/a.mov") == 0 && h.loop && h.plays == 0 && h.frames == 300 && h.fps == 29.97);
    CHECK(read_json("{\"event\":\"clip_ended\",\"seq\":65535,\"path\":\"/b.gif\",\"finished\":true}", &h) ==
          HAPPENING_CLIP_ENDED && h.seq == 65535 && h.finished && strcmp(h.path, "/b.gif") == 0);
    CHECK(read_json("{\"event\":\"scene_started\",\"seq\":3,\"name\":\"cantina\",\"slot\":7,\"then\":\"off\","
                    "\"until_clip_ends\":true,\"duration_s\":12.5}", &h) == HAPPENING_SCENE_STARTED);
    CHECK(strcmp(h.name, "cantina") == 0 && h.slot == 7 && h.then == SCENE_THEN_OFF && h.until_clip_ends &&
          h.duration_s == 12.5);
    /* No slot, no duration: 0 */
    CHECK(read_json("{\"event\":\"scene_started\",\"seq\":4,\"name\":\"now\",\"then\":\"stay\",\"until_clip_ends\":false}",
                    &h) == HAPPENING_SCENE_STARTED && h.slot == 0 && h.duration_s == 0 && h.then == SCENE_THEN_STAY);
    for (int by = 0; by < 4; by++) {
        char text[160];
        snprintf(text, sizeof(text), "{\"event\":\"scene_ended\",\"seq\":5,\"name\":\"x\",\"slot\":255,\"then\":\"restore\","
                 "\"by\":\"%s\"}", SCENE_BY_NAMES[by]);
        CHECK(read_json(text, &h) == HAPPENING_SCENE_ENDED && h.by == (scene_by_t)by && h.then == SCENE_THEN_RESTORE &&
              h.slot == 255);
    }
    CHECK(read_json("{\"event\":\"touch\",\"seq\":9,\"action\":\"up\",\"x\":239,\"y\":12}", &h) == HAPPENING_TOUCH &&
          h.up && h.x == 239 && h.y == 12);
    CHECK(read_json("{\"event\":\"touch\",\"seq\":9,\"action\":\"down\",\"x\":1,\"y\":2}", &h) == HAPPENING_TOUCH && !h.up);
    /* A `then` or `by` that is none of the names: the first; a number that isn't one: 0 */
    CHECK(read_json("{\"event\":\"scene_ended\",\"then\":\"later\",\"by\":7,\"slot\":\"3\"}", &h) == HAPPENING_SCENE_ENDED &&
          h.then == SCENE_THEN_STAY && h.by == SCENE_BY_CLIP && h.slot == 0 && h.name == NULL && h.seq == 0);
    /* Not a happening: a state kind, no kind, not an object */
    CHECK(read_json("{\"event\":\"leds\",\"state\":{}}", &h) == HAPPENING_NONE && h.kind == HAPPENING_NONE);
    CHECK(read_json("{\"seq\":3}", &h) == HAPPENING_NONE);
    CHECK(read_json("[1]", &h) == HAPPENING_NONE);
    CHECK(!happening_of(NULL, &h));
    /* The names, as the JSON has them */
    CHECK(strcmp(SCENE_THEN_NAMES[SCENE_THEN_RESTORE], "restore") == 0 && strcmp(SCENE_BY_NAMES[SCENE_BY_REPLACED], "replaced") == 0);
}

static void test_records(void)
{
    /* Each kind, byte for byte as hl_native.c packed it before the decoder was shared: kind, seq (low byte first) */
    CHECK(record_is("{\"event\":\"clip_ended\",\"seq\":258,\"path\":\"/b.mov\",\"finished\":true}",
                    (const uint8_t[8]){ 1, 2, 1, 1, 0, 0, 0, 0 }));
    CHECK(record_is("{\"event\":\"scene_ended\",\"seq\":5,\"name\":\"x\",\"slot\":200,\"then\":\"off\",\"by\":\"replaced\"}",
                    (const uint8_t[8]){ 2, 5, 0, 200, 2, 3, 0, 0 }));
    CHECK(record_is("{\"event\":\"scene_ended\",\"seq\":6,\"name\":\"x\",\"then\":\"restore\",\"by\":\"time\"}",
                    (const uint8_t[8]){ 2, 6, 0, 0, 1, 1, 0, 0 }));
    CHECK(record_is("{\"event\":\"touch\",\"seq\":9,\"action\":\"up\",\"x\":239,\"y\":12}",
                    (const uint8_t[8]){ 3, 9, 0, 1, 239, 12, 0, 0 }));
    /* clip_started: loop, plays held to 255, frames to 65535 (low byte first), fps rounded and held to 255 */
    CHECK(record_is("{\"event\":\"clip_started\",\"seq\":7,\"path\":\"/a.mov\",\"loop\":true,\"plays\":0,\"frames\":300,"
                    "\"fps\":29.97}", (const uint8_t[8]){ 4, 7, 0, 1, 0, 0x2C, 0x01, 30 }));
    CHECK(record_is("{\"event\":\"clip_started\",\"seq\":7,\"path\":\"/a.mov\",\"loop\":false,\"plays\":1000,"
                    "\"frames\":70000,\"fps\":300}", (const uint8_t[8]){ 4, 7, 0, 0, 255, 0xFF, 0xFF, 255 }));
    CHECK(record_is("{\"event\":\"clip_started\",\"seq\":7,\"plays\":3,\"frames\":60,\"fps\":19.4}",
                    (const uint8_t[8]){ 4, 7, 0, 0, 3, 60, 0, 19 }));
    /* scene_started: the duration in whole seconds, rounded up, held to 65535; none is 0 */
    CHECK(record_is("{\"event\":\"scene_started\",\"seq\":3,\"name\":\"c\",\"slot\":7,\"then\":\"restore\","
                    "\"until_clip_ends\":true,\"duration_s\":12.5}", (const uint8_t[8]){ 5, 3, 0, 7, 1, 1, 13, 0 }));
    CHECK(record_is("{\"event\":\"scene_started\",\"seq\":3,\"name\":\"c\",\"then\":\"off\",\"until_clip_ends\":false,"
                    "\"duration_s\":0.1}", (const uint8_t[8]){ 5, 3, 0, 0, 2, 0, 1, 0 }));
    CHECK(record_is("{\"event\":\"scene_started\",\"seq\":3,\"name\":\"c\",\"then\":\"stay\",\"until_clip_ends\":false}",
                    (const uint8_t[8]){ 5, 3, 0, 0, 0, 0, 0, 0 }));
    CHECK(record_is("{\"event\":\"scene_started\",\"seq\":3,\"name\":\"c\",\"then\":\"off\",\"duration_s\":65535.5}",
                    (const uint8_t[8]){ 5, 3, 0, 0, 2, 0, 0xFF, 0xFF }));     /* held to 65535 */
    /* None for anything else, its seq still written */
    happening_t h = { .kind = HAPPENING_NONE, .seq = 0x1234 };
    uint8_t r[8];
    CHECK(!hl_record(&h, r) && r[1] == 0x34 && r[2] == 0x12);
}

int main(void)
{
    test_read();
    test_records();
    printf("test_happening: %d checks, %d failed\n", s_checks, s_failed);
    return s_failed != 0;
}
