/*
 * Placeholder board file for a custom ESP32-S3 wiring.
 * Edit pin numbers to match your board, then select
 * "Generic ESP32-S3" in menuconfig under Smart Keyboard.
 */

#include "board.h"

#if CONFIG_SK_BOARD_GENERIC_ESP32S3

const board_t g_board = {
    .name = "Generic ESP32-S3 (edit me)",
    .display_type = BOARD_DISPLAY_NONE,
    .display = {
        .cs = -1, .sck = -1, .d0 = -1, .d1 = -1, .d2 = -1, .d3 = -1,
        .rst = -1, .te = -1, .bl = -1, .bl_active_low = false,
        .width = 320, .height = 240, .swap_xy = false,
    },
    .i2c_port    = CONFIG_SK_GAMEPAD_I2C_PORT,
    .i2c_sda     = CONFIG_SK_GAMEPAD_I2C_SDA_GPIO,
    .i2c_scl     = CONFIG_SK_GAMEPAD_I2C_SCL_GPIO,
    .i2c_freq_hz = CONFIG_SK_GAMEPAD_I2C_FREQ_HZ,
    .i2s = { .bclk = -1, .lrck = -1, .dout = -1, .port = 0 },
    .battery_adc_channel = -1,
};

#endif
