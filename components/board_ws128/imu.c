/*
 * QMI8658A IMU on the shared I2C bus, through waveshare/qmi8658.
 */
#include "board.h"
#include "esp_check.h"
#include "esp_log.h"
#include "qmi8658.h"

static const char *TAG = "imu";

static qmi8658_dev_t s_dev;
static bool s_present;
static uint8_t s_addr;

esp_err_t board_imu_init(void)
{
    if (s_present) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = board_i2c_bus();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_STATE, TAG, "board_init() first");

    /* Probed before qmi8658_init(), which adds a device handle it does not remove when the
     * address turns out to be wrong. */
    const uint8_t candidates[] = { BOARD_IMU_ADDR, BOARD_IMU_ADDR_ALT };
    uint8_t addr = 0;
    for (size_t i = 0; i < sizeof(candidates); i++) {
        if (i2c_master_probe(bus, candidates[i], 50) == ESP_OK) {
            addr = candidates[i];
            break;
        }
    }
    ESP_RETURN_ON_FALSE(addr != 0, ESP_ERR_NOT_FOUND, TAG, "no QMI8658 at 0x%02x or 0x%02x",
                        BOARD_IMU_ADDR, BOARD_IMU_ADDR_ALT);

    ESP_RETURN_ON_ERROR(qmi8658_init(&s_dev, bus, addr), TAG, "init");
    /* The driver starts at 8 g / 1 kHz; a board being tilted by hand wants resolution and
     * a rate the console can keep up with. */
    ESP_RETURN_ON_ERROR(qmi8658_set_accel_range(&s_dev, QMI8658_ACCEL_RANGE_4G), TAG, "accel range");
    ESP_RETURN_ON_ERROR(qmi8658_set_accel_odr(&s_dev, QMI8658_ACCEL_ODR_125HZ), TAG, "accel odr");
    ESP_RETURN_ON_ERROR(qmi8658_set_gyro_range(&s_dev, QMI8658_GYRO_RANGE_512DPS), TAG, "gyro range");
    ESP_RETURN_ON_ERROR(qmi8658_set_gyro_odr(&s_dev, QMI8658_GYRO_ODR_125HZ), TAG, "gyro odr");
    qmi8658_set_accel_unit_mg(&s_dev, true);
    qmi8658_set_gyro_unit_dps(&s_dev, true);
    ESP_RETURN_ON_ERROR(qmi8658_enable_sensors(&s_dev, QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO),
                        TAG, "enable");
    s_addr = addr;
    s_present = true;
    ESP_LOGI(TAG, "QMI8658 at 0x%02x: +/-4 g, +/-512 dps, 125 Hz", addr);
    return ESP_OK;
}

bool board_imu_present(void)
{
    return s_present;
}

uint8_t board_imu_address(void)
{
    return s_addr;
}

esp_err_t board_imu_read(board_imu_sample_t *out)
{
    ESP_RETURN_ON_FALSE(s_present, ESP_ERR_INVALID_STATE, TAG, "no IMU");
    qmi8658_data_t d;
    ESP_RETURN_ON_ERROR(qmi8658_read_sensor_data(&s_dev, &d), TAG, "read");
    out->ax = d.accelX / 1000.0f;   /* mg */
    out->ay = d.accelY / 1000.0f;
    out->az = d.accelZ / 1000.0f;
    out->gx = d.gyroX;
    out->gy = d.gyroY;
    out->gz = d.gyroZ;
    float t = 0.0f;
    out->temp_c = qmi8658_read_temp(&s_dev, &t) == ESP_OK ? t : 0.0f;
    return ESP_OK;
}

esp_err_t board_imu_who_am_i(uint8_t *who, uint8_t *revision)
{
    ESP_RETURN_ON_FALSE(s_present, ESP_ERR_INVALID_STATE, TAG, "no IMU");
    ESP_RETURN_ON_ERROR(qmi8658_get_who_am_i(&s_dev, who), TAG, "who am i");
    return qmi8658_read_register(&s_dev, QMI8658_REVISION, revision, 1);
}
