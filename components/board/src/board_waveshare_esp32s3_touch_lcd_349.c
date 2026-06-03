/*
 * Waveshare ESP32-S3-Touch-LCD-3.49 board definition.
 *
 * Panel:   AXS15231B QSPI, 640 (W) x 172 (H) landscape.
 *
 * Pins (Waveshare official reference firmware
 * Examples/Arduino/09_LVGL_V8_Test/user_config.h, verified working
 * by the clackups/draftling port on the same board):
 *   LCD QSPI:  CS=9, SCK=10, D0=11, D1=12, D2=13, D3=14
 *   LCD RST :  21
 *   LCD TE  :  -1 (not wired on this board)
 *   LCD BL  :  8  (active LOW, driven by LEDC PWM)
 *
 *   GPIO 17/18 are the on-board capacitive-touch I2C bus (SDA/SCL)
 *   on this board, NOT the LCD -- earlier revisions of this file
 *   placed LCD RST/TE there and the SPI driver streamed pixel data
 *   onto the wrong physical pins, leaving the panel in its POR
 *   state (random GRAM, preserved across MCU reset, re-randomised
 *   on power cycle).
 *
 *   External gamepad header: I2C SDA=1, SCL=2 / SPI SCLK=4,
 *   MOSI=5, MISO=39, CS=40. Hardcoded here (NOT taken from the
 *   global SK_GAMEPAD_* Kconfig defaults) because every one of
 *   those defaults overlaps a display QSPI line on this board
 *   (SK_GAMEPAD_I2C_SDA_GPIO defaults to 11 = LCD D0,
 *   SK_GAMEPAD_SPI_SCLK_GPIO=12 = LCD SCK, etc.). Driving any of
 *   those pins from a second master corrupts the LCD bus -- the
 *   reported symptom is a permanently-black panel because every
 *   QSPI memory-write burst from the display driver races the
 *   gamepad transactions.
 *
 *   The pins picked here come from the set of GPIOs the user has
 *   confirmed are physically broken out and unused on this board:
 *     { 0, 1, 2, 3, 4, 5, 19, 20, 33, 39, 40, 41, 43, 44 }
 *   We further avoid GPIO 0/3 (ESP32-S3 strapping), 19/20 (USB
 *   D-/D+, used by the USB-HID transport) and 43/44 (UART0
 *   TX/RX, used for the serial console). A
 *   BUILD_BUG_OR_ZERO-style static_assert block at the bottom of
 *   this file guards against re-introducing the overlap.
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
        .cs  = 9,
        .sck = 10,
        .d0  = 11,
        .d1  = 12,
        .d2  = 13,
        .d3  = 14,
        .rst = 21,
        .te  = -1,    /* TE not wired on this board */
        .bl  = 8,
        .bl_active_low = true,
        /* Logical (post-rotation) resolution. The AXS15231B panel is
         * natively portrait 172w x 640h; we expose it as 640x172
         * landscape to the rest of the firmware and let the display
         * backend perform a 90 deg CW software rotation in its
         * flush path. Hardware MADCTL MV (row/column swap) is
         * silently ignored on this panel revision, so swap_xy must
         * be true (software rotation). */
        .width  = 640,
        .height = 172,
        .swap_xy = true,
    },
#if CONFIG_SK_GAMEPAD_TRANSPORT_I2C
    /* Hardcoded SAFE pins -- ignore SK_GAMEPAD_I2C_*_GPIO Kconfig
     * defaults; see header comment for the conflict rationale. */
    .i2c_port    = CONFIG_SK_GAMEPAD_I2C_PORT,
    .i2c_sda     = 1,
    .i2c_scl     = 2,
    .i2c_freq_hz = CONFIG_SK_GAMEPAD_I2C_FREQ_HZ,
#else
    .i2c_port = 0, .i2c_sda = -1, .i2c_scl = -1, .i2c_freq_hz = 0,
