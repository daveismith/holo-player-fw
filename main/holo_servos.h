/*
 * The holoprojector's two servos, on the S3's own pins, and the holo engine over them.
 * Pins and travel are under "Holoprojector servos" in menuconfig.
 */
#pragma once

#include "esp_err.h"

/* Register the servos (they stay limp until the first move) and start the engine. After NVS
 * is up: the servos load their saved calibration from it. */
esp_err_t holo_servos_start(void);

/* The servo_* commands and `holo`. */
void holo_servos_register_commands(void);
