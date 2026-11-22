/*
 * Scenes: checked, applied, and saved in NVS -- namespace holo_cfg, one JSON string a scene
 * under scn0..scn15 -- and the `scene` console command. See scenes.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "events.h"
#include "nvs.h"
#include "scenes.h"

#define NVS_NS "holo_cfg"

bool scene_name_ok(const char *name)
{
    const size_t n = name != NULL ? strlen(name) : 0;
    if (n == 0 || n > SCENE_NAME_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        const char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' ||
              c == '_' || c == '.' || c == '-')) {
            return false;
        }
    }
    return true;
}

scene_err_t scene_check(const cJSON *scene, char *why, size_t why_len)
{
    if (!cJSON_IsObject(scene)) {
        snprintf(why, why_len, "a scene is an object");
        return SCENE_BAD;
    }
    for (const cJSON *k = scene->child; k != NULL; k = k->next) {
        if (strcmp(k->string, "name") && strcmp(k->string, "description") && strcmp(k->string, "screen") &&
            strcmp(k->string, "leds") && strcmp(k->string, "holo") && strcmp(k->string, "then") &&
            strcmp(k->string, "duration_s") && strcmp(k->string, "slot")) {
            snprintf(why, why_len, "unknown field `%s`", k->string);
            return SCENE_BAD;
        }
    }
    const cJSON *name = cJSON_GetObjectItem(scene, "name");
    if (!cJSON_IsString(name) || !scene_name_ok(name->valuestring)) {
        snprintf(why, why_len, "a name is 1-%d letters, digits, spaces, _ . and -", SCENE_NAME_MAX);
        return SCENE_BAD;
    }
    const cJSON *desc = cJSON_GetObjectItem(scene, "description");
    if (desc != NULL && !(cJSON_IsString(desc) && strlen(desc->valuestring) <= 120)) {
        snprintf(why, why_len, "a description is text, at most 120 characters");
        return SCENE_BAD;
    }
    const cJSON *then = cJSON_GetObjectItem(scene, "then");
    if (then != NULL && scene_then_of(then) < 0) {
        snprintf(why, why_len, "`then` is stay, restore or off");
        return SCENE_BAD;
    }
    const cJSON *slot = cJSON_GetObjectItem(scene, "slot");
    if (slot != NULL && !(cJSON_IsNumber(slot) && slot->valuedouble >= 1 && slot->valuedouble <= SCENE_SLOT_MAX &&
                          slot->valuedouble == (int)slot->valuedouble)) {
        snprintf(why, why_len, "`slot` is a whole number, 1..%d", SCENE_SLOT_MAX);
        return SCENE_BAD;
    }
    const cJSON *dur = cJSON_GetObjectItem(scene, "duration_s");
    if (dur != NULL && !(cJSON_IsNumber(dur) && dur->valuedouble >= 0.1 && dur->valuedouble <= 86400)) {
        snprintf(why, why_len, "`duration_s` is 0.1..86400 seconds");
        return SCENE_BAD;
    }
    /* A scene that does something when it ends has to end: after a time, or when a clip that
     * does not loop forever is done */
    const cJSON *screen = cJSON_GetObjectItem(scene, "screen");
    if (then != NULL && scene_then_of(then) != SCENE_THEN_STAY && dur == NULL &&
        !(cJSON_IsObject(screen) && cJSON_GetObjectItem(screen, "path") != NULL &&
          !cJSON_IsTrue(cJSON_GetObjectItem(screen, "loop")))) {
        snprintf(why, why_len, "`then` needs the scene to end: give `duration_s`, or a clip on the screen that does not "
                 "loop forever");
        return SCENE_BAD;
    }
    char part[96];
    scene_err_t err;
    const cJSON *p;
    if ((p = cJSON_GetObjectItem(scene, "screen")) != NULL &&
        (err = scene_do_screen(p, true, true, part, sizeof(part))) != SCENE_OK) {
        snprintf(why, why_len, "screen: %s", part);
        return err;
    }
    if ((p = cJSON_GetObjectItem(scene, "leds")) != NULL &&
        (err = scene_do_leds(p, true, part, sizeof(part))) != SCENE_OK) {
        snprintf(why, why_len, "leds: %s", part);
        return err;
    }
    if ((p = cJSON_GetObjectItem(scene, "holo")) != NULL &&
        (err = scene_do_holo(p, true, part, sizeof(part))) != SCENE_OK) {
        snprintf(why, why_len, "holo: %s", part);
        return err;
    }
    return SCENE_OK;
}

