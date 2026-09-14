#include "board.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "board";

static i2c_master_bus_handle_t s_i2c_bus;

esp_err_t board_init(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }
    /* The board has no I2C pull-ups, so the internal ones (~45k) are all there is. Enough at
     * 400 kHz for two devices a few centimetres apart; an external device on longer wires
     * would want real ones. */
    const i2c_master_bus_config_t cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_i2c_bus), TAG, "i2c bus");
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_i2c_bus;
}
