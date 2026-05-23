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
                        * like the ES8311 on the Waveshare 3.49
                        * board need it; bare DACs like MAX98357A
                        * do not)                                  */
    int port;          /* I2S port number */
} board_i2s_t;

typedef struct {
    const char         *name;            /* human-readable board id   */
    board_display_type_t display_type;
    board_display_t     display;

    /* I2C bus used to talk to the external gamepad. */
    int  i2c_port;
    int  i2c_sda;
    int  i2c_scl;
    int  i2c_freq_hz;

    /* Optional I2S audio output (only valid when CONFIG_BOARD_HAS_SPEAKER). */
    board_i2s_t i2s;

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
