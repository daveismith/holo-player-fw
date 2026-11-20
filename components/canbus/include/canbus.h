/*
 * canbus -- the ESP32-S3's one TWAI (CAN) controller, classic frames only: started on two pins
 * at a bitrate, frames in through a queue the ISR fills, frames out through a pool the ISR
 * empties, and the controller's error state. One owner at a time: the `can` bench command
 * (cmd_can.c) or, later, the Cyphal node. The pins are the caller's to choose and to guard.
 *
 * Every frame the bus carries is accepted (the controller's one filter is left open), so a
 * protocol on top filters in software.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANBUS_DEFAULT_BITRATE 1000000
#define CANBUS_STD_ID_MAX 0x7FFu        /* an 11-bit ID */
#define CANBUS_EXT_ID_MAX 0x1FFFFFFFu   /* a 29-bit ID */

typedef enum {
    CANBUS_NORMAL,      /* sends, receives, acknowledges */
    CANBUS_LISTEN,      /* receives only: never drives the bus, not even an acknowledgement */
    CANBUS_SELFTEST,    /* receives its own frames back through the transceiver, needs no ACK */
} canbus_mode_t;

typedef struct {
    int tx_gpio;
    int rx_gpio;
    uint32_t bitrate;   /* bits/s; 0 for CANBUS_DEFAULT_BITRATE. Sample point 87.5 % asked for */
    canbus_mode_t mode;
} canbus_config_t;

/* In this order so that it packs into 24 bytes, the receive queue's item (checked in canbus.c) */
typedef struct {
    uint64_t timestamp_us;  /* esp_timer_get_time() in the ISR that took it off the controller */
    uint32_t id;            /* 11 bits, or 29 when `ext` */
    uint8_t len;            /* 0..8; a remote frame's requested length */
    bool ext;
    bool rtr;
    uint8_t data[8];
} canbus_frame_t;

typedef enum {
    CANBUS_STOPPED,
    CANBUS_ERROR_ACTIVE,
    CANBUS_ERROR_WARNING,   /* an error counter at 96 or more */
    CANBUS_ERROR_PASSIVE,   /* an error counter at 128 or more */
    CANBUS_BUS_OFF,         /* off the bus until canbus_recover() */
} canbus_state_t;

typedef struct {
    canbus_state_t state;
    canbus_config_t config;     /* as started */
    uint16_t tx_errors;         /* the controller's TEC */
    uint16_t rx_errors;         /* REC */
    /* Since canbus_start(): */
    uint32_t tx_frames;         /* acknowledged (or, in self-test, sent) */
    uint32_t tx_failed;         /* not sent: given up on, aborted by bus-off, or dropped by a stop */
    uint32_t rx_frames;         /* put on the receive queue */
    uint32_t rx_dropped;        /* the receive queue was full */
    uint32_t rx_queue_peak;     /* the most frames the receive queue has held (of CONFIG_CANBUS_RX_QUEUE_LEN) */
    uint32_t bus_errors;        /* bit, form, stuff and ACK errors seen; lost arbitration is not one */
    uint32_t bus_off;           /* times the controller went bus-off */
    uint32_t tx_pending;        /* frames given to canbus_send() and not yet done */
} canbus_status_t;

/* The lock the calls below share. Once, from app_main, before another task can call them; again
 * is harmless. canbus_start() calls it too, for a caller with nothing to register. */
esp_err_t canbus_init(void);

/* Take the controller and the two pins, and start. The pins are reset first, whatever was
 * driving them as GPIOs: the caller has checked that nothing else owns them. ESP_ERR_INVALID_STATE
 * if already started, ESP_ERR_NOT_FOUND if another driver holds the controller,
 * ESP_ERR_INVALID_ARG for pins or a bitrate the controller can't do. Frames queued from an
 * earlier run are discarded. */
esp_err_t canbus_start(const canbus_config_t *config);

/* Stop, and leave the pins floating inputs. Frames not yet sent are dropped (counted in
 * tx_failed). Safe when stopped. If the driver will not let the node go, it is left running and
 * the error returned. */
esp_err_t canbus_stop(void);

/* Queue one frame for sending, waiting up to `wait` for room (CONFIG_CANBUS_TX_QUEUE_LEN frames
 * are in flight at most). ESP_ERR_TIMEOUT when there was none, ESP_ERR_NOT_SUPPORTED in
 * listen-only, ESP_ERR_INVALID_STATE when stopped or bus-off. Frames go out in the order given. */
esp_err_t canbus_send(const canbus_frame_t *frame, TickType_t wait);

/* The next received frame, waiting up to `wait`. False on timeout. One consumer at a time, and a
 * reader starts after its own successful canbus_start() (as cmd_can.c does): the queue
 * (CONFIG_CANBUS_RX_QUEUE_LEN frames) is made at the first start, and before it this returns
 * false at once. */
bool canbus_receive(canbus_frame_t *frame, TickType_t wait);

/* Start the controller's way back from bus-off: it rejoins after 128 x 11 recessive bits.
 * ESP_ERR_INVALID_STATE if it isn't bus-off. The frame bus-off aborted is lost (counted in
 * tx_failed); the frames waiting behind it go out once the controller is back. */
esp_err_t canbus_recover(void);

/* State, error counters and frame counts. Callable from any task, started or not. */
void canbus_status(canbus_status_t *out);

bool canbus_running(void);

const char *canbus_state_name(canbus_state_t state);

#ifdef __cplusplus
}
#endif
