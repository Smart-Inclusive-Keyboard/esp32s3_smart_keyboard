/*
 * Waveshare ESP32-S3-Touch-LCD-3.5B board definition.
 *
 * Panel:   AXS15231B QSPI, 480 (W) x 320 (H) landscape
 *          (native portrait 320 x 480, software-rotated 90 deg
 *          CW in the display flush path like the 3.49 board).
 *
 * Pins (Waveshare official reference firmware, cross-checked
 * against the xiaozhi-library board config for the same board:
 * https://github.com/coloz/xiaozhi-library
 *   src/boards/waveshare/esp32-s3-touch-lcd-3.5b/config.h):
 *
 *   LCD QSPI:  CS=12, SCK=5, D0=1, D1=2, D2=3, D3=4
 *   LCD RST :  -1 (not on a GPIO; driven by the on-board TCA9554
 *                  I/O expander pin 1 -- left unmanaged here, the
 *                  panel's internal POR brings it up)
 *   LCD TE  :  -1 (not wired on this board)
 *   LCD BL  :  6  (active HIGH, LEDC PWM)
 *
 *   On-board ES8311 audio codec + amplifier + speaker:
 *       MCLK = 44, BCLK = 13, LRCK (WS) = 15, DOUT = 16
 *   The codec sits on the touch I2C bus (SDA = 8, SCL = 7,
 *   address ES8311_CODEC_DEFAULT_ADDR). As with the 3.49 board
 *   the ES8311 register-level init is intentionally out of
 *   scope of this board file: this entry just exposes the
 *   correct I2S pins and selects BOARD_HAS_SPEAKER so the
 *   audio / narrator components are compiled in.
 *
 *   External gamepad SPI: SCLK=9, MOSI=10, MISO=11 on SPI3.
 *   The pin trio is requested by the user; CS is left
 *   unassigned (-1) because nothing else on the board is wired
 *   to a free pin suitable for use as a chip-select, and the
 *   gamepad we target here is the only slave on the bus so
 *   permanently-asserted /CS is acceptable. SK_GAMEPAD_SPI_*_GPIO
 *   Kconfig defaults are NOT used -- every one of them collides
 *   with the display QSPI bus or audio I2S pins on this board.
 *
 *   I2C gamepad transport is intentionally not wired on this
 *   board (every default pin clashes with display / audio /
 *   touch). If you select SK_GAMEPAD_TRANSPORT_I2C with this
 *   board you must edit the .i2c_* fields below to point at a
 *   free pin pair.
 *
 * References:
 *   https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5B
 *   https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5B
 */

#include "board.h"

#if CONFIG_SK_BOARD_WAVESHARE_ESP32S3_TOUCH_LCD_35B

