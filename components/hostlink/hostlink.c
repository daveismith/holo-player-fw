/*
 * The host link's settings, routes and console command. See hostlink.h.
 *
 * The settings are LinkSettings (the OpenAPI description): the Kconfig defaults, overridden by
 * what was saved (NVS namespace "hostlink", key "cfg", as JSON). Changing the transport, the pins,
 * `attn`, the I2C address or `general_call` waits for a restart; the rest applies at once, a new
 * baud rate after the reply that asked for it. The console (`link`), the API (PATCH /link) and
 * the web app all change them through hl_patch().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "api_core.h"
#include "hostlink.h"
#include "hl.h"

static const char *TAG = "hostlink";

#define NVS_NS  "hostlink"
#define NVS_KEY "cfg"

const char *const HL_MODES[] = { "off", "uart", "rs485", "i2c" };
const char *const HL_PROTOS[] = { "auto", "json", "native" };

static SemaphoreHandle_t s_lock;
static hl_settings_t s_saved;       /* what is saved, and runs from the next start */
static hl_settings_t s_run;         /* what runs now */
static bool s_started;
static hl_counters_t s_counters;
static bool s_log;
static int s_new_baud;              /* for the transport, after the reply now going out */

/* P2's free pins, less what the firmware has claimed: the only ones the link may take */
static const int FREE_PINS[] = { 15, 16, 17, 18, 21, 33 };

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

hl_counters_t *hl_counters(void)
{
    return &s_counters;
}

bool hl_logging(void)
{
    return s_log;
}

void hl_running(hl_settings_t *out)
{
    lock();
    *out = s_run;
    unlock();
}

int hl_take_baud(void)
{
    lock();
    const int b = s_new_baud;
    s_new_baud = 0;
    unlock();
    return b;
}

/* ------------------------------------------------------------------ settings as JSON */

#if CONFIG_HOSTLINK_MODE_UART
#define DEFAULT_MODE HL_UART
#elif CONFIG_HOSTLINK_MODE_RS485
#define DEFAULT_MODE HL_RS485
#else
#define DEFAULT_MODE HL_OFF
#endif
#if CONFIG_HOSTLINK_PROTOCOL_JSON
#define DEFAULT_PROTOCOL HL_JSON
#elif CONFIG_HOSTLINK_PROTOCOL_NATIVE
#define DEFAULT_PROTOCOL HL_NATIVE
#else
#define DEFAULT_PROTOCOL HL_AUTO
#endif

static void defaults(hl_settings_t *s)
{
    *s = (hl_settings_t){
        .mode = DEFAULT_MODE,
        .protocol = DEFAULT_PROTOCOL,
        .pin_a = CONFIG_HOSTLINK_PIN_A,
        .pin_b = CONFIG_HOSTLINK_PIN_B,
        .pin_c = CONFIG_HOSTLINK_PIN_C,
        .baud = CONFIG_HOSTLINK_BAUD,
        .address = CONFIG_HOSTLINK_ADDRESS,
        .reply_delay_ms = 1,
        .i2c_address = CONFIG_HOSTLINK_I2C_ADDRESS,
        .push = true,
    };
}

