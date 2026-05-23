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
 *   I2S to external DAC (user-wired):
 *   BCLK, LRCK, DOUT come from the Kconfig values.
 *
 * No on-board speaker, so CONFIG_BOARD_HAS_SPEAKER is not "select"ed
 * by this board. Users with an external I2S DAC can enable it in
 * menuconfig under Smart Keyboard -> Audio / Narrator.
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
    .i2c_port    = CONFIG_SK_GAMEPAD_I2C_PORT,
    .i2c_sda     = CONFIG_SK_GAMEPAD_I2C_SDA_GPIO,
    .i2c_scl     = CONFIG_SK_GAMEPAD_I2C_SCL_GPIO,
    .i2c_freq_hz = CONFIG_SK_GAMEPAD_I2C_FREQ_HZ,
    .i2s = {
#if CONFIG_BOARD_HAS_SPEAKER
        .bclk = CONFIG_AUDIO_I2S_BCLK_GPIO,
        .lrck = CONFIG_AUDIO_I2S_LRCK_GPIO,
        .dout = CONFIG_AUDIO_I2S_DOUT_GPIO,
        .port = CONFIG_AUDIO_I2S_PORT,
#else
        .bclk = -1, .lrck = -1, .dout = -1, .port = 0,
#endif
    },
    .battery_adc_channel = -1,
};

#endif /* CONFIG_SK_BOARD_WAVESHARE_ESP32S3_TOUCH_LCD_349 */
