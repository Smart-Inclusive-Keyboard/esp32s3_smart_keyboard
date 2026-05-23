#pragma once

/*
 * External SPI gamepad client.
 *
 * The device acts as the SPI host (master) and the gamepad as
 * the SPI slave. The driver mirrors gamepad_i2c:
 *
 *   - Same 4-byte simplified-HID report layout (see
 *     gamepad_i2c.h for byte semantics).
 *   - Same gamepad_event_t edge events on the returned
 *     FreeRTOS queue.
 *   - Same poll cadence (CONFIG_SK_GAMEPAD_POLL_MS) and
 *     axis dead-zone (CONFIG_SK_GAMEPAD_AXIS_DEADZONE).
 *
 * Pin / clock / mode settings come from CONFIG_SK_GAMEPAD_SPI_*
 * (and are surfaced on the board_t via .spi_*).
 *
 * Only one transport is built at a time, selected by the
 * CONFIG_SK_GAMEPAD_TRANSPORT_{I2C,SPI} Kconfig choice. When SPI
 * is not selected, gamepad_spi_start() returns NULL and links
 * to a no-op stub.
 */

#include "gamepad_i2c.h"  /* gamepad_event_t, gamepad_button_t, gamepad_button_name */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start the SPI gamepad task. Returns the queue handle on which
 * gamepad_event_t messages will arrive (queue size ~16).
 * Returns NULL on error or when the SPI transport is not
 * selected at build time.
 *
 * Safe to call after board_init(); installs an SPI bus + device
 * on the board's spi_host and starts a low-priority polling
 * task pinned to core 0.
 */
QueueHandle_t gamepad_spi_start(void);

#ifdef __cplusplus
}
#endif