static cJSON *settings_json(const hl_settings_t *s)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "mode", HL_MODES[s->mode]);
    cJSON_AddStringToObject(o, "protocol", HL_PROTOS[s->protocol]);
    cJSON *pins = cJSON_AddObjectToObject(o, "pins");
    cJSON_AddNumberToObject(pins, "a", s->pin_a);
    cJSON_AddNumberToObject(pins, "b", s->pin_b);
    if (s->pin_c >= 0) {
        cJSON_AddNumberToObject(pins, "c", s->pin_c);
    } else {
        cJSON_AddNullToObject(pins, "c");
    }
    cJSON_AddNumberToObject(o, "baud", s->baud);
    cJSON_AddNumberToObject(o, "address", s->address);
    cJSON *groups = cJSON_AddArrayToObject(o, "groups");
    for (int i = 0; i < s->n_groups; i++) {
        cJSON_AddItemToArray(groups, cJSON_CreateNumber(s->groups[i]));
    }
    cJSON_AddNumberToObject(o, "reply_delay_ms", s->reply_delay_ms);
    cJSON_AddBoolToObject(o, "attn", s->attn);
    cJSON_AddNumberToObject(o, "i2c_address", s->i2c_address);
    cJSON_AddBoolToObject(o, "general_call", s->general_call);
    cJSON *ev = cJSON_AddObjectToObject(o, "events");
    cJSON *kinds = cJSON_AddArrayToObject(ev, "kinds");
    for (const char *p = s->kinds; *p; ) {
        const size_t n = strcspn(p, ",");
        char name[16];
        if (n > 0 && n < sizeof(name)) {
            memcpy(name, p, n);
            name[n] = '\0';
            cJSON_AddItemToArray(kinds, cJSON_CreateString(name));
        }
        p += n + (p[n] == ',');
    }
    cJSON_AddBoolToObject(ev, "push", s->push);
    return o;
}