const board_t g_board = {
    .name = "Waveshare ESP32-S3-Touch-LCD-3.5B",
    .display_type = BOARD_DISPLAY_AXS15231B,
    .display = {
        .cs  = 12,
        .sck = 5,
        .d0  = 1,
        .d1  = 2,
        .d2  = 3,
        .d3  = 4,
        .rst = -1,    /* on the TCA9554 I/O expander, not a GPIO */
        .te  = -1,    /* TE not wired on this board */
        .bl  = 6,
        .bl_active_low = false,
        /* Logical (post-rotation) resolution. The AXS15231B panel is
         * natively portrait 320w x 480h; we expose it as 480x320
         * landscape to the rest of the firmware and let the display
         * backend perform a 90 deg CW software rotation in its
         * flush path (same approach as the 3.49 board). */
        .width  = 480,
        .height = 320,
        .swap_xy = true,
    },
#if CONFIG_SK_GAMEPAD_TRANSPORT_I2C
    /* I2C gamepad pins are NOT wired on this board's free-pin
     * set -- every Kconfig default collides with display / audio
     * / touch. Force the bus pins to -1 so a misconfiguration is
     * caught at runtime instead of silently driving the wrong
     * wires. */
    .i2c_port    = CONFIG_SK_GAMEPAD_I2C_PORT,
    .i2c_sda     = -1,
    .i2c_scl     = -1,
    .i2c_freq_hz = CONFIG_SK_GAMEPAD_I2C_FREQ_HZ,
#else
    .i2c_port = 0, .i2c_sda = -1, .i2c_scl = -1, .i2c_freq_hz = 0,
#endif
#if CONFIG_SK_GAMEPAD_TRANSPORT_SPI
    /* SPI3 host (display owns SPI2). Pin trio requested by the
     * board author: SCLK=9, MOSI=10, MISO=11. No dedicated CS
     * pin -- gamepad is the only slave on the bus. */
    .spi_host    = 3,
    .spi_sclk    = 9,
    .spi_mosi    = 10,
    .spi_miso    = 11,
    .spi_cs      = -1,
    .spi_freq_hz = CONFIG_SK_GAMEPAD_SPI_FREQ_HZ,
    .spi_mode    = CONFIG_SK_GAMEPAD_SPI_MODE,
#else
    .spi_host = -1, .spi_sclk = -1, .spi_mosi = -1, .spi_miso = -1,
    .spi_cs   = -1, .spi_freq_hz = 0, .spi_mode = 0,
#endif
    .i2s = {
        /* On-board ES8311 codec wiring -- fixed by the PCB. */
        .mclk = 44,
        .bclk = 13,
        .lrck = 15,
        .dout = 16,
        .port = 0,
    },
    .codec = {
        /* ES8311 lives at 7-bit address 0x18 on the I2C bus
         * shared with the capacitive touch controller (SDA = 8,
         * SCL = 7). The on-board class-D amplifier draws its
         * enable from the codec's GPIO -- there is no separate
         * MCU-controlled PA pin, so pa_pin stays at -1. */
        .i2c_port = 0,
        .sda      = 8,
        .scl      = 7,
        .addr     = 0x18,
        .freq_hz  = 100000,
        .pa_pin   = -1,
    },
    .touch = {
        /* The 3.5B carries the same AXS15231B-family capacitive
         * touch controller as the 3.49 board (magic-packet I2C
         * protocol, 7-bit address 0x3B). On THIS board the touch
         * I2C lines are shared with the ES8311 audio codec
         * (SDA = 8, SCL = 7) -- there is no separate touch bus.
         * INT and RST are not broken out (the controller is
         * polled, no IRQ).
         *
         * Native panel orientation is portrait 320 (W) x 480 (H),
         * which is the coordinate space the controller reports
         * in. The rest of the firmware sees the panel as a
         * landscape 480 x 320 framebuffer (90 deg CW software
         * rotation in the display flush path), so we map raw
         * touch coordinates to logical pixels with mirror_x =
         * mirror_y = swap_xy = true -- the same composition
         * Waveshare's reference driver applies via its
         * esp_lcd_touch_axs15231b flags { swap_xy=1, mirror_x=1,
         * mirror_y=1 } block (see coloz/xiaozhi-library
         * src/boards/waveshare/esp32-s3-touch-lcd-3.5b for the
         * upstream cross-reference). */
        .i2c_port = 0,
        .sda      = 8,
        .scl      = 7,
        .intr     = -1,
        .rst      = -1,
        .addr     = 0x3B,
        .freq_hz  = 400000,
        .native_w = 320,
        .native_h = 480,
        .mirror_x = true,
        .mirror_y = true,
        .swap_xy  = true,
    },
    .battery_adc_channel = -1,
};

/* Compile-time guards against re-introducing pin overlap between
 * the gamepad SPI bus, the display QSPI bus, the audio I2S bus
 * and the on-board touch I2C bus. The pin sets owned by each
 * subsystem on this board are:
 *   display : { 1, 2, 3, 4, 5, 6, 12 }
 *   audio   : { 13, 15, 16, 44 } (I2S) + { 7, 8 } (I2C to codec)
 *   touch   : { 7, 8 } (shared with the codec I2C bus)
 * Any gamepad pin landing in any of those sets would corrupt the
 * shared wire. */
#include <assert.h>
#define SK_35B_PIN_CONFLICTS_DISPLAY(p) ( \
    (p) == 1  || (p) == 2  || (p) == 3  || (p) == 4 || \
    (p) == 5  || (p) == 6  || (p) == 12)
#define SK_35B_PIN_CONFLICTS_AUDIO(p) ( \
    (p) == 7  || (p) == 8  || (p) == 13 || (p) == 15 || \
    (p) == 16 || (p) == 44)
#define SK_35B_GAMEPAD_PIN_OK(p) \
    (!SK_35B_PIN_CONFLICTS_DISPLAY(p) && \
     !SK_35B_PIN_CONFLICTS_AUDIO(p))

#if CONFIG_SK_GAMEPAD_TRANSPORT_SPI
_Static_assert(SK_35B_GAMEPAD_PIN_OK(9),
    "Waveshare 3.5B: gamepad SPI SCLK must not overlap display/audio pins");
_Static_assert(SK_35B_GAMEPAD_PIN_OK(10),
    "Waveshare 3.5B: gamepad SPI MOSI must not overlap display/audio pins");
_Static_assert(SK_35B_GAMEPAD_PIN_OK(11),
    "Waveshare 3.5B: gamepad SPI MISO must not overlap display/audio pins");
#endif

#endif /* CONFIG_SK_BOARD_WAVESHARE_ESP32S3_TOUCH_LCD_35B */
