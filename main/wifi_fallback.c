/*
 * Wi-Fi fallback: see wifi_fallback.h.
 *
 * At start, and on a press of the button with the board off the network: a join (at start,
 * wifi_known_start()'s rejoin), then a check after HOLO_WIFI_JOIN_WAIT_S. Still not joined,
 * the access point goes on for HOLO_WIFI_FALLBACK_AP_MIN minutes and turns itself off after
 * (wifi_ap_start_for()); the station keeps retrying meanwhile. A press while the access point
 * is on for a while starts its minutes over. An access point turned on to stay (`wifi ap on`,
 * the API) is left alone.
 */
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "wifi_ap.h"
#include "wifi_known.h"
#include "wifi_fallback.h"

static const char *TAG = "wifi_fallback";

#define AP_S        (CONFIG_HOLO_WIFI_FALLBACK_AP_MIN * 60)
#define WAIT_US     ((int64_t)CONFIG_HOLO_WIFI_JOIN_WAIT_S * 1000000)
#define POLL_MS     20
#define PRESS_POLLS 3       /* held this many polls in a row: a press, not a bounce */

static int64_t s_check_at;  /* when to see whether the join worked, by esp_timer_get_time(); 0: none due */

static void ap_on(void)
{
    const esp_err_t err = wifi_ap_start_for(AP_S);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "access point: %s", esp_err_to_name(err));
    }
}

static void check(void)
{
    if (!wifi_known_is_connected()) {
        ESP_LOGI(TAG, "no network after %d s: the access point for %d min", CONFIG_HOLO_WIFI_JOIN_WAIT_S,
                 CONFIG_HOLO_WIFI_FALLBACK_AP_MIN);
        ap_on();
    }
}

static bool find_last(const char *ssid, bool has_passphrase, bool last, void *ctx)
{
    (void)ssid;
    (void)has_passphrase;
    *(bool *)ctx |= last;
    return !last;
}

/* A network to rejoin: the one joined last. */
static bool have_network(void)
{
    bool last = false;
    wifi_known_list(find_last, &last);
    return last;
}

#if CONFIG_HOLO_WIFI_BUTTON_GPIO >= 0
static void on_press(void)
{
    if (wifi_known_is_connected()) {
        ESP_LOGI(TAG, "button: already on a network");
        return;
    }
    wifi_ap_info_t ap;
    wifi_ap_get_info(&ap);
    if (ap.on && ap.off_in_s != 0) {
        ESP_LOGI(TAG, "button: the access point for another %d min", CONFIG_HOLO_WIFI_FALLBACK_AP_MIN);
        ap_on();
    }
    if (!have_network()) {
        if (!ap.on) {
            ESP_LOGI(TAG, "button: no saved network; the access point for %d min", CONFIG_HOLO_WIFI_FALLBACK_AP_MIN);
            ap_on();
        }
        return;
    }
    ESP_LOGI(TAG, "button: rejoining");
    wifi_known_set_enabled(true);
    if (!ap.on) {
        s_check_at = esp_timer_get_time() + WAIT_US;
    }
}
#endif

/* The button, polled, and the check when it is due: the access point comes up from here, not
 * from a timer callback, whose task has too little stack for it. */
static void fallback_task(void *arg)
{
    (void)arg;
#if CONFIG_HOLO_WIFI_BUTTON_GPIO >= 0
    int held = 0;
#endif
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        if (s_check_at != 0 && esp_timer_get_time() >= s_check_at) {
            s_check_at = 0;
            check();
        }
#if CONFIG_HOLO_WIFI_BUTTON_GPIO >= 0
        if (gpio_get_level(CONFIG_HOLO_WIFI_BUTTON_GPIO) != 0) {
            held = 0;
        } else if (++held == PRESS_POLLS) {
            on_press();     /* once per press, however long it is held */
        }
#endif
    }
}

esp_err_t wifi_fallback_start(void)
{
#if CONFIG_HOLO_WIFI_BUTTON_GPIO >= 0
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_HOLO_WIFI_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
#endif
#if CONFIG_HOLO_WIFI_FALLBACK_AP
    if (!have_network()) {
        ESP_LOGI(TAG, "no saved network: the access point for %d min", CONFIG_HOLO_WIFI_FALLBACK_AP_MIN);
        ap_on();
    } else {
        s_check_at = esp_timer_get_time() + WAIT_US;
    }
#endif
    return xTaskCreate(fallback_task, "wifi_fallback", 3072, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
