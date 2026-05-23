#pragma once

/*
 * Virtual on-screen keyboard.
 *
 * Owns the user-visible state: selection cursor, modifier latches
 * (Shift / Ctrl / Alt), connection status text, mouse-mode flag.
 * Pure data + draw -- no peripherals touched. Driven by the
 * input_router, which translates gamepad events into the
 * keyboard_ui_handle_* calls below.
 *
 * The active layout and theme are resolved at draw time via
 * kb_layout_active() / theme_active().
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KB_MODE_KEYBOARD = 0,
    KB_MODE_MOUSE,
} keyboard_ui_mode_t;

/* Initialize after display_init() so we can size the grid to the
 * actual framebuffer. Persists the active layout / theme to NVS
 * on the next change. */
void keyboard_ui_init(void);

/* Move the selection cursor (clamped to the active layout grid).
 * Returns true if the selection actually moved. */
bool keyboard_ui_move(int drow, int dcol);

/* Toggle a sticky modifier (KB_MOD_LSHIFT / _LCTRL / _LALT from
 * ble_hid.h). Calling with mod = 0 clears all modifiers. */
void keyboard_ui_toggle_mod(uint8_t mod);

/* Latch a one-shot modifier for the next keypress. Useful for
 * "X = Shift+key" without needing to depress Shift first. */
void keyboard_ui_oneshot_mod(uint8_t mod);

/* Press the currently selected key with the active modifier set
 * (sticky + one-shot OR'd together). Sends a key-down + key-up
 * pair to the host via ble_hid. */
void keyboard_ui_press_current(void);

/* Status-bar updates from outside. The strings are copied. */
void keyboard_ui_set_ble_status(const char *text, bool connected);
void keyboard_ui_set_passkey(uint32_t passkey);

/* Switch between text-entry and mouse-cursor modes. In mouse
 * mode the keyboard area is replaced with cursor velocity
 * indicators; the actual mouse-report dispatch lives in
 * input_router. */
void keyboard_ui_set_mode(keyboard_ui_mode_t mode);
keyboard_ui_mode_t keyboard_ui_get_mode(void);

/* Mark the whole screen dirty and redraw. Cheap; call after any
 * state change you want reflected on screen. */
void keyboard_ui_request_redraw(void);

/* The UI task pump. Spawned from app_main(); takes redraw
 * requests off a queue and walks display_*. */
void keyboard_ui_start_task(void);

/* HID usage of the currently selected cell (HID_USAGE_NONE if
 * the cell is empty). Exposed for the narrator. */
int keyboard_ui_selected_hid_usage(void);

/* Cycle to the next built-in keyboard layout (US -> DE -> FR ->
 * UA -> US). Persists the choice in NVS. */
void keyboard_ui_cycle_layout(void);

/* Cycle to the next built-in theme. Persists the choice in NVS. */
void keyboard_ui_cycle_theme(void);

#ifdef __cplusplus
}
#endif