static int index_of(const char *const *names, size_t n, const cJSON *v)
{
    for (size_t i = 0; cJSON_IsString(v) && i < n; i++) {
        if (strcmp(v->valuestring, names[i]) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static bool whole(const cJSON *v, int lo, int hi, int *out)
{
    if (!cJSON_IsNumber(v) || v->valuedouble != (int)v->valuedouble || v->valuedouble < lo || v->valuedouble > hi) {
        return false;
    }
    *out = (int)v->valuedouble;
    return true;
}

static const char *pin_problem(int pin)
{
    for (size_t i = 0; i < sizeof(FREE_PINS) / sizeof(FREE_PINS[0]); i++) {
        if (FREE_PINS[i] == pin) {
            if (pin == CONFIG_LEDS_GPIO) {
                return "the LED strip's";
            }
            if (pin == CONFIG_HOLO_SERVO1_GPIO || pin == CONFIG_HOLO_SERVO2_GPIO) {
                return "a servo's";
            }
            return NULL;
        }
    }
    return "not one of P2's spare pins (15, 16, 17, 18, 21, 33)";
}

/*
 * `patch` onto `s`: false, with `status` and `why`, at the first thing wrong. `strict` (the API
 * and the console) refuses unknown fields and kinds; loading what was saved skips them.
 */
static bool patch(hl_settings_t *s, const cJSON *patch, bool strict, int *status, char *why, size_t why_len)
{
    static const char *const KEYS[] = { "mode", "protocol", "pins", "baud", "address", "groups", "reply_delay_ms",
                                        "attn", "i2c_address", "general_call", "events", NULL };
    const char *bad = NULL;
    *status = 400;
    if (strict && (!cJSON_IsObject(patch) || patch->child == NULL || !api_only_keys(patch, KEYS, &bad))) {
        snprintf(why, why_len, bad ? "unknown field `%s`" : "send the settings that change", bad);
        return false;
    }
    const cJSON *v;
    int n;
    if ((v = cJSON_GetObjectItem(patch, "mode")) != NULL) {
        n = index_of(HL_MODES, 4, v);
        if (n == HL_I2C) {
            snprintf(why, why_len, "the I2C link isn't in this firmware yet: off, uart or rs485");
            return false;
        }
        if (n < 0) {
            snprintf(why, why_len, "`mode` is off, uart or rs485");
            return false;
        }
        s->mode = (hl_mode_t)n;
    }
    if ((v = cJSON_GetObjectItem(patch, "protocol")) != NULL) {
        if ((n = index_of(HL_PROTOS, 3, v)) < 0) {
            snprintf(why, why_len, "`protocol` is auto, json or native");
            return false;
        }
        s->protocol = (hl_proto_t)n;
    }
    if ((v = cJSON_GetObjectItem(patch, "pins")) != NULL) {
        if (!cJSON_IsObject(v)) {
            snprintf(why, why_len, "`pins` is {\"a\", \"b\", \"c\"}");
            return false;
        }
        int *slots[] = { &s->pin_a, &s->pin_b, &s->pin_c };
        const char *names[] = { "a", "b", "c" };
        for (int i = 0; i < 3; i++) {
            const cJSON *p = cJSON_GetObjectItem(v, names[i]);
            if (p == NULL) {
                continue;
            }
            if (i == 2 && cJSON_IsNull(p)) {
                s->pin_c = -1;
                continue;
            }
            int pin;
            if (!whole(p, 0, 48, &pin)) {
                snprintf(why, why_len, "pin %s is a GPIO number", names[i]);
                return false;
            }
            const char *problem = pin_problem(pin);
            if (problem != NULL) {
                *status = 409;
                snprintf(why, why_len, "GPIO%d is %s", pin, problem);
                return false;
            }
            *slots[i] = pin;
        }
        if (s->pin_a == s->pin_b || s->pin_a == s->pin_c || s->pin_b == s->pin_c) {
            snprintf(why, why_len, "the three roles need three different pins");
            return false;
        }
    }
    if ((v = cJSON_GetObjectItem(patch, "baud")) != NULL && !whole(v, 1200, 921600, &s->baud)) {
        snprintf(why, why_len, "`baud` is 1200..921600");
        return false;
    }
    if ((v = cJSON_GetObjectItem(patch, "address")) != NULL && !whole(v, 1, 223, &s->address)) {
        snprintf(why, why_len, "`address` is 1..223");
        return false;
    }
    if ((v = cJSON_GetObjectItem(patch, "groups")) != NULL) {
        const int count = cJSON_GetArraySize(v);
        if (!cJSON_IsArray(v) || count > HL_MAX_GROUPS) {
            snprintf(why, why_len, "`groups` is a list of up to %d of 1..31", HL_MAX_GROUPS);
            return false;
        }
        uint8_t g[HL_MAX_GROUPS];
        for (int i = 0; i < count; i++) {
            int x;
            if (!whole(cJSON_GetArrayItem(v, i), 1, 31, &x)) {
                snprintf(why, why_len, "a group is 1..31");
                return false;
            }
            g[i] = (uint8_t)x;
        }
        memcpy(s->groups, g, sizeof(g));
        s->n_groups = count;
    }
    if ((v = cJSON_GetObjectItem(patch, "reply_delay_ms")) != NULL && !whole(v, 0, 50, &s->reply_delay_ms)) {
        snprintf(why, why_len, "`reply_delay_ms` is 0..50");
        return false;
    }
    if ((v = cJSON_GetObjectItem(patch, "attn")) != NULL) {
        if (!cJSON_IsBool(v)) {
            snprintf(why, why_len, "`attn` is true or false");
            return false;
        }
        s->attn = cJSON_IsTrue(v);
    }
    if ((v = cJSON_GetObjectItem(patch, "i2c_address")) != NULL && !whole(v, 8, 119, &s->i2c_address)) {
        snprintf(why, why_len, "`i2c_address` is 8..119 (0x08..0x77)");
        return false;
    }
    if ((v = cJSON_GetObjectItem(patch, "general_call")) != NULL) {
        if (!cJSON_IsBool(v)) {
            snprintf(why, why_len, "`general_call` is true or false");
            return false;
        }
        s->general_call = cJSON_IsTrue(v);
    }
    if ((v = cJSON_GetObjectItem(patch, "events")) != NULL) {
        const cJSON *kinds = cJSON_GetObjectItem(v, "kinds");
        const cJSON *push = cJSON_GetObjectItem(v, "push");
        if (!cJSON_IsObject(v) || (push != NULL && !cJSON_IsBool(push))) {
            snprintf(why, why_len, "`events` is {\"kinds\": [...], \"push\": true|false}");
            return false;
        }
        if (kinds != NULL) {
            char list[sizeof(s->kinds)] = "";
            const cJSON *k;
            cJSON_ArrayForEach(k, kinds) {
                const bool known = cJSON_IsString(k) && events_bit(k->valuestring) != 0;
                if (!known && strict) {
                    snprintf(why, why_len, "no such kind of event: %s", cJSON_IsString(k) ? k->valuestring : "?");
                    return false;
                }
                if (known && strlen(list) + strlen(k->valuestring) + 2 < sizeof(list)) {
                    snprintf(list + strlen(list), sizeof(list) - strlen(list), "%s%s", list[0] ? "," : "", k->valuestring);
                }
            }
            if (!cJSON_IsArray(kinds)) {
                snprintf(why, why_len, "`events.kinds` is a list of event names");
                return false;
            }
            strlcpy(s->kinds, list, sizeof(s->kinds));
        }
        if (push != NULL) {
            s->push = cJSON_IsTrue(push);
        }
    }
    return true;
}

static esp_err_t save(const hl_settings_t *s)
{
    cJSON *o = settings_json(s);
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (text == NULL) {
        return ESP_ERR_NO_MEM;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, NVS_KEY, text);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    cJSON_free(text);
    return err;
}

static bool restart_required(void)
{
    const hl_settings_t *a = &s_saved, *b = &s_run;
    return a->mode != b->mode || a->pin_a != b->pin_a || a->pin_b != b->pin_b || a->pin_c != b->pin_c ||
           a->attn != b->attn || a->i2c_address != b->i2c_address || a->general_call != b->general_call;
}

/* The saved settings' events as the link's */
static void apply_events(const hl_settings_t *s)
{
    events_mask_t mask = 0;
    for (const char *p = s->kinds; *p; ) {
        const size_t n = strcspn(p, ",");
        char name[16];
        if (n > 0 && n < sizeof(name)) {
            memcpy(name, p, n);
            name[n] = '\0';
            mask |= events_bit(name);
        }
        p += n + (p[n] == ',');
    }
    hl_events_set(mask, s->push);
}

/* Change the settings: saved, and what can apply now applied. `via_link`: a new baud rate waits
 * for the reply. */
static bool hl_patch(const cJSON *body, bool via_link, int *status, char *why, size_t why_len)
{
    lock();
    hl_settings_t s = s_saved;
    const bool ok = patch(&s, body, true, status, why, why_len);
    esp_err_t err = ok ? save(&s) : ESP_OK;
    if (ok && err == ESP_OK) {
        const int old_baud = s_run.baud;
        s_saved = s;
        s_run.protocol = s.protocol;
        s_run.baud = s.baud;
        s_run.address = s.address;
        memcpy(s_run.groups, s.groups, sizeof(s.groups));
        s_run.n_groups = s.n_groups;
        s_run.reply_delay_ms = s.reply_delay_ms;
        strlcpy(s_run.kinds, s.kinds, sizeof(s_run.kinds));
        s_run.push = s.push;
        if (s.baud != old_baud && s_started) {
            if (via_link) {
                s_new_baud = s.baud;
            } else {
                hl_uart_set_baud(s.baud);
            }
        }
    }
    unlock();
    if (ok && err != ESP_OK) {
        *status = 500;
        snprintf(why, why_len, "not saved: %s", esp_err_to_name(err));
        return false;
    }
    if (ok && cJSON_GetObjectItem(body, "events") != NULL) {
        apply_events(&s);
    }
    return ok;
}

/* ------------------------------------------------------------------ routes */

static cJSON *link_json(void)
{
    lock();
    const hl_settings_t saved = s_saved, run = s_run;
    const bool restart = restart_required();
    const hl_counters_t c = s_counters;
    unlock();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "protocol_version", HOSTLINK_PROTOCOL_VERSION);
    cJSON_AddItemToObject(o, "settings", settings_json(&saved));
    cJSON *r = cJSON_AddObjectToObject(o, "running");
    cJSON_AddStringToObject(r, "mode", s_started ? HL_MODES[run.mode] : "off");
    cJSON_AddStringToObject(r, "protocol", HL_PROTOS[run.protocol]);
    if (s_started) {
        cJSON *pins = cJSON_AddObjectToObject(r, "pins");
        cJSON_AddNumberToObject(pins, "a", run.pin_a);
        cJSON_AddNumberToObject(pins, "b", run.pin_b);
        if (run.pin_c >= 0 && (run.mode == HL_RS485 || run.attn)) {
            cJSON_AddNumberToObject(pins, "c", run.pin_c);
        } else {
            cJSON_AddNullToObject(pins, "c");
        }
    }
    cJSON_AddBoolToObject(o, "restart_required", restart);
    cJSON *cn = cJSON_AddObjectToObject(o, "counters");
    cJSON_AddNumberToObject(cn, "requests", c.requests);
    cJSON_AddNumberToObject(cn, "events", c.events);
    cJSON_AddNumberToObject(cn, "bad_lines", c.bad_lines);
    cJSON_AddNumberToObject(cn, "crc_errors", c.crc_errors);
    cJSON_AddNumberToObject(cn, "framing_errors", c.framing_errors);
    cJSON_AddNumberToObject(cn, "overruns", c.overruns);
    cJSON_AddNumberToObject(cn, "ignored", c.ignored);
    if (c.last_request_us != 0) {
        cJSON_AddNumberToObject(o, "last_request_s",
                                (double)((esp_timer_get_time() - c.last_request_us) / 100000) / 10);
    } else {
        cJSON_AddNullToObject(o, "last_request_s");
    }
    cJSON *ev = cJSON_AddObjectToObject(o, "events");
    cJSON_AddItemToObject(ev, "kinds", hl_kinds_json(hl_events_kinds()));
    cJSON_AddBoolToObject(ev, "push", hl_events_push());
    cJSON_AddNumberToObject(ev, "seq", events_seq());
    cJSON_AddNumberToObject(ev, "pending", hl_events_pending());
    return o;
}

static api_reply_t link_get(const api_req_t *req)
{
    (void)req;
    return api_json(200, link_json());
}

static api_reply_t link_patch(const api_req_t *req)
{
    int status;
    char why[128];
    if (!hl_patch(req->body, strcmp(req->via, "link") == 0, &status, why, sizeof(why))) {
        return api_error(status, status == 409 ? "pin_in_use" : status == 500 ? "failed" : "bad_request", "%s", why);
    }
    return api_json(200, link_json());
}

static cJSON *event_state_json(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "kinds", hl_kinds_json(hl_events_kinds()));
    cJSON_AddBoolToObject(o, "push", hl_events_push());
    cJSON_AddNumberToObject(o, "seq", events_seq());
    cJSON_AddNumberToObject(o, "pending", hl_events_pending());
    return o;
}

static api_reply_t link_events_get(const api_req_t *req)
{
    const bool give = strcmp(req->via, "link") == 0;
    cJSON *o = hl_events_ungiven(give);
    if (give) {
        s_counters.events += (uint32_t)cJSON_GetArraySize(cJSON_GetObjectItem(o, "events"));
    }
    return api_json(200, o);
}

static api_reply_t link_events_patch(const api_req_t *req)
{
    static const char *const KEYS[] = { "kinds", "push", NULL };
    const char *bad = NULL;
    const cJSON *kinds = cJSON_GetObjectItem(req->body, "kinds");
    const cJSON *push = cJSON_GetObjectItem(req->body, "push");
    if (req->body->child == NULL || !api_only_keys(req->body, KEYS, &bad) || (push != NULL && !cJSON_IsBool(push))) {
        return api_error(400, "bad_request", "send {\"kinds\": [...], \"push\": true|false}");
    }
    events_mask_t mask = hl_events_kinds();
    char name[32];
    if (kinds != NULL && !hl_kinds_of(kinds, &mask, name, sizeof(name))) {
        return api_error(400, "bad_request", "no such kind of event: %s", name);
    }
    hl_events_set(mask, push != NULL ? cJSON_IsTrue(push) : hl_events_push());
    return api_json(200, event_state_json());
}

/* GET /events over the link: the kept happenings as JSON (HTTP's is the web server's) */
typedef struct {
    cJSON *arr;
} gather_t;

static void gather(uint16_t seq, events_mask_t bit, const char *json, void *ctx)
{
    (void)seq;
    (void)bit;
    cJSON *e = cJSON_Parse(json);
    if (e != NULL) {
        cJSON_AddItemToArray(((gather_t *)ctx)->arr, e);
    }
}

static api_reply_t events_get(const api_req_t *req)
{
    char list[160], bad[16] = "", num[12];
    events_mask_t want = events_all();
    if (api_query(req, "kinds", list, sizeof(list)) && !events_parse(list, &want, bad, sizeof(bad))) {
        return api_error(400, "bad_request", "no such kind of event: %s", bad);
    }
    unsigned long after = 0;
    if (api_query(req, "after", num, sizeof(num))) {
        char *end = NULL;
        after = strtoul(num, &end, 10);
        if (end == num || *end != '\0' || after > 65535) {
            return api_error(400, "bad_request", "after: a number from 0 to 65535");
        }
    }
    gather_t g = { .arr = cJSON_CreateArray() };
    uint32_t lost = 0;
    const uint16_t seq = events_since((uint16_t)after, want & ~events_states(), gather, &g, &lost);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "seq", seq);
    cJSON_AddNumberToObject(o, "lost", lost);
    cJSON_AddItemToObject(o, "events", g.arr);
    return api_json(200, o);
}