#endif
#if CONFIG_SK_GAMEPAD_TRANSPORT_SPI
    /* Hardcoded SAFE pins on SPI3 (display owns SPI2). Every
     * SK_GAMEPAD_SPI_*_GPIO Kconfig default collides with a
     * display QSPI line on this board, so we ignore them and pin
     * the gamepad SPI to a private set of GPIOs. */
    .spi_host    = 3,
    .spi_sclk    = 4,
    .spi_mosi    = 5,
    .spi_miso    = 39,
    .spi_cs      = 40,
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
    .touch = {
        /* AXS5106-family capacitive touch controller wired to a
         * dedicated I2C bus on GPIO 17 (SDA) / GPIO 18 (SCL).
         * INT and RST are not broken out on this board (the
         * Waveshare official driver also leaves both at -1 and
         * relies on polling instead of an INT IRQ).
         *
         * The controller reports coordinates in landscape pixels
         * (640w x 172h) but with both axes flipped relative to
         * the LCD scan-out: Waveshare's USER_DISP_ROT_90 handler
         * remaps point.x = LCD_H_RES - rawX, point.y = LCD_V_RES
         * - rawY, which is exactly the (mirror_x = mirror_y = 1,
         * swap_xy = 0) combination we apply in touchscreen.c. */
        .i2c_port = 1,
        .sda      = 17,
        .scl      = 18,
        .intr     = -1,
        .rst      = -1,
        .addr     = 0x3B,
        .freq_hz  = 400000,
        .native_w = 640,
        .native_h = 172,
        .mirror_x = true,
        .mirror_y = true,
        .swap_xy  = false,
    },
    .battery_adc_channel = -1,
};

/* Compile-time guards against re-introducing the display/gamepad pin
 * overlap that left the panel permanently black. The set of GPIOs the
 * display owns is { 8, 9, 10, 11, 12, 13, 14, 21 }; the audio codec
 * owns { 7, 15, 45, 46 }; the on-board capacitive-touch I2C bus is
 * on { 17, 18 }. Any gamepad pin landing in any of those sets means
 * the gamepad bus master will drive the same wire as the LCD / codec
 * / touch driver, corrupting both.
 *
 * Additionally enforce membership in the set of GPIOs the user has
 * confirmed are broken out and free on this board:
 *   { 0, 1, 2, 3, 4, 5, 19, 20, 33, 39, 40, 41, 43, 44 }
 * Picking anything else risks colliding with on-board peripherals
 * (USB, UART0, flash/PSRAM, strapping pins) that are not represented
 * in the conflict macros above. */
#include <assert.h>
#define SK_PIN_AVAILABLE(p) ( \
    (p) == 0  || (p) == 1  || (p) == 2  || (p) == 3  || (p) == 4 || \
    (p) == 5  || (p) == 19 || (p) == 20 || (p) == 33 || (p) == 39 || \
    (p) == 40 || (p) == 41 || (p) == 43 || (p) == 44)
#define SK_PIN_CONFLICTS_DISPLAY(p) ( \
    (p) == 8  || (p) == 9  || (p) == 10 || (p) == 11 || (p) == 12 || \
    (p) == 13 || (p) == 14 || (p) == 21)
#define SK_PIN_CONFLICTS_AUDIO(p) ( \
    (p) == 7 || (p) == 15 || (p) == 45 || (p) == 46)
#define SK_PIN_CONFLICTS_TOUCH(p) ( \
    (p) == 17 || (p) == 18)
#define SK_GAMEPAD_PIN_OK(p) \
    (SK_PIN_AVAILABLE(p) && \
     !SK_PIN_CONFLICTS_DISPLAY(p) && \
     !SK_PIN_CONFLICTS_AUDIO(p) && \
     !SK_PIN_CONFLICTS_TOUCH(p))

#if CONFIG_SK_GAMEPAD_TRANSPORT_I2C
_Static_assert(SK_GAMEPAD_PIN_OK(1),
    "Waveshare 3.49: gamepad I2C SDA must be in the board's free-pin set "
    "and must not overlap display/audio pins");
_Static_assert(SK_GAMEPAD_PIN_OK(2),
    "Waveshare 3.49: gamepad I2C SCL must be in the board's free-pin set "
    "and must not overlap display/audio pins");
#endif
#if CONFIG_SK_GAMEPAD_TRANSPORT_SPI
_Static_assert(SK_GAMEPAD_PIN_OK(4),
    "Waveshare 3.49: gamepad SPI SCLK must be in the board's free-pin set "
    "and must not overlap display/audio pins");
_Static_assert(SK_GAMEPAD_PIN_OK(5),
    "Waveshare 3.49: gamepad SPI MOSI must be in the board's free-pin set "
    "and must not overlap display/audio pins");
_Static_assert(SK_GAMEPAD_PIN_OK(39),
    "Waveshare 3.49: gamepad SPI MISO must be in the board's free-pin set "
    "and must not overlap display/audio pins");
_Static_assert(SK_GAMEPAD_PIN_OK(40),
    "Waveshare 3.49: gamepad SPI CS must be in the board's free-pin set "
    "and must not overlap display/audio pins");
#endif

#endif /* CONFIG_SK_BOARD_WAVESHARE_ESP32S3_TOUCH_LCD_349 */
