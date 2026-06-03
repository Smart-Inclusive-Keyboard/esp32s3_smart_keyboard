#pragma once

/*
 * Capacitive touchscreen input.
 *
 * On boards that select CONFIG_BOARD_HAS_TOUCH, brings up the
 * I2C bus described by board_t::touch and runs a polling task
 * that translates the controller's raw points into framebuffer-
 * space (x, y) coordinates and invokes a user-supplied callback
 * on the down-edge of each tap.
 *
 * Today this targets the AXS5106 / AXS15231B touch-controller
 * family (same chip family that drives the AXS15231B display on
 * the Waveshare 3.49 board). The protocol is the "magic packet"
 * one: the host writes an 8-byte vendor preamble + read-length
 * byte and the controller responds with 8 bytes containing a
 * gesture id, point count and one (x, y) pair.
 *
 * The component is a no-op (init / start return false, no task
 * spawned, no I2C bus opened) on boards without
 * CONFIG_BOARD_HAS_TOUCH, so call sites can be unconditional.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Callback invoked on the down-edge of each tap. (x, y) are in
 * the same coordinate space as display_width() / display_height(),
 * i.e. logical post-rotation framebuffer pixels.
 *
 * Runs in the touchscreen task context; keep the work short and
 * push to a queue if heavier processing is needed. */
typedef void (*touchscreen_tap_cb_t)(int x, int y);

/* Initialize the I2C bus and probe the controller. Idempotent.
 * Returns true if a touchscreen was found and is ready to poll,
 * false otherwise (no touch hardware on this board, or the
 * controller did not acknowledge). */
bool touchscreen_init(void);

/* Start the polling task. `cb` is invoked on the down-edge of
 * each new tap (one call per finger-down, no repeats while the
 * finger is held). Safe to call only after touchscreen_init()
 * returned true; otherwise a no-op. */
void touchscreen_start(touchscreen_tap_cb_t cb);

/* True after a successful touchscreen_init(). */
bool touchscreen_is_available(void);

#ifdef __cplusplus
}
#endif
