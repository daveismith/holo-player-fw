/*
 * The holo on Cyphal: its registers, after the node's own (cyphal_app_t.registers; manual/reference/cyphal.md,
 * "Registers"):
 *
 *   r2.holo.horizontal.endpoints_us, r2.holo.vertical.endpoints_us   natural16[2]  [closed, open] us; [0, 0] clears
 *   r2.holo.horizontal.release, r2.holo.vertical.release             bit[3]        release at closed, open, between
 *   r2.holo.horizontal.settle_ms, r2.holo.vertical.settle_ms         natural16[1]  driven this long before a release
 *   r2.touch.reporting                                               bit[1]        touch events on (saved)
 *
 * The first six are the kit's own saved values, through its public servo API, the one store the console's `holo
 * <holo> endpoints`, the web app's calibration and PUT /api/v1/servos/policy use: servo_get_limits(),
 * servo_set_op_limits() and servo_clear_op_limits() (as the holo engine's endpoints hook makes them: closed above
 * open turns the servo round), servo_set_drive_policy() over servo_get_drive_policy(). The kit's rules come with
 * them: clearing the endpoints clears the drive policy too, and a policy once saved has a settle time of 100 ms at
 * least. Reading them can wait (the policy read takes the servos' mutex, held through an NVS write), so they are
 * CYPHAL_REG_READ_ON_WORKER: a read over Cyphal is the node's worker's, as a write is, never the node's task's.
 *
 * Touch reporting is one of the board's settings (settings_t.touch, components/scenes/settings.c: GET and PATCH
 * /api/v1/settings and `settings touch` show and change the same one), which the start applies. A write applies it
 * at once and saves it; `touch on` and `touch off` change what is in effect without saving it, and the register
 * reads what is in effect (board_touch_running(), a pointer's read, fit for the node's task).
 */
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "esp_log.h"
#include "holo.h"
#include "holo_cyphal.h"
#include "holo_cyphal_internal.h"
#include "holo_cyphal_util.h"
#include "scenes.h"
#include "sdkconfig.h"
#include "servo.h"

static const char *TAG = "holo_cyphal";

enum { AXIS_H, AXIS_V };

/* The axis's servo ident (the holo's description, as the engine has it): NULL without one */
static const char *ident_of(int axis)
{
    holo_desc_t d;
    if (!holo_describe(0, &d)) {
        return NULL;
    }
    return axis == AXIS_H ? d.h : d.v;
}

/* The register's axis (its ctx: NULL for the horizontal) */
static int axis_of(const cyphal_register_t *r)
{
    return r->ctx != NULL ? AXIS_V : AXIS_H;
}

/* The axis's endpoints and drive policy as the kit has them: false, with nothing read, without a servo. May wait (the
 * servos' mutex): on the node's worker or the console's task, never the node's own */
static bool read_axis(const cyphal_register_t *r, servo_limits_t *l, servo_drive_policy_t *p)
{
    const char *id = ident_of(axis_of(r));
    return id != NULL && servo_get_limits(id, l) && servo_get_drive_policy(id, p);
}

/* The register's axis's servo, for a write: NULL, with why, without one */
static const char *servo_for(const cyphal_register_t *r, char *why, size_t why_len)
{
    const char *id = ident_of(axis_of(r));
    if (id == NULL || !servo_exists(id)) {
        snprintf(why, why_len, "the axis has no servo (not fitted)");
        return NULL;
    }
    return id;
}

/* ---- endpoints */

/* An axis without a servo reads [0, 0], [false, false, false] and 0 */
static void read_endpoints(const cyphal_register_t *r, const cyphal_settings_t *s, uavcan_register_Value_1_0 *out)
{
    (void)s;
    servo_limits_t l;
    servo_drive_policy_t p;
    uint16_t e[2] = { 0, 0 };
    if (read_axis(r, &l, &p)) {
        holo_endpoints_of(l.op_min_us, l.op_max_us, l.invert, e);
    }
    cyphal_reg_set_natural16(out, e, 2);
}

