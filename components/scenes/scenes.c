/*
 * Scenes: checked, applied, and saved in NVS -- namespace holo_cfg, one JSON string a scene
 * under scn0..scn15 -- and the `scene` console command. See scenes.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
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
            strcmp(k->string, "duration_s")) {
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

static void slot_key(int i, char key[8])
{
    snprintf(key, 8, "scn%d", i);
}

/* Slot i's scene, or NULL. */
static cJSON *load_slot(nvs_handle_t h, int i)
{
    char key[8];
    slot_key(i, key);
    size_t len = 0;
    if (nvs_get_str(h, key, NULL, &len) != ESP_OK || len == 0 || len > SCENE_JSON_MAX + 1) {
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

/* The slot holding `name` (and its scene, if `out`), or -1. */
static int find(nvs_handle_t h, const char *name, cJSON **out)
{
    for (int i = 0; i < SCENE_MAX; i++) {
        cJSON *s = load_slot(h, i);
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

cJSON *scene_load_all(void)
{
    cJSON *all = cJSON_CreateArray();
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return all;
    }
    for (int i = 0; i < SCENE_MAX; i++) {
        cJSON *s = load_slot(h, i);
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
    int slot = find(h, cJSON_GetObjectItem(scene, "name")->valuestring, NULL);
    *replaced = slot >= 0;
    for (int i = 0; slot < 0 && i < SCENE_MAX; i++) {
        char key[8];
        size_t len = 0;
        slot_key(i, key);
        if (nvs_get_str(h, key, NULL, &len) != ESP_OK) {
            slot = i;
        }
    }
    if (slot < 0) {
        nvs_close(h);
        cJSON_free(text);
        snprintf(why, why_len, "%d scenes are saved; delete one first", SCENE_MAX);
        return SCENE_FULL;
    }
    char key[8];
    slot_key(slot, key);
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
    return SCENE_OK;
}

scene_err_t scene_remove(const char *name)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return SCENE_MISSING;
    }
    const int slot = find(h, name, NULL);
    if (slot >= 0) {
        char key[8];
        slot_key(slot, key);
        nvs_erase_key(h, key);
        nvs_commit(h);
    }
    nvs_close(h);
    if (slot < 0) {
        return SCENE_MISSING;
    }
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
            printf("%s%s%s%s\n", nm, strcmp(nm, st.boot_scene) == 0 ? " (at start)" : "",
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
    if (argc < 3 || (strcmp(sub, "show") && strcmp(sub, "apply") && strcmp(sub, "delete"))) {
        printf("usage: scene [list] | show <name> | apply <name> | end | delete <name> | boot [<name>|--clear]\n");
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
    cJSON *scene = scene_load(name);
    if (scene == NULL) {
        printf("scene: no scene '%s'\n", name);
        return 1;
    }
    int rc = 0;
    if (strcmp(sub, "show") == 0) {
        char *text = cJSON_Print(scene);
        printf("%s\n", text != NULL ? text : "?");
        cJSON_free(text);
    } else {
        char why[128];
        if (scene_apply(scene, why, sizeof(why)) != SCENE_OK) {
            printf("scene: %s\n", why);
            rc = 1;
        } else {
            printf("applied '%s'\n", name);
        }
    }
    cJSON_Delete(scene);
    return rc;
}

void scene_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "scene",
        .help = "Saved scenes -- what the screen, the LEDs and the holo do together: list them, show one, apply "
                "it, end the one running (doing what it does when it ends), delete one, or choose the one the "
                "board starts with. They are made with the HTTP API or the web app.",
        .hint = "[list] | show <name> | apply <name> | end | delete <name> | boot [<name>|--clear]",
        .func = cmd_scene,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
