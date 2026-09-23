/*
 * The two Arduino functions AnimatedGIF::playFrame() calls outside Linux and macOS.
 *
 * Only millis() is ever reached here: the player asks for frames with bSync false and does its
 * own timing, so delay() exists only to satisfy the compiler.
 */
#pragma once

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static inline long millis(void)
{
    return (long)(esp_timer_get_time() / 1000);
}

static inline void delay(long ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}
