/*
 * Waveshare ESP32-S3-Touch-LCD-3.49 board definition.
 *
 * Panel:   AXS15231B QSPI, 640 (W) x 172 (H) landscape.
 *
 * Pins (Waveshare schematic / sample code):
 *   LCD QSPI:  CS=9, SCK=12, D0=11, D1=13, D2=14, D3=10
 *   LCD RST :  17
 *   LCD TE  :  18 (tearing-effect input)
 *   LCD BL  :  8  (active LOW, driven by LEDC PWM)
 *
 *   I2C (touch + gamepad header): defaults to the Kconfig values
 *   (SDA=11, SCL=10 -- adjust in menuconfig if you wire the
 *   gamepad to a different header).
 *
 *   On-board audio (ES8311 codec + amplifier + speaker):
 *       MCLK = 7, BCLK = 15, LRCK (WS) = 46, DOUT = 45
 *   The pins are wired on the PCB and cannot be changed, so they
 *   are hard-coded here rather than read from Kconfig. The
 *   AUDIO_I2S_* Kconfig entries are ignored for this board.
 *
 *   NOTE: The ES8311 is an I2C-controlled codec. The current
 *   audio component drives the I2S TX path only; making the
 *   speaker actually emit sound also requires bringing up the
 *   ES8311 over I2C (codec register init). That codec init is
 *   intentionally out of scope of this board file -- this entry
 *   just exposes the correct pins and selects BOARD_HAS_SPEAKER
 *   so the audio/narrator components are compiled in.
 *
 * References:
 *   https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.49
 *   https://github.com/clackups/draftling -- same panel, same MCU.
 */

#include "board.h"

#if CONFIG_SK_BOARD_WAVESHARE_ESP32S3_TOUCH_LCD_349

const board_t g_board = {
    .name = "Waveshare ESP32-S3-Touch-LCD-3.49",
    .display_type = BOARD_DISPLAY_AXS15231B,
    .display = {
        .cs = 9,
        .sck = 12,
        .d0 = 11,
        .d1 = 13,
        .d2 = 14,
        .d3 = 10,
        .rst = 17,
        .te  = 18,
        .bl  = 8,
        .bl_active_low = true,
        .width  = 640,
        .height = 172,
        .swap_xy = false,
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
    .i2s = {
        /* On-board ES8311 codec wiring -- fixed by the PCB. */
        .mclk = 7,
        .bclk = 15,
        .lrck = 46,
        .dout = 45,
        .port = 0,
    },
    .battery_adc_channel = -1,
};

#endif /* CONFIG_SK_BOARD_WAVESHARE_ESP32S3_TOUCH_LCD_349 */