/* The screen, then the LEDs, then the holo: checked already. For runner.c. */
scene_err_t scene_apply_parts(const cJSON *scene, char *why, size_t why_len)
{
    scene_err_t err;
    char part[96];
    const cJSON *p;
    if ((p = cJSON_GetObjectItem(scene, "screen")) != NULL &&
        (err = scene_do_screen(p, true, false, part, sizeof(part))) != SCENE_OK) {
        snprintf(why, why_len, "screen: %s", part);
        return err == SCENE_MISSING ? SCENE_UNPLAYABLE : err;
    }
    if ((p = cJSON_GetObjectItem(scene, "leds")) != NULL &&
        (err = scene_do_leds(p, false, part, sizeof(part))) != SCENE_OK) {
        snprintf(why, why_len, "leds: %s", part);
        return err;
    }
    if ((p = cJSON_GetObjectItem(scene, "holo")) != NULL &&
        (err = scene_do_holo(p, false, part, sizeof(part))) != SCENE_OK) {
        snprintf(why, why_len, "holo: %s", part);
        return err;
    }
    return SCENE_OK;
}

/* ------------------------------------------------------------------ storage */

static void idx_key(int i, char key[8])
{
    snprintf(key, 8, "scn%u", (unsigned)i % 1000u);    /* 0..SCENE_MAX-1: the bound is for the compiler */
}

/* Whether NVS entry i holds a scene by its length alone, nothing read or parsed: *len its text's, NUL and all */
static bool idx_held(nvs_handle_t h, int i, size_t *len)
{
    char key[8];
    idx_key(i, key);
    *len = 0;
    return nvs_get_str(h, key, NULL, len) == ESP_OK && *len > 0 && *len <= SCENE_JSON_MAX + 1;
}

/* Slot i's scene, or NULL. */
static cJSON *load_idx(nvs_handle_t h, int i)
{
    char key[8];
    idx_key(i, key);
    size_t len = 0;
    if (!idx_held(h, i, &len)) {
        return NULL;
    }
    char *text = malloc(len);
    cJSON *scene = NULL;
    if (text != NULL && nvs_get_str(h, key, text, &len) == ESP_OK) {
        scene = cJSON_Parse(text);
    }
    free(text);
    if (scene != NULL && !cJSON_IsString(cJSON_GetObjectItem(scene, "name"))) {
        cJSON_Delete(scene);
        scene = NULL;
    }
    return scene;
}

/* The NVS entry holding `name` (and its scene, if `out`), or -1. */
static int find(nvs_handle_t h, const char *name, cJSON **out)
{
    for (int i = 0; i < SCENE_MAX; i++) {
        cJSON *s = load_idx(h, i);
        if (s != NULL && strcmp(cJSON_GetObjectItem(s, "name")->valuestring, name) == 0) {
            if (out != NULL) {
                *out = s;
            } else {
                cJSON_Delete(s);
            }
            return i;
        }
        cJSON_Delete(s);
    }
    return -1;
}

cJSON *scene_load(const char *name)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return NULL;
    }
    cJSON *scene = NULL;
    find(h, name, &scene);
    nvs_close(h);
    return scene;
}

/* One scene parsed at a time, as find() does: the first, in NVS order (scene_load_all()'s), with that slot */
cJSON *scene_load_slot(int slot)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return NULL;
    }
    cJSON *found = NULL;
    for (int i = 0; found == NULL && i < SCENE_MAX; i++) {
        cJSON *s = load_idx(h, i);
        const cJSON *n = cJSON_GetObjectItem(s, "slot");
        if (cJSON_IsNumber(n) && n->valueint == slot) {
            found = s;
        } else {
            cJSON_Delete(s);
        }
    }
    nvs_close(h);
    return found;
}

