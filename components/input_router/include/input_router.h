#pragma once

/*
 * Gamepad-event router.
 *
 * Reads gamepad_event_t from the gamepad_i2c queue, translates
 * them into UI / BLE-HID actions, and (optionally) pings the
 * narrator on selection changes.
 *
 * Button mapping (configurable later via Kconfig / on-screen
 * settings menu):
 *   D-pad     -> keyboard_ui_move() with hold-to-repeat
 *   A         -> press selected key (no modifier)
 *   X         -> Shift + selected key
 *   B         -> Ctrl  + selected key
 *   Y         -> Alt   + selected key
 *   L         -> Backspace
 *   R         -> toggle Shift sticky
 *   START     -> cycle theme
 *   SELECT    -> cycle layout
 *   L + R     -> toggle mouse mode  (chord)
 *
 * In mouse mode, the D-pad drives the cursor (8 px / poll), A
 * is left-click and B is right-click.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Spawn the router task. `events` is the queue handle returned
 * by gamepad_i2c_start(). Must be called after keyboard_ui_init()
 * and ble_hid_init(). */
void input_router_start(QueueHandle_t events);

#ifdef __cplusplus
}
#endif
