/*
 * The holoprojector's two servos, on the S3's own pins (MCPWM), and the holo engine over them.
 * See holo_servos.h.
 */
#include "holo_servos.h"

#include "cmd_servo.h"
#include "esp_log.h"
#include "holo.h"
#include "sdkconfig.h"
#include "servo.h"

static const char *TAG = "holo";

/* Which servo is which axis is a menuconfig choice: nothing on the board says. */
#if CONFIG_HOLO_PAN_SERVO1
#define SERVO1_IDENT "pan"
#define SERVO2_IDENT "tilt"
#else
#define SERVO1_IDENT "tilt"
#define SERVO2_IDENT "pan"
#endif

static const holo_desc_t s_holos[] = {
    { .name = "holo", .h = "pan", .v = "tilt", .led = NULL },
};

/* Both servos. Run again by `servo_register`, which is harmless: a pin already attached
 * is left alone. */
static esp_err_t attach(void)
{
    const esp_err_t err1 = servo_attach_gpio(CONFIG_HOLO_SERVO1_GPIO, SERVO1_IDENT,
                                             CONFIG_HOLO_SERVO_ABS_MIN_US, CONFIG_HOLO_SERVO_ABS_MAX_US,
                                             CONFIG_HOLO_SERVO1_MIN_US, CONFIG_HOLO_SERVO1_MAX_US);
    const esp_err_t err2 = servo_attach_gpio(CONFIG_HOLO_SERVO2_GPIO, SERVO2_IDENT,
                                             CONFIG_HOLO_SERVO_ABS_MIN_US, CONFIG_HOLO_SERVO_ABS_MAX_US,
                                             CONFIG_HOLO_SERVO2_MIN_US, CONFIG_HOLO_SERVO2_MAX_US);
    return err1 != ESP_OK ? err1 : err2;
}

esp_err_t holo_servos_start(void)
{
    const esp_err_t err = attach();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "servos: %s", esp_err_to_name(err));
    }
    /* Each servo's default travel (menuconfig) is its real travel, not a guess, so motion
     * does not wait for `holo endpoints` to save one. */
    const holo_config_t cfg = { .allow_uncalibrated = true };
    return holo_start(s_holos, sizeof(s_holos) / sizeof(s_holos[0]), &cfg);
}

void holo_servos_register_commands(void)
{
    register_servo(attach);
    holo_register_command();
}
