#pragma once

/*
 * External I2C gamepad client.
 *
 * The gamepad is treated as a simplified HID device that ships
 * a fixed-length report whenever the master reads it. The
 * report layout below is a reasonable v1 baseline; if your
 * controller speaks something different, override the parser
 * by editing gamepad_parse_report() in src/gamepad_i2c.c.
 *
 *   byte 0:   X axis, signed -128..127 (0 = centred)
 *   byte 1:   Y axis, signed -128..127 (0 = centred, positive = down)
 *   byte 2:   button bitmap A=0x01 B=0x02 X=0x04 Y=0x08
 *   byte 3:   button bitmap L=0x01 R=0x02 SELECT=0x04 START=0x08
 *
 * The driver polls every CONFIG_SK_GAMEPAD_POLL_MS milliseconds
 * and emits edge events on a FreeRTOS queue. Axes are converted
 * to discrete N/S/E/W "presses" with a dead-zone given by
 * CONFIG_SK_GAMEPAD_AXIS_DEADZONE so that input_router can treat
 * the D-pad and analog stick uniformly.
 */

#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* D-pad / stick directions (edge-triggered). */
    GP_BTN_UP = 0,
    GP_BTN_DOWN,
    GP_BTN_LEFT,
    GP_BTN_RIGHT,
    /* Face buttons. */
    GP_BTN_A,
    GP_BTN_B,
    GP_BTN_X,
    GP_BTN_Y,
    /* Shoulders & menu. */
    GP_BTN_L,
    GP_BTN_R,
    GP_BTN_SELECT,
    GP_BTN_START,

    GP_BTN_COUNT,
} gamepad_button_t;

typedef struct {
    gamepad_button_t button;
    bool             pressed;    /* true = down edge, false = up edge */
    uint32_t         time_ms;    /* monotonic timestamp               */
} gamepad_event_t;

/*
 * Start the gamepad task. Returns the queue handle on which
 * gamepad_event_t messages will arrive (queue size ~16).
 * Returns NULL on error.
 *
 * Safe to call after board_init(); installs an I2C master on
 * the board's i2c_port and starts a low-priority polling task
 * pinned to core 0.
 */
QueueHandle_t gamepad_i2c_start(void);

/* Convenience for diagnostics. The string is statically
 * allocated and valid for the lifetime of the process. */
const char *gamepad_button_name(gamepad_button_t b);

#ifdef __cplusplus
}
#endif
