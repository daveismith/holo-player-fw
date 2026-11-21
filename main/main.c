/*
 * holo-player-fw: bring-up for the Waveshare ESP32-S3-Touch-LCD-1.28, with a UART console
 * carrying the esp-console-kit command set.
 *
 * Started from the ESP-IDF console_advanced example (Unlicense OR CC0-1.0).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "esp_event.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "linenoise/linenoise.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ota_pull.h"
#include "soc/soc_caps.h"
#include "board.h"
#include "cmd_fs.h"
#include "fs_ops.h"
#include "cmd_ota.h"
#include "ota_core.h"
#include "console_history.h"
#include "holo_servos.h"
#include "hostlink.h"
#include "leds.h"
#include "scenes.h"
#include "cmd_i2ctools.h"
#include "cmd_network.h"
#include "cmd_nvs.h"
#include "cmd_system.h"
#include "cmd_wifi.h"
#include "cmd_can.h"
#include "cyphal.h"
#include "holo_cyphal.h"
#include "r2/holo/ports.h"
#include "console_settings.h"
#include "video_player.h"
#include "web_server.h"
#include "webui.h"
#include "wifi_known.h"
#include "wifi_fallback.h"

/*
 * We warn if a secondary serial console is enabled. A secondary serial console is always output-only and
 * hence not very useful for interactive console applications. If you encounter this warning, consider disabling
 * the secondary serial console in menuconfig unless you know what you are doing.
 */
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#if !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#warning "A secondary serial console is not useful when using the console component. Please disable it in menuconfig."
#endif
#endif

static const char *TAG = "holo";
#define PROMPT_STR "holo"

/* The LittleFS `storage` partition: command history now, the video clips later. */
#define MOUNT_PATH    "/data"
#define STORAGE_LABEL "storage"
/* Where `nvs_export <name>` writes, and `nvs_import`'s snapshot of the configuration before it */
#define BACKUP_DIR    MOUNT_PATH "/backup"

#if CONFIG_CONSOLE_STORE_HISTORY
#define HISTORY_PATH MOUNT_PATH "/history.txt"

#define HISTORY_LINES 100   /* as console_settings.c gives linenoise */

/*
 * Saving the history writes flash, which stalls both cores until the write is done; during a
 * clip that shows as late frames. So while one plays, the save waits: it happens when the
 * screen turns off (the clip ends by itself, `video stop`, `screen clear`), or after the first
 * command run with no clip playing (a colour shown in the clip's place keeps the screen on).
 * Either way the console task does the writing.
 */
static void save_history_if_idle(void)
{
    if (!video_playing()) {
        console_history_save();
    }
}

/* From whichever task turned the screen off -- the player's, when a clip ends by itself --
 * so it only asks: the console task saves when it is next waiting for a keystroke. */
static void on_screen_off(void *ctx)
{
    (void)ctx;
    console_history_request_save();
}
#else
#define HISTORY_PATH NULL
#endif

/* Pins the `gpio` command refuses to drive: everything this board has wired to something,
 * the strapping pins, and the flash/PSRAM bus. Free pins are on the P2 header: 15, 16, 17,
 * 18, 21 and 33. */