static cyphal_reg_result_t write_endpoints(const cyphal_register_t *r, cyphal_settings_t *s,
                                           const uavcan_register_Value_1_0 *v, char *why, size_t why_len)
{
    (void)s;
    int64_t n[2];
    cyphal_reg_result_t res = cyphal_reg_take_numbers(r, v, n, why, why_len);
    const char *id = res == CYPHAL_REG_OK ? servo_for(r, why, why_len) : NULL;
    if (res != CYPHAL_REG_OK || id == NULL) {
        return res != CYPHAL_REG_OK ? res : CYPHAL_REG_BAD_VALUE;
    }
    servo_limits_t l;
    holo_endpoints_t e;
    if (!servo_get_limits(id, &l) || !holo_endpoints_write(n, l.abs_min_us, l.abs_max_us, &e, why, why_len)) {
        return CYPHAL_REG_BAD_VALUE;
    }
    const bool ok = e.clear ? servo_clear_op_limits(id) : servo_set_op_limits(id, e.min_us, e.max_us, e.invert);
    if (!ok) {
        snprintf(why, why_len, "%s: not saved", id);
        return CYPHAL_REG_BAD_VALUE;
    }
    return CYPHAL_REG_OK;
}

/* ---- the drive policy */

static void read_release(const cyphal_register_t *r, const cyphal_settings_t *s, uavcan_register_Value_1_0 *out)
{
    (void)s;
    servo_limits_t l;
    servo_drive_policy_t p;
    bool release[3] = { false, false, false };
    if (read_axis(r, &l, &p)) {
        release[0] = p.drive_closed == SERVO_DRIVE_RELEASE;
        release[1] = p.drive_open == SERVO_DRIVE_RELEASE;
        release[2] = p.drive_mid == SERVO_DRIVE_RELEASE;
    }
    cyphal_reg_set_bits(out, release, 3);
}

static void read_settle(const cyphal_register_t *r, const cyphal_settings_t *s, uavcan_register_Value_1_0 *out)
{
    (void)s;
    servo_limits_t l;
    servo_drive_policy_t p;
    const uint16_t ms = read_axis(r, &l, &p) ? p.settle_ms : 0;
    cyphal_reg_set_natural16(out, &ms, 1);
}

/* The axis's policy as it is, with the release bits (NULL: as they are) and the settle time (-1: as it is), saved */
static cyphal_reg_result_t put_policy(const cyphal_register_t *r, const bool *release, int settle_ms, char *why,
                                      size_t why_len)
{
    const char *id = servo_for(r, why, why_len);
    servo_drive_policy_t p;
    if (id == NULL || !servo_get_drive_policy(id, &p)) {
        return CYPHAL_REG_BAD_VALUE;
    }
    if (release != NULL) {
        p.drive_closed = release[0] ? SERVO_DRIVE_RELEASE : SERVO_DRIVE_HOLD;
        p.drive_open = release[1] ? SERVO_DRIVE_RELEASE : SERVO_DRIVE_HOLD;
        p.drive_mid = release[2] ? SERVO_DRIVE_RELEASE : SERVO_DRIVE_HOLD;
    }
    if (settle_ms >= 0) {
        p.settle_ms = (uint16_t)settle_ms;
    }
    const bool ok = servo_set_drive_policy(id, &p);
    if (!ok) {
        snprintf(why, why_len, "%s: the policy was not saved", id);
        return CYPHAL_REG_BAD_VALUE;
    }
    return CYPHAL_REG_OK;
}

static cyphal_reg_result_t write_release(const cyphal_register_t *r, cyphal_settings_t *s,
                                         const uavcan_register_Value_1_0 *v, char *why, size_t why_len)
{
    (void)s;
    bool bits[3];
    const cyphal_reg_result_t res = cyphal_reg_take_bits(r, v, bits, why, why_len);
    return res != CYPHAL_REG_OK ? res : put_policy(r, bits, -1, why, why_len);
}

