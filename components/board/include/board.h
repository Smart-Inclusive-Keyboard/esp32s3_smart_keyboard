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
} board_display_type_t;

typedef struct {
    /* QSPI display pins (AXS15231B). All -1 when display absent. */
    int cs;
    int sck;
    int d0;
    int d1;
    int d2;
    int d3;
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
                         * the gamepad bus to avoid pin clashes) */
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

typedef struct {
    const char         *name;            /* human-readable board id   */
    board_display_type_t display_type;
    board_display_t     display;

    /* I2C bus used to talk to the external gamepad. */
    int  i2c_port;
    int  i2c_sda;
    int  i2c_scl;
    int  i2c_freq_hz;

    /* SPI bus used to talk to the external gamepad when the
     * SPI transport is selected. The device is always the
     * host / master; the gamepad is the slave. All -1 when
     * SPI is not wired on the board (or not selected). */
    int  spi_host;
    int  spi_sclk;
    int  spi_mosi;
    int  spi_miso;
    int  spi_cs;
    int  spi_freq_hz;
    int  spi_mode;

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
 * GPIO; the I2C / I2S / SPI buses are owned by their respective
 * components (gamepad_i2c, audio, display) and configured by them
 * from the board_t struct.
 *
 * Idempotent and safe to call exactly once from app_main() before
 * any other component init.
 */
void board_init(void);

#ifdef __cplusplus
}
#endif