static const gpio_reserved_t s_gpio_reserved[] = {
    { 0, "BOOT strap and button" },
    { BOARD_BAT_ADC_GPIO, "battery ADC" },
    { BOARD_LCD_BL, "LCD backlight" },
    { BOARD_IMU_INT2, "IMU INT2" },
    { BOARD_IMU_INT1, "IMU INT1" },
    { BOARD_TOUCH_INT, "touch INT" },
    { BOARD_I2C_SDA, "I2C SDA" },
    { BOARD_I2C_SCL, "I2C SCL" },
    { BOARD_LCD_DC, "LCD DC" },
    { BOARD_LCD_CS, "LCD CS" },
    { BOARD_LCD_SCLK, "LCD SCLK" },
    { BOARD_LCD_MOSI, "LCD MOSI" },
    { BOARD_LCD_MISO, "LCD MISO" },
    { BOARD_TOUCH_RST, "touch RST" },
    { BOARD_LCD_RST, "LCD RST" },
    { CONFIG_LEDS_GPIO, "LED strip data" },
    { CONFIG_HOLO_SERVO1_GPIO, "holo servo 1" },
    { CONFIG_HOLO_SERVO2_GPIO, "holo servo 2" },
    { 26, "flash/PSRAM SPI" }, { 27, "flash/PSRAM SPI" }, { 28, "flash/PSRAM SPI" },
    { 29, "flash/PSRAM SPI" }, { 30, "flash/PSRAM SPI" }, { 31, "flash/PSRAM SPI" },
    { 32, "flash/PSRAM SPI" },
    { 43, "UART0 TX (the console)" },
    { 44, "UART0 RX (the console)" },
    { 45, "VDD_SPI strap" },
    { 46, "boot mode strap" },
};

/*
 * What the `gpio` command refuses: the table above, then three entries for the pins the host link
 * holds (its transport's, or the Cyphal node's in `link mode can`) and two for `can`'s, each -1
 * (matching no pin) while nothing holds it, and named for whoever does.
 *
 * The kit's register_gpio() keeps this pointer, not a copy, and walks the table at each `gpio`;
 * link_pins_held() and can_pins_held() change their entries after registration. So the table must
 * stay where it is and as long as it was registered, and only those five entries may change.
 */
#define LINK_PIN_COUNT HOSTLINK_PIN_ROLES   /* the host link's A, B and C */
#define CAN_PIN_COUNT  2                    /* `can`'s TX and RX */
#define BOARD_PIN_COUNT (sizeof(s_gpio_reserved) / sizeof(s_gpio_reserved[0]))
static gpio_reserved_t s_reserved[BOARD_PIN_COUNT + LINK_PIN_COUNT + CAN_PIN_COUNT];
static gpio_reserved_t *const s_link_reserved = &s_reserved[BOARD_PIN_COUNT];
static gpio_reserved_t *const s_can_reserved = &s_reserved[BOARD_PIN_COUNT + LINK_PIN_COUNT];

/* From hostlink_start(), in app_main before the console's loop runs `gpio` */
static void link_pins_held(const hostlink_pin_t pins[LINK_PIN_COUNT], bool held)
{
    for (int i = 0; i < LINK_PIN_COUNT; i++) {
        s_link_reserved[i] = (gpio_reserved_t){ held ? pins[i].gpio : -1, pins[i].what };
    }
}

/* `can` goes on the host link's pins A and B, and only while the link isn't running on them */
static const char *can_pins(int *tx, int *rx)
{
    return hostlink_lend_pins(tx, rx);
}

/* From `can` on the console's task, so `gpio` never sees the table half-changed */
static void can_pins_held(int tx, int rx, bool held)
{
    s_can_reserved[0] = (gpio_reserved_t){ held ? tx : -1, "CAN TX, while `can` runs" };
    s_can_reserved[1] = (gpio_reserved_t){ held ? rx : -1, "CAN RX, while `can` runs" };
}

/* An update is about to write flash, which stalls both cores for each write: a clip would
 * stutter through it, so it stops, and the screen with it. */
static void before_update(void *ctx)
{
    (void)ctx;
    if (video_playing()) {
        screen_clear();
    }
}

/* A file is about to be replaced, moved or deleted (the console's `fs`, or the API): a clip
 * playing from it stops first. */
static void before_file_change(const char *path, void *ctx)
{
    (void)ctx;
    screen_release(path);
}

static void initialize_filesystem(void)
{
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = MOUNT_PATH,
        .partition_label = STORAGE_LABEL,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS (%s)", esp_err_to_name(err));
        return;
    }
    size_t total = 0, used = 0;
    esp_littlefs_info(STORAGE_LABEL, &total, &used);
    ESP_LOGI(TAG, "LittleFS on %s: %u of %u KB used", MOUNT_PATH,
             (unsigned)(used / 1024), (unsigned)(total / 1024));
}