scene_err_t scene_apply_by(const char *name, int slot, char *why, size_t why_len)
{
    cJSON *scene = slot != 0 ? scene_load_slot(slot) : name != NULL ? scene_load(name) : NULL;
    if (scene == NULL) {
        if (slot != 0 || name == NULL) {
            snprintf(why, why_len, "no scene with slot %d", slot);
        } else {
            snprintf(why, why_len, "no scene '%s'", name);
        }
        return SCENE_MISSING;
    }
    const scene_err_t err = scene_apply(scene, why, why_len);
    cJSON_Delete(scene);
    return err;
}

/* The scenes counted by their NVS entries' lengths alone (idx_held()), in scene_load_all()'s order, and only the
 * index-th read and parsed */
int scene_list_entry(int index, char *name, size_t name_len, int *slot)
{
    name[0] = '\0';
    *slot = 0;
    nvs_handle_t h;
    const esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 0;           /* nothing saved yet */
    }
    if (err != ESP_OK) {
        return SCENE_LIST_UNREADABLE;
    }
    int count = 0, at = -1;
    for (int i = 0; i < SCENE_MAX; i++) {
        size_t len;
        if (idx_held(h, i, &len)) {
            at = count == index ? i : at;
            count++;
        }
    }
    int ret = count;
    if (at >= 0) {
        cJSON *s = load_idx(h, at);
        if (s == NULL) {
            ret = SCENE_LIST_UNREADABLE;    /* not parsed: corrupt, or no memory for it now */
        } else {
            strlcpy(name, cJSON_GetObjectItem(s, "name")->valuestring, name_len);
            const cJSON *n = cJSON_GetObjectItem(s, "slot");
            *slot = cJSON_IsNumber(n) ? n->valueint : 0;
            cJSON_Delete(s);
        }
    }
    nvs_close(h);
    return ret;
}

cJSON *scene_load_all(void)
{
    cJSON *all = cJSON_CreateArray();
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return all;
    }
    for (int i = 0; i < SCENE_MAX; i++) {
        cJSON *s = load_idx(h, i);
        if (s != NULL) {
            cJSON_AddItemToArray(all, s);
        }
    }
    nvs_close(h);
    return all;
}

scene_err_t scene_store(const cJSON *scene, bool *replaced, char *why, size_t why_len)
{
    scene_err_t err = scene_check(scene, why, why_len);
    if (err != SCENE_OK) {
        return err;
    }
    char *text = cJSON_PrintUnformatted(scene);
    if (text == NULL || strlen(text) > SCENE_JSON_MAX) {
        cJSON_free(text);
        snprintf(why, why_len, "a scene is at most %d characters of JSON", SCENE_JSON_MAX);
        return SCENE_BAD;
    }
    nvs_handle_t h;
    esp_err_t e = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) {
        cJSON_free(text);
        snprintf(why, why_len, "cannot open the settings: %s", esp_err_to_name(e));
        return SCENE_FAILED;
    }
    const char *name = cJSON_GetObjectItem(scene, "name")->valuestring;
    const cJSON *slot = cJSON_GetObjectItem(scene, "slot");
    for (int i = 0; slot != NULL && i < SCENE_MAX; i++) {
        cJSON *other = load_idx(h, i);
        const cJSON *on = cJSON_GetObjectItem(other, "name");
        const cJSON *os = cJSON_GetObjectItem(other, "slot");
        const bool taken = cJSON_IsNumber(os) && os->valueint == slot->valueint && cJSON_IsString(on) &&
                           strcmp(on->valuestring, name) != 0;
        if (taken) {
            snprintf(why, why_len, "slot %d is taken by '%s'", slot->valueint, on->valuestring);
        }
        cJSON_Delete(other);
        if (taken) {
            nvs_close(h);
            cJSON_free(text);
            return SCENE_TAKEN;
        }
    }
    int idx = find(h, name, NULL);
    *replaced = idx >= 0;
    for (int i = 0; idx < 0 && i < SCENE_MAX; i++) {
        char key[8];
        size_t len = 0;
        idx_key(i, key);
        if (nvs_get_str(h, key, NULL, &len) != ESP_OK) {
            idx = i;
        }
    }
    if (idx < 0) {
        nvs_close(h);
        cJSON_free(text);
        snprintf(why, why_len, "%d scenes are saved; delete one first", SCENE_MAX);
        return SCENE_FULL;
    }
    char key[8];
    idx_key(idx, key);
    e = nvs_set_str(h, key, text);
    if (e == ESP_OK) {
        e = nvs_commit(h);
    }
    nvs_close(h);
    cJSON_free(text);
    if (e != ESP_OK) {
        snprintf(why, why_len, "not saved: %s", esp_err_to_name(e));
        return SCENE_FAILED;
    }
    events_changed("scenes");
    return SCENE_OK;
}

