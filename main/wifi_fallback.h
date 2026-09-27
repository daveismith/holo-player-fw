/*
 * The way back onto the board without a network: its access point, for a while, when it can't
 * join one at start, and a button that rejoins -- or, failing that, turns the access point on.
 * Under "Wi-Fi fallback" in menuconfig. See wifi_fallback.c.
 */
#pragma once

#include "esp_err.h"

/* After wifi_known_start(): watch the rejoin it started, and the button. */
esp_err_t wifi_fallback_start(void);
