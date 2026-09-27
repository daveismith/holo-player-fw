/*
 * webui: Holo Player's web app. The pages are web/ (embedded by embed_assets.py); the API is
 * esp-console-kit's web_server and web_ota; this ties them together and adds release channels.
 */
#include "ota_pull.h"
#include "web_fs.h"
#include "web_ota.h"
#include "web_server.h"
#include "webui.h"
#include "api.h"

extern const web_asset_t webui_assets[];
extern const size_t webui_asset_count;

esp_err_t webui_release_resolve(const char *channel, char *manifest_url, size_t url_len, char *why,
                                size_t why_len);

esp_err_t webui_start(void)
{
    ota_pull_set_resolver(webui_release_resolve);
    esp_err_t err = web_ota_register();
    err |= web_fs_register();
    err |= api_screen_register();
    const web_server_config_t cfg = {
        .name_prefix = "holo",
        .product = "Holo Player",
        .assets = webui_assets,
        .n_assets = webui_asset_count,
    };
    const esp_err_t start = web_server_start(&cfg);
    return start != ESP_OK ? start : err;
}