/* For `fs df` and the space check before an upload. */
static esp_err_t storage_info(void *ctx, size_t *total, size_t *used)
{
    return esp_littlefs_info((const char *)ctx, total, used);
}

/* `nvs_import <url>`'s download, through the HTTP client that release checks use. One byte past
 * the cap is asked for, so a file that is too big is refused rather than cut short. It runs on
 * the console's task: an HTTPS fetch left 3.5 KB of its 8 KB stack unused (2026-10-22). */
static esp_err_t backup_fetch(const char *url, size_t max_bytes, char **body, size_t *len, char *why,
                              size_t why_len)
{
    esp_err_t err = ota_pull_fetch(url, body, max_bytes + 1, why, why_len);
    if (err == ESP_OK) {
        *len = strlen(*body);
    }
    if (err == ESP_ERR_INVALID_SIZE || (err == ESP_OK && *len > max_bytes)) {
        free(*body);
        *body = NULL;
        snprintf(why, why_len, "over %u bytes: a backup is never that big", (unsigned)max_bytes);
        return ESP_ERR_INVALID_SIZE;
    }
    return err;
}

/*
 * The Cyphal node's part of this board (components/cyphal; manual/reference/cyphal.md): the holo's
 * ports, whose IDs are registers with these defaults (r2_cyphal_dsdl's ports/holo.csv, through the
 * generated r2/holo/ports.h), what the node does with those built so far (holo_cyphal.c), and what
 * uavcan.node.ExecuteCommand's restart, factory reset and identify do, through the code the HTTP API's
 * routes use (a restart as POST /api/v1/restart's, after the response: ota_core_restart_after()). These
 * run on the node's worker task.
 */
static const cyphal_port_t s_holo_ports[] = { R2_HOLO_PORTS(CYPHAL_PORT_ENTRY) };
_Static_assert(R2_HOLO_PORT_COUNT <= CYPHAL_PORTS_MAX, "the node keeps CYPHAL_PORTS_MAX ports' IDs at most");
_Static_assert(sizeof(s_holo_ports) / sizeof(s_holo_ports[0]) == R2_HOLO_PORT_COUNT, "one entry a port");

/* Beyond the node's own registers: the board's settings (the boot scene, the backlight, the LEDs'
 * brightness and count), as DELETE /api/v1/settings does. The scenes, the files, the servos'
 * calibration and drive policy, the network and the web app's settings, and the host link's are kept,
 * so that the node comes back on the bus. All or nothing, as the node asks: if the reset doesn't save,
 * what was saved before is put back (NVS writes each value as it is set) */
static esp_err_t cyphal_factory_reset(void)
{
    settings_t was, s;
    settings_get(&was);
    const esp_err_t err = settings_reset(&s);
    if (err != ESP_OK && settings_save(&was) != ESP_OK) {
        ESP_LOGE(TAG, "factory reset: the board's settings couldn't be put back either");
    }
    return err;
}

/* Five seconds of the crosshair and the ring blinking white, as a scene that then restores what the
 * screen, the ring and the holo were doing */
static esp_err_t cyphal_identify(void)
{
    cJSON *scene = cJSON_Parse("{\"name\": \"identify\", \"screen\": {\"calibration\": true},"
                               " \"leds\": {\"mode\": \"blink\", \"colour\": \"white\"},"
                               " \"then\": \"restore\", \"duration_s\": 5}");
    if (scene == NULL) {
        return ESP_ERR_NO_MEM;
    }
    char why[128];
    const scene_err_t err = scene_apply(scene, why, sizeof(why));
    cJSON_Delete(scene);
    if (err != SCENE_OK) {
        ESP_LOGW(TAG, "identify: %s", why);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static bool any_network(const char *ssid, bool has_passphrase, bool last, void *ctx)
{
    (void)ssid;
    (void)has_passphrase;
    (void)last;
    *(bool *)ctx = true;
    return false;
}

/* After an import: a backup never holds a Wi-Fi network, so a wiped board has none. */
static const char *import_note(void)
{
    bool any = false;
    wifi_known_list(any_network, &any);
    return any ? NULL
                 : "no saved Wi-Fi network here, and a backup never holds one: join again with "
                   "wifi_save, or on the web app's Network page over the access point";
}

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void initialize_board(void)
{
    ESP_ERROR_CHECK(board_init());

    /* Asleep, backlight off, until a clip or a colour is shown. */
    esp_err_t err = board_lcd_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD: %s", esp_err_to_name(err));
    }

    err = board_imu_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "IMU: %s", esp_err_to_name(err));
    }

    /* The NeoPixel strip on P2, off, as long as the settings say */
    leds_set_count(settings_led_count());
    err = leds_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LEDs: %s", esp_err_to_name(err));
    }

    /* The holo's servos on P2, limp until the first move */
    err = holo_servos_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "holo: %s", esp_err_to_name(err));
    }