static const api_route_t ROUTES[] = {
    API_ROUTE(API_GET, "/api/v1/link", link_get, 0, API_LINK),
    API_ROUTE(API_PATCH, "/api/v1/link", link_patch, 512, API_LINK),
    API_ROUTE(API_GET, "/api/v1/link/events", link_events_get, 0, API_LINK),
    API_ROUTE(API_PATCH, "/api/v1/link/events", link_events_patch, 512, API_LINK),
    API_ROUTE(API_GET, "/api/v1/events", events_get, 0, API_LINK | API_NO_HTTP),
};

esp_err_t hostlink_register_routes(void)
{
    return api_add_routes(ROUTES, sizeof(ROUTES) / sizeof(ROUTES[0]));
}

/* ------------------------------------------------------------------ start */

void hostlink_init(void)
{
    if (s_lock != NULL) {
        return;
    }
    s_lock = xSemaphoreCreateMutex();
    hl_events_init();
    defaults(&s_saved);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = 0;
        if (nvs_get_str(h, NVS_KEY, NULL, &len) == ESP_OK && len > 0 && len < 2048) {
            char *text = malloc(len);
            if (text != NULL && nvs_get_str(h, NVS_KEY, text, &len) == ESP_OK) {
                cJSON *o = cJSON_Parse(text);
                int status;
                char why[96];
                hl_settings_t s = s_saved;
                if (o != NULL && patch(&s, o, false, &status, why, sizeof(why))) {
                    s_saved = s;
                } else {
                    ESP_LOGW(TAG, "the saved settings don't read (%s): the defaults", o ? why : "not JSON");
                }
                cJSON_Delete(o);
            }
            free(text);
        }
        nvs_close(h);
    }
    s_run = s_saved;
}

