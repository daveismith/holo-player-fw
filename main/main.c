/*
 * holo-player-fw: bring-up for the Waveshare ESP32-S3-Touch-LCD-1.28, with a UART console
 * carrying the esp-console-kit command set.
 *
 * Started from the ESP-IDF console_advanced example (Unlicense OR CC0-1.0).
 */

#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "esp_event.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "linenoise/linenoise.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "soc/soc_caps.h"
#include "board.h"
#include "cmd_fs.h"
#include "cmd_i2ctools.h"
#include "cmd_network.h"
#include "cmd_nvs.h"
#include "cmd_system.h"
#include "cmd_wifi.h"
#include "console_settings.h"
#include "video_player.h"
#include "wifi_known.h"

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

#if CONFIG_CONSOLE_STORE_HISTORY
#define HISTORY_PATH MOUNT_PATH "/history.txt"
#else
#define HISTORY_PATH NULL
#endif

/* Pins the `gpio` command refuses to drive: everything this board has wired to something,
 * the strapping pins, and the flash/PSRAM bus. Free pins are on the P2 header: 15, 16, 17,
 * 18, 21 and 33. */
static const gpio_reserved_t s_gpio_reserved[] = {
    { 0, "BOOT strap" },
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
    { 26, "flash/PSRAM SPI" }, { 27, "flash/PSRAM SPI" }, { 28, "flash/PSRAM SPI" },
    { 29, "flash/PSRAM SPI" }, { 30, "flash/PSRAM SPI" }, { 31, "flash/PSRAM SPI" },
    { 32, "flash/PSRAM SPI" },
    { 43, "UART0 TX (the console)" },
    { 44, "UART0 RX (the console)" },
    { 45, "VDD_SPI strap" },
    { 46, "boot mode strap" },
};

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

    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    const char *prompt = setup_prompt(PROMPT_STR ">");

    /* Register commands */
    esp_console_register_help_command();
    register_system_common();
    register_gpio(s_gpio_reserved, sizeof(s_gpio_reserved) / sizeof(s_gpio_reserved[0]));
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
    register_i2ctools();
    const cmd_fs_config_t fs_config = {
        .base_path = MOUNT_PATH,
        .info = storage_info,
        .info_ctx = (void *)STORAGE_LABEL,
        .uart_num = -1,
    };
    ESP_ERROR_CHECK(register_fs(&fs_config));
    board_register_commands();
    register_video_commands(MOUNT_PATH);

    /* Radio up in station mode, and the last network joined rejoined. */
    esp_err_t err = wifi_known_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi: %s", esp_err_to_name(err));
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
            linenoiseHistoryAdd(line);
#if CONFIG_CONSOLE_STORE_HISTORY
            /* Save command history to filesystem */
            linenoiseHistorySave(HISTORY_PATH);
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
        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);
    }

    ESP_LOGE(TAG, "Error or end-of-input, terminating console");
    esp_console_deinit();
}