scene_err_t scene_remove(const char *name)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return SCENE_MISSING;
    }
    const int idx = find(h, name, NULL);
    if (idx >= 0) {
        char key[8];
        idx_key(idx, key);
        nvs_erase_key(h, key);
        nvs_commit(h);
    }
    nvs_close(h);
    if (idx < 0) {
        return SCENE_MISSING;
    }
    events_changed("scenes");
    settings_t s;
    settings_get(&s);
    if (strcmp(s.boot_scene, name) == 0) {
        s.boot_scene[0] = '\0';
        settings_save(&s);
    }
    return SCENE_OK;
}

/* ------------------------------------------------------------------ console */

/* argv[from..] joined with spaces: a scene's name may have them. */
static void join(int argc, char **argv, int from, char *out, size_t len)
{
    out[0] = '\0';
    for (int i = from; i < argc; i++) {
        snprintf(out + strlen(out), len - strlen(out), "%s%s", i > from ? " " : "", argv[i]);
    }
}

/* scene slot <name> [<1-255>|--clear]: a name may have spaces, so the number is the last word */
static int cmd_slot(int argc, char **argv)
{
    const char *last = argv[argc - 1];
    char *end = NULL;
    const long n = strtol(last, &end, 10);
    const bool clear = strcmp(last, "--clear") == 0;
    const bool set = clear || (*end == '\0' && end != last);
    char name[64];
    join(set ? argc - 1 : argc, argv, 2, name, sizeof(name));
    cJSON *scene = scene_load(name);
    if (scene == NULL) {
        printf("scene: no scene '%s'\n", name);
        return 1;
    }
    if (!set) {
        const cJSON *sl = cJSON_GetObjectItem(scene, "slot");
        if (cJSON_IsNumber(sl)) {
            printf("'%s' is slot %d\n", name, sl->valueint);
        } else {
            printf("'%s' has no slot\n", name);
        }
        cJSON_Delete(scene);
        return 0;
    }
    if (!clear && (n < 1 || n > SCENE_SLOT_MAX)) {
        printf("scene: a slot is 1..%d\n", SCENE_SLOT_MAX);
        cJSON_Delete(scene);
        return 1;
    }
    cJSON_DeleteItemFromObject(scene, "slot");
    if (!clear) {
        cJSON_AddNumberToObject(scene, "slot", n);
    }
    bool replaced;
    char why[128];
    const scene_err_t err = scene_store(scene, &replaced, why, sizeof(why));
    cJSON_Delete(scene);
    if (err != SCENE_OK) {
        printf("scene: %s\n", why);
        return 1;
    }
    if (clear) {
        printf("'%s' has no slot\n", name);
    } else {
        printf("'%s' is slot %ld\n", name, n);
    }
    return 0;
}

