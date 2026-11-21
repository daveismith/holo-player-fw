/*
 * The holo's own ports on the Cyphal node (components/cyphal; manual/reference/cyphal.md): what the node
 * does with r2.holo.Motion, Light, Scene, Screen, Ring, ListScenes, MediaInfo, the setpoint subscription, Status,
 * Event and Axes, as handlers for cyphal_app_t, with uavcan.file.List and GetInfo on their fixed IDs, the holo's
 * registers, and its poll. The pure parts are holo_cyphal_util.h.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cyphal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* cyphal_app_t.handlers and handler_count: the holo's ports built so far */
extern const cyphal_handler_t HOLO_CYPHAL_HANDLERS[];
extern const size_t HOLO_CYPHAL_HANDLER_COUNT;

/* cyphal_app_t.fixed_services and fixed_service_count: uavcan.file.List and GetInfo (holo_cyphal_lists.c) */
extern const cyphal_handler_t HOLO_CYPHAL_FIXED[];
extern const size_t HOLO_CYPHAL_FIXED_COUNT;

/* cyphal_app_t.registers and register_count: the holo's calibration and drive policy, and touch reporting
 * (holo_cyphal_registers.c) */
extern const cyphal_register_t HOLO_CYPHAL_REGISTERS[];
extern const size_t HOLO_CYPHAL_REGISTER_COUNT;

/* cyphal_app_t.poll: the setpoints carried out, Status and Axes published at their rates, the board's
 * happenings published as Event, the heartbeat's health kept */
uint32_t holo_cyphal_poll(bool online);

#ifdef __cplusplus
}
#endif