#if CONFIG_BOARD_TOUCH_AUTOSTART
    err = board_touch_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch: %s", esp_err_to_name(err));
    }
#endif
}

void app_main(void)
{
    initialize_nvs();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    initialize_filesystem();
    initialize_board();

    /* Initialize console output periheral (UART, USB_OTG, USB_JTAG) */
    initialize_console_peripheral();

    /* Initialize linenoise library and esp_console*/
    initialize_console_library(HISTORY_PATH);
#if CONFIG_CONSOLE_STORE_HISTORY
    if (console_history_init(HISTORY_PATH, HISTORY_LINES) == ESP_OK) {
        board_lcd_set_off_hook(on_screen_off, NULL);
    }
#endif

    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    const char *prompt = setup_prompt(PROMPT_STR ">");

    /* Register commands */
    esp_err_t err;
    esp_console_register_help_command();
    register_system_common();
    /* The board's own pins; the host link's and `can`'s, none yet, are filled in as they are held */
    memcpy(s_reserved, s_gpio_reserved, sizeof(s_gpio_reserved));
    for (size_t i = BOARD_PIN_COUNT; i < sizeof(s_reserved) / sizeof(s_reserved[0]); i++) {
        s_reserved[i] = (gpio_reserved_t){ -1, NULL };
    }
    register_gpio(s_reserved, sizeof(s_reserved) / sizeof(s_reserved[0]));
#if SOC_LIGHT_SLEEP_SUPPORTED
    register_system_light_sleep();
#endif
#if SOC_DEEP_SLEEP_SUPPORTED
    register_system_deep_sleep();
#endif
    register_wifi();
    wifi_known_register_commands();
    register_network_commands();
    register_nvs();
    /* `nvs_inventory`, `nvs_export`, `nvs_import`. The kit's exclusions are all this board needs;
     * a new NVS key that is a credential, a per-board identity or a counter is excluded here,
     * with nvs_backup_exclude(), in the same change. No stdin_refusal: stdin is always the
     * console's (UART0; the host link has a UART of its own). */
    register_nvs_backup(&(const nvs_backup_config_t){
        .dir = BACKUP_DIR,
        .fetch = backup_fetch,
        .import_note = import_note,
    });
    register_i2ctools();
    const cmd_fs_config_t fs_config = {
        .base_path = MOUNT_PATH,
        .info = storage_info,
        .info_ctx = (void *)STORAGE_LABEL,
        .uart_num = -1,
    };
    ESP_ERROR_CHECK(register_fs(&fs_config));
    ESP_ERROR_CHECK(register_ota(-1));
    ota_core_set_begin_hook(before_update, NULL);
    fs_ops_set_change_hook(before_file_change, NULL);
    board_register_commands();
    register_video_commands(MOUNT_PATH);
    register_leds_commands();
    holo_servos_register_commands();
    web_server_register_commands();
    scene_register_commands();
    settings_register_commands();
    /* The link's settings, which its routes and `link` read from now on. `link mode can` starts the
     * Cyphal node on the link's pins A and B; named before `link`'s help */
    hostlink_init();
    hostlink_set_can_user(cyphal_start, "the Cyphal node", "cyphal");
    hostlink_set_on_pins(link_pins_held);
    hostlink_register_commands();
    register_can_commands(&(const can_cmd_config_t){ .pins = can_pins, .on_pins = can_pins_held });
    /* The Cyphal node: its settings now, its start from the host link's `can` mode, which hands it
     * pins A and B (and names it in the `gpio` command's table above). Its NVS namespace,
     * "cyphal", holds no credential or counter, but it does hold an identity on the bus: a fixed
     * node-ID and the node's name. Both stay in a backup, as the hostname and the link's address
     * do, so that a replacement board takes the old one's place (CLAUDE.md, "Configuration
     * backup"); a second board restored from it needs its own node-ID (manual/use/backup.md). So
     * do the holo's port numbers (its own setpoint subject, for a second holo). */
    err = cyphal_init(&(const cyphal_app_t){
        .ports = s_holo_ports,
        .port_count = sizeof(s_holo_ports) / sizeof(s_holo_ports[0]),
        .restart = ota_core_restart_after,
        .factory_reset = cyphal_factory_reset,
        .identify = cyphal_identify,
        .handlers = HOLO_CYPHAL_HANDLERS,
        .handler_count = HOLO_CYPHAL_HANDLER_COUNT,
        .poll = holo_cyphal_poll,
    });
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cyphal: %s", esp_err_to_name(err));
    }
    cyphal_register_commands("the host link isn't running in CAN mode: `link mode can`, then restart");

    /* Radio up in station mode, and the last network joined rejoined. */
    err = wifi_known_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi: %s", esp_err_to_name(err));
    }

    /* No network to join, or none joined soon: the access point for a while. And the button. */
    err = wifi_fallback_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi fallback: %s", esp_err_to_name(err));
    }

    /* The web app and its API, on every interface: `web` says where. The host link's routes first. */
    hostlink_register_routes();
    err = webui_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "web: %s", esp_err_to_name(err));
    }

    /* What the board starts with: the saved backlight and LED brightness, and the boot scene */
    err = scenes_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scenes: %s", esp_err_to_name(err));
    }
    settings_apply_boot();

    /* A controller on a wire, when the link settings say so: `link` says how */
    err = hostlink_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "host link: %s", esp_err_to_name(err));
    }

    printf("\n"
           "holo-player-fw on the Waveshare ESP32-S3-Touch-LCD-1.28.\n"
           "Type 'help' to get the list of commands.\n"
           "Use UP/DOWN arrows to navigate through command history.\n"
           "Press TAB when typing command name to auto-complete.\n");

    if (linenoiseIsDumbMode()) {
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using Windows Terminal or Putty instead.\n");
    }

    /* Up, with a console and the web server: an image an update installed has proved itself,
     * and stays. */
    ota_confirm_running();

    /* Main loop */
    while(true) {
        /* Get a line using linenoise.
         * The line is returned when ENTER is pressed.
         */
        char* line = linenoise(prompt);

#if CONFIG_CONSOLE_IGNORE_EMPTY_LINES
        if (line == NULL) { /* Ignore empty lines */
            continue;
        }
#else
        if (line == NULL) { /* Break on EOF or error */
            break;
        }
#endif // CONFIG_CONSOLE_IGNORE_EMPTY_LINES

        /* Add the command to the history if not empty*/
        if (strlen(line) > 0) {
#if CONFIG_CONSOLE_STORE_HISTORY
            /* Into the history, and saved to the filesystem unless a clip is playing */
            console_history_add(line);
            save_history_if_idle();
#else
            linenoiseHistoryAdd(line);
#endif // CONFIG_CONSOLE_STORE_HISTORY
        }

        /* Try to run the command */
        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unrecognized command\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            // command was empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }
#if CONFIG_CONSOLE_STORE_HISTORY
        /* A save held back by a clip the command has just stopped */
        save_history_if_idle();
#endif
        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);
    }

    ESP_LOGE(TAG, "Error or end-of-input, terminating console");
    esp_console_deinit();
}
