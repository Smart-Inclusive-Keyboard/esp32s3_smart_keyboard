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
 *   External gamepad UART: the companion gamepad board streams
 *   its HID report TX-only into this board's RX pin (8-N-1,
 *   115200 baud by default). The default RX pin is GPIO 11,
 *   which is free on this board (the SK_GAMEPAD_UART_* Kconfig
 *   defaults can be used as-is). The interface is receive-only;
 *   no TX / RTS / CTS line is driven.
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
    /* External gamepad UART link. The companion gamepad board
     * streams its HID report TX-only into our RX pin (8-N-1).
     * GPIO 11 is free on this board (display owns {1,2,3,4,5,6,
     * 12}, audio / touch own {7,8,13,15,16,44}), so the
     * SK_GAMEPAD_UART_RX_GPIO Kconfig default of 11 is safe to
     * use as-is here. */
    .uart_port = CONFIG_SK_GAMEPAD_UART_PORT,
    .uart_rx   = CONFIG_SK_GAMEPAD_UART_RX_GPIO,
    .uart_baud = CONFIG_SK_GAMEPAD_UART_BAUD,
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
         * swap_xy = true (and mirror_y = false). On this board
         * the touch X axis runs the same direction as the panel's
         * native X axis once the 90 deg CW software rotation in
         * the display flush is taken into account -- mirroring
         * both axes flips the logical X (Tab is reported at the
         * far-right edge as PgDn). The remaining mirror_x in the
         * touch's native space is needed for the vertical axis
         * once swap_xy turns it into the logical Y. */
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
        .mirror_y = false,
        .swap_xy  = true,
    },
    .battery_adc_channel = -1,
};

/* Compile-time guards against re-introducing pin overlap between
 * the gamepad UART RX pin, the display QSPI bus, the audio I2S
 * bus and the on-board touch I2C bus. The pin sets owned by each
 * subsystem on this board are:
 *   display : { 1, 2, 3, 4, 5, 6, 12 }
 *   audio   : { 13, 15, 16, 44 } (I2S) + { 7, 8 } (I2C to codec)
 *   touch   : { 7, 8 } (shared with the codec I2C bus)
 * A gamepad pin landing in any of those sets would corrupt the
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

_Static_assert(SK_35B_GAMEPAD_PIN_OK(CONFIG_SK_GAMEPAD_UART_RX_GPIO),
    "Waveshare 3.5B: gamepad UART RX must not overlap display/audio pins");

#endif /* CONFIG_SK_BOARD_WAVESHARE_ESP32S3_TOUCH_LCD_35B */