static int cmd_scene(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "list";
    char name[64];
    join(argc, argv, 2, name, sizeof(name));
    settings_t st;
    settings_get(&st);

    if (strcmp(sub, "list") == 0 && argc <= 2) {
        cJSON *all = scene_load_all();
        const cJSON *s;
        int n = 0;
        cJSON_ArrayForEach(s, all) {
            const char *nm = cJSON_GetObjectItem(s, "name")->valuestring;
            const cJSON *d = cJSON_GetObjectItem(s, "description");
            const cJSON *sl = cJSON_GetObjectItem(s, "slot");
            char num[8] = "";
            if (cJSON_IsNumber(sl)) {
                snprintf(num, sizeof(num), "[%d] ", sl->valueint);
            }
            printf("%s%s%s%s%s\n", num, nm, strcmp(nm, st.boot_scene) == 0 ? " (at start)" : "",
                   cJSON_IsString(d) ? ": " : "", cJSON_IsString(d) ? d->valuestring : "");
            n++;
        }
        cJSON_Delete(all);
        if (n == 0) {
            printf("no scenes: save them with the HTTP API (PUT /api/v1/scenes/scene) or the web app\n");
        }
        scene_active_t a;
        if (scene_active(&a)) {
            printf("running: %s, then %s %s\n", a.name, SCENE_THEN_NAMES[a.then],
                   a.remaining_s >= 0 ? "when its time is up" : "when its clip is done");
        }
        return 0;
    }
    if (strcmp(sub, "end") == 0 && argc == 2) {
        char why[128];
        if (scene_end(why, sizeof(why)) != SCENE_OK) {
            printf("scene: %s\n", why);
            return 1;
        }
        printf("ended\n");
        return 0;
    }
    if (strcmp(sub, "boot") == 0) {
        if (argc == 2) {
            printf("at start: %s\n", st.boot_scene[0] ? st.boot_scene : "nothing (--clear)");
            return 0;
        }
        const bool clear = strcmp(name, "--clear") == 0;
        cJSON *scene = clear ? NULL : scene_load(name);
        if (!clear && scene == NULL) {
            printf("scene: no scene '%s'\n", name);
            return 1;
        }
        cJSON_Delete(scene);
        strlcpy(st.boot_scene, clear ? "" : name, sizeof(st.boot_scene));
        if (settings_save(&st) != ESP_OK) {
            printf("scene: not saved\n");
            return 1;
        }
        printf(clear ? "the board starts with nothing\n" : "the board starts with '%s'\n", name);
        return 0;
    }
    if (strcmp(sub, "slot") == 0 && argc >= 3) {
        return cmd_slot(argc, argv);
    }
    if (argc < 3 || (strcmp(sub, "show") && strcmp(sub, "apply") && strcmp(sub, "delete"))) {
        printf("usage: scene [list] | show <name> | apply <name> | end | delete <name> | boot [<name>|--clear] | "
               "slot <name> [<1-255>|--clear]\n");
        return 1;
    }
    if (strcmp(sub, "delete") == 0) {
        if (scene_remove(name) != SCENE_OK) {
            printf("scene: no scene '%s'\n", name);
            return 1;
        }
        printf("deleted '%s'\n", name);
        return 0;
    }
    if (strcmp(sub, "apply") == 0) {
        char why[128];
        if (scene_apply_by(name, 0, why, sizeof(why)) != SCENE_OK) {
            printf("scene: %s\n", why);
            return 1;
        }
        printf("applied '%s'\n", name);
        return 0;
    }
    cJSON *scene = scene_load(name);
    if (scene == NULL) {
        printf("scene: no scene '%s'\n", name);
        return 1;
    }
    char *text = cJSON_Print(scene);
    printf("%s\n", text != NULL ? text : "?");
    cJSON_free(text);
    cJSON_Delete(scene);
    return 0;
}

void scene_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "scene",
        .help = "Saved scenes -- what the screen, the LEDs and the holo do together: list them, show one, apply "
                "it, end the one running (doing what it does when it ends), delete one, choose the one the "
                "board starts with, or give one the slot number a host applies it by. They are made with the "
                "HTTP API or the web app.",
        .hint = "[list] | show <name> | apply <name> | end | delete <name> | boot [<name>|--clear] | "
                "slot <name> [<1-255>|--clear]",
        .func = cmd_scene,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