void hostlink_pins(int *a, int *b, int *c)
{
    *a = *b = *c = -1;
    if (s_saved.mode == HL_UART || s_saved.mode == HL_RS485) {
        *a = s_saved.pin_a;
        *b = s_saved.pin_b;
        *c = s_saved.mode == HL_RS485 || s_saved.attn ? s_saved.pin_c : -1;
    }
}

esp_err_t hostlink_start(void)
{
    hostlink_init();
    apply_events(&s_run);
    if (s_run.mode == HL_OFF) {
        ESP_LOGI(TAG, "off (`link mode uart` turns it on)");
        return ESP_OK;
    }
    const esp_err_t err = hl_uart_start(&s_run);
    s_started = err == ESP_OK;
    return err;
}

/* ------------------------------------------------------------------ console */

static void print_link(void)
{
    cJSON *o = link_json();
    const cJSON *s = cJSON_GetObjectItem(o, "settings");
    const cJSON *c = cJSON_GetObjectItem(o, "counters");
    const cJSON *ev = cJSON_GetObjectItem(o, "events");
    hl_settings_t run;
    hl_running(&run);
    if (!s_started) {
        printf("link: off%s\n", s_saved.mode != HL_OFF ? "; from the next restart: " : "");
    } else {
        printf("link: %s on GPIO%d (TX), GPIO%d (RX)", HL_MODES[run.mode], run.pin_a, run.pin_b);
        if (run.mode == HL_RS485 && run.pin_c >= 0) {
            printf(", GPIO%d (DE)", run.pin_c);
        } else if (run.attn && run.pin_c >= 0) {
            printf(", GPIO%d (ATTN)", run.pin_c);
        }
        printf(", %d baud, protocol %s, address %d", run.baud, HL_PROTOS[run.protocol], run.address);
        for (int i = 0; i < run.n_groups; i++) {
            printf("%s%d", i ? "," : ", groups ", run.groups[i]);
        }
        printf("\n");
    }
    if (cJSON_IsTrue(cJSON_GetObjectItem(o, "restart_required")) || !s_started) {
        if (s_saved.mode != HL_OFF) {
            printf("  saved: %s on GPIO%d, GPIO%d, pin C %d -- from the next restart\n",
                   HL_MODES[s_saved.mode], s_saved.pin_a, s_saved.pin_b, s_saved.pin_c);
        }
    }
    char *kinds = cJSON_PrintUnformatted(cJSON_GetObjectItem(ev, "kinds"));
    const cJSON *last = cJSON_GetObjectItem(o, "last_request_s");
    printf("  requests %d, events given %d, bad lines %d, framing errors %d, overruns %d, ignored %d; last request %s",
           cJSON_GetObjectItem(c, "requests")->valueint, cJSON_GetObjectItem(c, "events")->valueint,
           cJSON_GetObjectItem(c, "bad_lines")->valueint, cJSON_GetObjectItem(c, "framing_errors")->valueint,
           cJSON_GetObjectItem(c, "overruns")->valueint, cJSON_GetObjectItem(c, "ignored")->valueint,
           cJSON_IsNumber(last) ? "" : "never");
    if (cJSON_IsNumber(last)) {
        printf("%.1f s ago", last->valuedouble);
    }
    printf("\n  events: %s, %s, %d pending\n", kinds ? kinds : "[]",
           cJSON_IsTrue(cJSON_GetObjectItem(ev, "push")) ? "pushed" : "kept", cJSON_GetObjectItem(ev, "pending")->valueint);
    cJSON_free(kinds);
    (void)s;
    cJSON_Delete(o);
}

