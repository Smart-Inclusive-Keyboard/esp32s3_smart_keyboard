#pragma once

/*
 * External UART gamepad client.
 *
 * The gamepad is a separate board (see
 * https://github.com/clackups/esp32s3_dual_foc_gp) that streams
 * its HID report over a one-way serial link. This firmware only
 * receives: 8-N-1 UART at CONFIG_SK_GAMEPAD_UART_BAUD baud
 * (115200 by default), RX only -- no TX / RTS / CTS line is
 * driven.
 *
 * Wire report layout (6 bytes, identical to the USB / UART HID
 * report emitted by the gamepad firmware):
 *
 *   byte 0:   buttons 0..7   (bit i set = HID button i+1 pressed)
 *   byte 1:   buttons 8..9   (bits 0..1) + 6 bits padding
 *   byte 2:   X axis, signed 16-bit little-endian, low byte
 *   byte 3:   X axis, high byte (-32767..32767, 0 = centred,
 *             positive = right)
 *   byte 4:   Y axis, signed 16-bit little-endian, low byte
 *   byte 5:   Y axis, high byte (-32767..32767, 0 = centred,
 *             positive = down)
 *
 * There are 10 HID buttons. The firmware does not map them back
 * to vendor-specific letter names (A/B/X/Y, Cross/Circle/...);
 * the input router operates directly on numbered buttons.
 *
 * The driver reads one 6-byte frame at a time off the UART and
 * emits edge events on a FreeRTOS queue. The analog axes are
 * converted to discrete N/S/E/W "presses" with a dead-zone given
 * by CONFIG_SK_GAMEPAD_AXIS_DEADZONE so that input_router can
 * treat the D-pad and analog stick uniformly.
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
    /* Numbered HID buttons. The numeric suffix matches the
     * button index used in the wire report (and in standard
     * HID Button-page usages). We intentionally do not bake
     * vendor letter names (A/B/X/Y, L/R/SELECT/START) into the
     * API so the same code works across controllers with
     * different button silkscreens. */
    GP_BTN_1,
    GP_BTN_2,
    GP_BTN_3,
    GP_BTN_4,
    GP_BTN_5,
    GP_BTN_6,
    GP_BTN_7,
    GP_BTN_8,
    GP_BTN_9,
    GP_BTN_10,

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
 * Safe to call after board_init(); installs the UART driver in
 * receive-only mode on the board's uart_port / uart_rx and
 * starts a low-priority RX task pinned to core 0.
 */
QueueHandle_t gamepad_uart_start(void);

/* Convenience for diagnostics. The string is statically
 * allocated and valid for the lifetime of the process. */
const char *gamepad_button_name(gamepad_button_t b);

#ifdef __cplusplus
}
#endif
