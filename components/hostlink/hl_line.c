/*
 * JSON lines: one request a line, answered with one reply line, over the board's API routes
 * (the kit's api_core) that are marked API_LINK. See manual/reference/host-protocol.md.
 *
 *   [@<address> ][#<tag> ]<METHOD> <path>[ <JSON>]
 *   [@<address> ][#<tag> ]<status>[ +<pending>][ <JSON>]
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"
#include "api_core.h"
#include "hl.h"

#define PREFIX "/api/v1"

typedef enum { TO_US, TO_ALL, TO_GROUP, TO_OTHER } target_t;

/* A word of `s`, up to a space: its length, and `s` moved past it and the spaces after. */
static size_t word(const char **s)
{
    const char *p = *s;
    while (*p && *p != ' ') {
        p++;
    }
    const size_t n = (size_t)(p - *s);
    while (*p == ' ') {
        p++;
    }
    *s = p;
    return n;
}

static target_t target_of(const char *w, size_t n, const hl_settings_t *run, bool *ok)
{
    *ok = true;
    if (n == 1 && w[0] == '*') {
        return TO_ALL;
    }
    char num[8];
    const bool group = n > 1 && w[0] == 'g';
    const char *digits = group ? w + 1 : w;
    const size_t dn = group ? n - 1 : n;
    if (dn == 0 || dn >= sizeof(num)) {
        *ok = false;
        return TO_OTHER;
    }
    for (size_t i = 0; i < dn; i++) {
        if (!isdigit((unsigned char)digits[i])) {
            *ok = false;
            return TO_OTHER;
        }
    }
    memcpy(num, digits, dn);
    num[dn] = '\0';
    const int v = atoi(num);
    if (group) {
        for (int i = 0; i < run->n_groups; i++) {
            if (run->groups[i] == v) {
                return TO_GROUP;
            }
        }
        return TO_OTHER;
    }
    return v == run->address ? TO_US : TO_OTHER;
}

static char *reply_line(const char *prefix, int status, cJSON *body)
{
    char *json = body != NULL ? cJSON_PrintUnformatted(body) : NULL;
    cJSON_Delete(body);
    const uint32_t pending = hl_events_pending();
    char head[64];
    int n = snprintf(head, sizeof(head), "%s%d", prefix, status);
    if (pending > 0) {
        n += snprintf(head + n, sizeof(head) - n, " +%u", (unsigned)pending);
    }
    const size_t len = (size_t)n + (json ? strlen(json) + 1 : 0) + 1;
    char *out = malloc(len);
    if (out != NULL) {
        snprintf(out, len, "%s%s%s", head, json ? " " : "", json ? json : "");
    }
    cJSON_free(json);
    return out;
}

static char *error_line(const char *prefix, int status, const char *code, const char *message)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", code);
    cJSON_AddStringToObject(o, "message", message);
    return reply_line(prefix, status, o);
}

char *hl_line_handle(const char *line, const hl_settings_t *run)
{
    hl_counters_t *c = hl_counters();
    const char *p = line;
    while (*p == ' ') {
        p++;
    }
    if (*p == '\0') {
        return NULL;
    }
    char prefix[40] = "";
    target_t to = TO_US;

    /* @<address> */
    if (*p == '@') {
        const char *w = p + 1;
        const size_t n = word(&p) - 1;
        bool ok;
        to = target_of(w, n, run, &ok);
        if (!ok || to == TO_OTHER) {
            c->ignored += ok ? 0 : 1;
            return NULL;                        /* another board's */
        }
        snprintf(prefix, sizeof(prefix), "@%d ", run->address);
    } else if (run->mode == HL_RS485) {
        c->ignored++;
        return NULL;                            /* on a bus, a request names its board */
    }
    const bool answer = to == TO_US || run->mode != HL_RS485;

    /* #<tag> */
    if (*p == '#') {
        const char *w = p;
        const size_t n = word(&p);
        bool ok = n >= 2 && n <= 17;
        for (size_t i = 1; ok && i < n; i++) {
            ok = isalnum((unsigned char)w[i]) || w[i] == '_' || w[i] == '-';
        }
        if (!ok) {
            c->bad_lines++;
            return answer ? error_line(prefix, 400, "bad_request", "a #tag is 1-16 of A-Z a-z 0-9 _ -") : NULL;
        }
        snprintf(prefix + strlen(prefix), sizeof(prefix) - strlen(prefix), "%.*s ", (int)n, w);
    }

    /* METHOD path[?query][ JSON] */
    char method[8];
    const char *w = p;
    size_t n = word(&p);
    api_method_t m;
    if (n == 0 || n >= sizeof(method) || (snprintf(method, sizeof(method), "%.*s", (int)n, w), !api_method_of(method, &m))) {
        c->bad_lines++;
        return answer ? error_line(prefix, 400, "bad_request", "a request is <METHOD> <path>[ <JSON>]: GET, PUT, POST, PATCH or DELETE") : NULL;
    }
    w = p;
    n = word(&p);
    if (n == 0 || w[0] != '/') {
        c->bad_lines++;
        return answer ? error_line(prefix, 400, "bad_request", "a path starts with /: GET /screen") : NULL;
    }
    char path[160];
    char query[256] = "";
    const char *q = memchr(w, '?', n);
    const size_t plen = q ? (size_t)(q - w) : n;
    const bool full = plen >= strlen(PREFIX) && strncmp(w, PREFIX, strlen(PREFIX)) == 0;
    snprintf(path, sizeof(path), "%s%.*s", full ? "" : PREFIX, (int)plen, w);
    if (q != NULL) {
        snprintf(query, sizeof(query), "%.*s", (int)(n - plen - 1), q + 1);
    }

    bool known = false;
    const api_route_t *r = api_find(m, path, &known);
    if (r == NULL || !(r->flags & API_LINK)) {
        c->requests++;
        char msg[200];
        snprintf(msg, sizeof(msg), "%s %s is not served over the link", method, path + strlen(PREFIX));
        return answer ? error_line(prefix, 404, "not_found", msg) : NULL;
    }
    cJSON *body = NULL;
    if (*p != '\0') {
        if (r->body_max > 0 && strlen(p) > r->body_max) {
            c->requests++;
            return answer ? error_line(prefix, 413, "too_large", "the body is too large for this request") : NULL;
        }
        body = cJSON_Parse(p);
        if (!cJSON_IsObject(body)) {
            cJSON_Delete(body);
            c->bad_lines++;
            return answer ? error_line(prefix, 400, "bad_request", "the body is not a JSON object, on the same line") : NULL;
        }
    } else {
        body = cJSON_CreateObject();
    }
    const api_req_t req = { .query = query[0] ? query : NULL, .body = body, .via = "link" };
    const api_reply_t reply = r->fn(&req);
    cJSON_Delete(body);
    c->requests++;
    c->last_request_us = esp_timer_get_time();
    if (!answer) {
        cJSON_Delete(reply.body);
        return NULL;
    }
    return reply_line(prefix, reply.status, reply.body);
}