static int console_patch(cJSON *body)
{
    int status;
    char why[128];
    const bool ok = hl_patch(body, false, &status, why, sizeof(why));
    cJSON_Delete(body);
    if (!ok) {
        printf("link: %s\n", why);
        return 1;
    }
    print_link();
    return 0;
}

static int cmd_link(int argc, char **argv)
{
    if (argc == 1) {
        print_link();
        return 0;
    }
    const char *sub = argv[1];
    cJSON *body = cJSON_CreateObject();
    if (strcmp(sub, "mode") == 0 && argc >= 3) {
        cJSON_AddStringToObject(body, "mode", argv[2]);
        for (int i = 3; i + 1 < argc; i += 2) {
            if (strcmp(argv[i], "-b") == 0) {
                cJSON_AddNumberToObject(body, "baud", atoi(argv[i + 1]));
            } else if (strcmp(argv[i], "-a") == 0) {
                const long a = strtol(argv[i + 1], NULL, 0);
                cJSON_AddNumberToObject(body, strcmp(argv[2], "i2c") == 0 ? "i2c_address" : "address", a);
            }
        }
        return console_patch(body);
    }
    if (strcmp(sub, "proto") == 0 && argc == 3) {
        cJSON_AddStringToObject(body, "protocol", argv[2]);
        return console_patch(body);
    }
    if (strcmp(sub, "address") == 0 && argc == 3) {
        cJSON_AddNumberToObject(body, "address", atoi(argv[2]));
        return console_patch(body);
    }
    if (strcmp(sub, "groups") == 0) {
        cJSON *g = cJSON_AddArrayToObject(body, "groups");
        for (int i = 2; i < argc && strcmp(argv[i], "--clear") != 0; i++) {
            cJSON_AddItemToArray(g, cJSON_CreateNumber(atoi(argv[i])));
        }
        return console_patch(body);
    }
    if (strcmp(sub, "pins") == 0 && argc == 5) {
        cJSON *p = cJSON_AddObjectToObject(body, "pins");
        cJSON_AddNumberToObject(p, "a", atoi(argv[2]));
        cJSON_AddNumberToObject(p, "b", atoi(argv[3]));
        if (atoi(argv[4]) < 0) {
            cJSON_AddNullToObject(p, "c");
        } else {
            cJSON_AddNumberToObject(p, "c", atoi(argv[4]));
        }
        return console_patch(body);
    }
    if (strcmp(sub, "events") == 0) {
        cJSON *ev = cJSON_AddObjectToObject(body, "events");
        cJSON *kinds = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--push") == 0 || strcmp(argv[i], "--keep") == 0) {
                cJSON_AddBoolToObject(ev, "push", strcmp(argv[i], "--push") == 0);
            } else {
                if (kinds == NULL) {
                    kinds = cJSON_AddArrayToObject(ev, "kinds");
                }
                if (strcmp(argv[i], "--none") != 0) {
                    cJSON_AddItemToArray(kinds, cJSON_CreateString(argv[i]));
                }
            }
        }
        return console_patch(body);
    }
    if (strcmp(sub, "log") == 0) {
        s_log = argc < 3 || strcmp(argv[2], "off") != 0;
        printf("link: %s\n", s_log ? "printing what goes in and out (`link log off` stops)" : "not printing");
        cJSON_Delete(body);
        return 0;
    }
    if (strcmp(sub, "reset") == 0 && argc == 2) {
        cJSON_Delete(body);
        hl_settings_t d;
        defaults(&d);
        lock();
        s_saved = d;
        const esp_err_t err = save(&d);
        unlock();
        if (err != ESP_OK) {
            printf("link: not saved: %s\n", esp_err_to_name(err));
            return 1;
        }
        print_link();
        return 0;
    }
    cJSON_Delete(body);
    printf("usage: link [mode <off|uart|rs485> [-b <baud>] [-a <address>] | proto <auto|json|native> | "
           "address <1-223> | groups [<g>...|--clear] | pins <a> <b> <c> | events [<kind>...|--none] "
           "[--push|--keep] | log [on|off] | reset]\n");
    return 1;
}

void hostlink_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "link",
        .help = "The host link: a controller on a wire, on P2's spare pins (UART, or RS485 through a transceiver). "
                "Alone, what it is doing. The transport and its pins take effect at the next restart; the rest at "
                "once. `link log` prints what goes in and out.",
        .hint = "[mode <off|uart|rs485> [-b <baud>] [-a <address>] | proto <auto|json|native> | address <n> | "
                "groups [<g>...|--clear] | pins <a> <b> <c> | events [<kind>...|--none] [--push|--keep] | "
                "log [on|off] | reset]",
        .func = cmd_link,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
