#pragma once

/*
 * Hardware Abstraction Layer for the Smart Keyboard.
 *
 * Every board-specific decision (pin numbers, panel controller,
 * presence of PSRAM / speaker / battery) is funnelled through
 * board.h. Components downstream of this file see a single
 * `board_t` instance returned by board_get(); they never call
 * gpio_num_t directly from board-specific code, and they branch
 * on capabilities via CONFIG_BOARD_HAS_xxx Kconfig flags.
 *
 * Adding a new board:
 *   1. Add a `config SK_BOARD_<NAME>` choice entry in the
 *      project-root Kconfig.projbuild and "select" the
 *      capability flags it supports.
 *   2. Create components/board/src/board_<name>.c that defines
 *      `board_t g_board = { ... }` gated by its CONFIG symbol.
 *   3. Reference the new source file from
 *      components/board/CMakeLists.txt (already done for the
 *      shipped boards).
 */

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Panel controller families. Extend as new backends are added. */
typedef enum {
    BOARD_DISPLAY_NONE = 0,
    BOARD_DISPLAY_AXS15231B,
    BOARD_DISPLAY_ILI9341,
} board_display_type_t;

typedef struct {
    /* Display bus pins.
     *
     * The AXS15231B backend uses these as a 4-line QSPI bus:
     *   cs, sck, d0, d1, d2, d3.
     *
     * The ILI9341 backend uses a plain 4-wire SPI bus and reuses
     * the same fields:
     *   cs   = chip-select
     *   sck  = clock (SCLK)
     *   d0   = MOSI (SDA), the only data line written
     *   d1   = MISO, -1 when not wired (the panel is write-only)
     *   d2, d3 = unused, -1
     *   dc   = data/command select line (SPI displays only)
     *
     * All -1 when the display is absent.
     */
    int cs;
    int sck;
    int d0;
    int d1;
    int d2;
    int d3;
    int dc;            /* data/command line (SPI panels), -1 if N/A */
    int rst;
    int te;            /* tearing-effect input, -1 if unused      */
    int bl;            /* backlight enable, -1 if always-on       */
    bool bl_active_low;/* true = LEDC duty 0 means fully bright   */

    int width;         /* logical width (after rotation)          */
    int height;        /* logical height (after rotation)         */
    bool swap_xy;      /* rotate 90 deg CW in software at flush   */
} board_display_t;

typedef struct {
    int bclk;
    int lrck;
    int dout;
    int mclk;          /* master clock out, -1 when unused (codecs
                        * like the ES8311 on the Waveshare 3.5B
                        * board need it; bare DACs like MAX98357A
                        * do not)                                  */
    int port;          /* I2S port number */
} board_i2s_t;

/*
 * Optional I2C-controlled audio codec attached to the I2S TX
 * path. Populated only when BOARD_HAS_CODEC_ES8311 is selected;
 * otherwise i2c_port is -1 and the audio component skips codec
 * bring-up and assumes a bare-DAC speaker path.
 *
 * On the Waveshare 3.5B the codec I2C bus is physically shared
 * with the capacitive touch controller (SDA = 8, SCL = 7), so
 * the audio component creates the bus and the touchscreen
 * component re-acquires it via i2c_master_get_bus_handle().
 */
typedef struct {
    int     i2c_port;
    int     sda;
    int     scl;
    uint8_t addr;       /* 7-bit codec I2C address (ES8311 = 0x18) */
    int     freq_hz;    /* I2C clock (codec accepts up to 400 kHz)  */
    int     pa_pin;     /* class-D PA enable pin, -1 if not wired   */
} board_codec_t;

/*
 * Optional capacitive touchscreen overlay attached to the LCD.
 *
 * Populated only when CONFIG_BOARD_HAS_TOUCH is selected for the
 * chosen board; otherwise the pins are -1 and the touchscreen
 * component compiles to a no-op.
 *
 * The controller speaks the AXS5106-family "magic packet" I2C
 * protocol (the same chip family that drives the AXS15231B
 * display on the Waveshare 3.5B board). Coordinates the
 * controller reports are in panel-native orientation; the
 * mirror_x / mirror_y / swap_xy / native_w / native_h fields
 * map them onto the logical (post-rotation) framebuffer that
 * the display backend exposes via display_width() /
 * display_height().
 */
typedef struct {
    int     i2c_port;   /* I2C peripheral number (separate from
                         * other buses to avoid pin clashes)     */
    int     sda;
    int     scl;
    int     intr;       /* INT line, -1 if not wired              */
    int     rst;        /* RST line, -1 if not wired              */
    uint8_t addr;       /* 7-bit I2C address (AXS5106 = 0x3B)     */
    int     freq_hz;    /* I2C clock                               */
    int     native_w;   /* controller's native pixel width        */
    int     native_h;   /* controller's native pixel height       */
    bool    mirror_x;   /* flip raw X before mapping to logical   */
    bool    mirror_y;   /* flip raw Y before mapping to logical   */
    bool    swap_xy;    /* swap axes (apply after mirror_*)       */
} board_touch_t;

/* One receive-only UART link to an external gamepad board. rx is
 * -1 when this gamepad is not wired. */
typedef struct {
    int port;   /* UART peripheral number (UART1 / UART2)          */
    int rx;     /* RX GPIO (gamepad TX line); -1 = not wired       */
    int baud;   /* link baud rate (8-N-1)                          */
} board_gamepad_uart_t;

typedef struct {
    const char         *name;            /* human-readable board id   */
    board_display_type_t display_type;
    board_display_t     display;

    /* UART links used to receive the external gamepads' HID
     * reports. Two gamepads are supported; both feed the same
     * on-screen keyboard. Each link is receive-only (8-N-1); rx is
     * the GPIO the gamepad's TX line is wired to, and is -1 when
     * that gamepad UART is not wired on the board. */
    board_gamepad_uart_t gamepad_uart[2];

    /* Optional I2S audio output (only valid when CONFIG_BOARD_HAS_SPEAKER). */
    board_i2s_t i2s;

    /* Optional I2C-controlled codec (only valid when
     * CONFIG_BOARD_HAS_CODEC_ES8311). */
    board_codec_t codec;

    /* Optional capacitive touchscreen (only valid when
     * CONFIG_BOARD_HAS_TOUCH). */
    board_touch_t touch;

    /* Optional battery ADC channel (only valid when CONFIG_BOARD_HAS_BATTERY).
     * < 0 when not implemented. */
    int battery_adc_channel;
} board_t;

/*
 * Returns the singleton describing the compiled-in board. The
 * returned pointer is valid for the lifetime of the process.
 * Never NULL.
 */
const board_t *board_get(void);

/*
 * Initialize whatever shared peripherals the board needs before
 * the rest of the firmware comes up. Today this is limited to a
 * short panel-reset pulse on boards that have a controllable RST
 * GPIO; the I2C / I2S / UART buses are owned by their respective
 * components (gamepad_uart, audio, display) and configured by them
 * from the board_t struct.
 *
 * Idempotent and safe to call exactly once from app_main() before
 * any other component init.
 */
void board_init(void);

#ifdef __cplusplus
}
#endif
