/*
 * Placeholder board file for a custom classic-ESP32 wiring.
 * Edit pin numbers to match your board.
 */

#include "board.h"

#if CONFIG_SK_BOARD_GENERIC_ESP32

const board_t g_board = {
    .name = "Generic ESP32 (edit me)",
    .display_type = BOARD_DISPLAY_NONE,
    .display = {
        .cs = -1, .sck = -1, .d0 = -1, .d1 = -1, .d2 = -1, .d3 = -1,
        .rst = -1, .te = -1, .bl = -1, .bl_active_low = false,
        .width = 320, .height = 240, .swap_xy = false,
    },
#if CONFIG_SK_GAMEPAD_TRANSPORT_I2C
    .i2c_port    = CONFIG_SK_GAMEPAD_I2C_PORT,
    .i2c_sda     = CONFIG_SK_GAMEPAD_I2C_SDA_GPIO,
    .i2c_scl     = CONFIG_SK_GAMEPAD_I2C_SCL_GPIO,
    .i2c_freq_hz = CONFIG_SK_GAMEPAD_I2C_FREQ_HZ,
#else
    .i2c_port = 0, .i2c_sda = -1, .i2c_scl = -1, .i2c_freq_hz = 0,
#endif
#if CONFIG_SK_GAMEPAD_TRANSPORT_SPI
    .spi_host    = CONFIG_SK_GAMEPAD_SPI_HOST,
    .spi_sclk    = CONFIG_SK_GAMEPAD_SPI_SCLK_GPIO,
    .spi_mosi    = CONFIG_SK_GAMEPAD_SPI_MOSI_GPIO,
    .spi_miso    = CONFIG_SK_GAMEPAD_SPI_MISO_GPIO,
    .spi_cs      = CONFIG_SK_GAMEPAD_SPI_CS_GPIO,
    .spi_freq_hz = CONFIG_SK_GAMEPAD_SPI_FREQ_HZ,
    .spi_mode    = CONFIG_SK_GAMEPAD_SPI_MODE,
#else
    .spi_host = -1, .spi_sclk = -1, .spi_mosi = -1, .spi_miso = -1,
    .spi_cs   = -1, .spi_freq_hz = 0, .spi_mode = 0,
#endif
    .i2s = { .bclk = -1, .lrck = -1, .dout = -1, .mclk = -1, .port = 0 },
    .battery_adc_channel = -1,
};

#endif
