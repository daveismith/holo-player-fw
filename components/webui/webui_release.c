/*
 * Release channels: the documentation site publishes each release's firmware, with a manifest
 * (tools/web_install_manifest.py), under its docs version -- 1.0/firmware/manifest.json. The
 * channels are the site's versions and their aliases, listed in versions.json (written by mike):
 * `latest` is an alias of the newest release. The alias directory itself holds only HTML
 * redirects, so latest/firmware/manifest.json does not exist; hence the lookup.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_err.h"
#include "ota_pull.h"
#include "sdkconfig.h"

esp_err_t webui_release_resolve(const char *channel, char *manifest_url, size_t url_len, char *why,
                                size_t why_len)
{
    const char *base = CONFIG_WEBUI_RELEASES_URL;
    const char *sep = base[0] && base[strlen(base) - 1] == '/' ? "" : "/";
    char url[200];
    snprintf(url, sizeof(url), "%s%sversions.json", base, sep);
    char *text = NULL;
    esp_err_t err = ota_pull_fetch(url, &text, 8 * 1024, why, why_len);
    if (err != ESP_OK) {
        return err;
    }
    cJSON *versions = cJSON_Parse(text);
    free(text);
    const char *found = NULL;
    const cJSON *v;
    cJSON_ArrayForEach(v, versions) {
        const cJSON *name = cJSON_GetObjectItem(v, "version");
        if (!cJSON_IsString(name)) {
            continue;
        }
        if (strcmp(name->valuestring, channel) == 0) {
            found = name->valuestring;
        }
        const cJSON *alias;
        cJSON_ArrayForEach(alias, cJSON_GetObjectItem(v, "aliases")) {
            if (cJSON_IsString(alias) && strcmp(alias->valuestring, channel) == 0) {
                found = name->valuestring;
            }
        }
        if (found != NULL) {
            break;
        }
    }
    if (found == NULL) {
        snprintf(why, why_len, "no release channel `%.24s` on %.80s", channel, base);
        err = cJSON_IsArray(versions) ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_RESPONSE;
    } else {
        snprintf(manifest_url, url_len, "%s%s%s/firmware/manifest.json", base, sep, found);
    }
    cJSON_Delete(versions);
    return err;
}