static cyphal_reg_result_t write_settle(const cyphal_register_t *r, cyphal_settings_t *s,
                                        const uavcan_register_Value_1_0 *v, char *why, size_t why_len)
{
    (void)s;
    int64_t n;
    uint16_t ms;
    const cyphal_reg_result_t res = cyphal_reg_take_numbers(r, v, &n, why, why_len);
    if (res != CYPHAL_REG_OK) {
        return res;
    }
    return holo_settle_write(n, &ms, why, why_len) ? put_policy(r, NULL, ms, why, why_len) : CYPHAL_REG_BAD_VALUE;
}

/* ---- touch reporting */

#if CONFIG_BOARD_TOUCH_ENABLE
static void read_touch(const cyphal_register_t *r, const cyphal_settings_t *s, uavcan_register_Value_1_0 *out)
{
    (void)r;
    (void)s;
    const bool on = board_touch_running();
    cyphal_reg_set_bits(out, &on, 1);
}

/* Applied, then saved in the board's settings; if the save fails, put back as it was and refused */
static cyphal_reg_result_t write_touch(const cyphal_register_t *r, cyphal_settings_t *s,
                                       const uavcan_register_Value_1_0 *v, char *why, size_t why_len)
{
    (void)s;
    bool on;
    const cyphal_reg_result_t res = cyphal_reg_take_bits(r, v, &on, why, why_len);
    if (res != CYPHAL_REG_OK) {
        return res;
    }
    const bool was = board_touch_running();
    if (on) {
        const esp_err_t err = board_touch_start();
        if (err != ESP_OK) {
            snprintf(why, why_len, "touch didn't start: %s", esp_err_to_name(err));
            return CYPHAL_REG_BAD_VALUE;
        }
    } else {
        board_touch_stop();
    }
    settings_t st;
    settings_get(&st);
    st.touch = on;
    const esp_err_t err = settings_save(&st);
    if (err != ESP_OK) {
        if (was && !on) {
            board_touch_start();
        } else if (!was && on) {
            board_touch_stop();
        }
        snprintf(why, why_len, "not saved: %s", esp_err_to_name(err));
        return CYPHAL_REG_BAD_VALUE;
    }
    ESP_LOGI(TAG, "touch reporting %s, saved", on ? "on" : "off");
    return CYPHAL_REG_OK;
}
#endif

/* ---- the table */

#define RW     (CYPHAL_REG_MUTABLE | CYPHAL_REG_PERSISTENT)
#define SERVOS (RW | CYPHAL_REG_READ_ON_WORKER)     /* the kit's servo reads may wait */
#define AXIS(axis, ctx)                                                                                          \
    { "r2.holo." axis ".endpoints_us", CYPHAL_REG_NATURAL16, 2, 0, SERVOS,                                       \
      "two whole numbers, closed then open (0 0 to clear)", read_endpoints, write_endpoints, ctx },              \
    { "r2.holo." axis ".release", CYPHAL_REG_BIT, 3, 0, SERVOS, NULL, read_release, write_release, ctx },        \
    { "r2.holo." axis ".settle_ms", CYPHAL_REG_NATURAL16, 1, 0, SERVOS, NULL, read_settle, write_settle, ctx }

/* ctx: NULL for the horizontal axis, anything else for the vertical */
static const char VERTICAL = 'v';

const cyphal_register_t HOLO_CYPHAL_REGISTERS[] = {
    AXIS("horizontal", NULL),
    AXIS("vertical", &VERTICAL),
#if CONFIG_BOARD_TOUCH_ENABLE
    { "r2.touch.reporting", CYPHAL_REG_BIT, 1, 0, RW, NULL, read_touch, write_touch, NULL },
#endif
};
const size_t HOLO_CYPHAL_REGISTER_COUNT = sizeof(HOLO_CYPHAL_REGISTERS) / sizeof(HOLO_CYPHAL_REGISTERS[0]);
