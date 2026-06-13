/*
 * Gamepad-event -> keyboard/HID action router.
 *
 * Button map (numbered buttons match the gamepad wire-report bit
 * positions, see components/gamepad_uart):
 *
 *   GP_BTN_0  normal keypress       / left mouse click
 *   GP_BTN_1  shifted keypress      / right mouse click
 *   GP_BTN_2  Space
 *   GP_BTN_3  Enter
 *   GP_BTN_4  Backspace
 *   GP_BTN_5  Ctrl   (sticky modifier toggle)
 *   GP_BTN_6  AltGr  (sticky modifier toggle, right Alt)
 *   GP_BTN_7  unused
 *   GP_BTN_8  unused
 *   GP_BTN_9  on down -> mouse mode, on up -> keyboard mode
 *
 * The D-pad / analog directions move the selection cursor (or the
 * mouse pointer in mouse mode, or the menu selection while the
 * settings menu is open).
 */

#include "input_router.h"

#include <stdbool.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "gamepad_uart.h"
#include "keyboard_ui.h"
#include "hid.h"
#include "kb_layout.h"
#include "narrator.h"

static const char *TAG = "input_router";

/* Hold-to-repeat parameters. */
#define INITIAL_REPEAT_MS 350
#define REPEAT_INTERVAL_MS 80

#define MOUSE_STEP 8  /* pixels per poll while a D-pad is held */

/* Per-button state, used to drive hold-to-repeat. */
typedef struct {
    bool     down;
    uint32_t down_at_ms;
    uint32_t last_repeat_ms;
} btn_state_t;

static btn_state_t s_b[GP_BTN_COUNT];

static inline bool is_dir(gamepad_button_t b)
{
    return b == GP_BTN_UP || b == GP_BTN_DOWN
        || b == GP_BTN_LEFT || b == GP_BTN_RIGHT;
}

static void dir_apply(gamepad_button_t b)
{
    int dr = 0, dc = 0;
    switch (b) {
    case GP_BTN_UP:    dr = -1; break;
    case GP_BTN_DOWN:  dr =  1; break;
    case GP_BTN_LEFT:  dc = -1; break;
    case GP_BTN_RIGHT: dc =  1; break;
    default: return;
    }
    switch (keyboard_ui_get_mode()) {
    case KB_MODE_MENU:
        /* Up/Down move the cursor; Left/Right change the value. */
        if (dr) keyboard_ui_menu_move(dr);
        else    keyboard_ui_menu_adjust(dc);
        break;
    case KB_MODE_MOUSE:
        hid_send_mouse(dc * MOUSE_STEP, dr * MOUSE_STEP, 0, 0);
        break;
    case KB_MODE_KEYBOARD:
    default:
        if (keyboard_ui_move(dr, dc)) {
            /* Speak the newly selected key (no-op when the
             * narrator is disabled). */
            keyboard_ui_narrate_selection();
        }
        break;
    }
}

/* Send a fixed key (Space / Enter / Backspace) and speak it. These
 * are language-neutral and not tied to the selection cursor. */
static void send_fixed(uint8_t usage)
{
    hid_send_key(0, usage);
    hid_release_all();
    narrator_speak_hid(usage);
}

static void handle_down(gamepad_button_t b, uint32_t now)
{
    s_b[b].down = true;
    s_b[b].down_at_ms = now;
    s_b[b].last_repeat_ms = now;

    if (is_dir(b)) {
        dir_apply(b);
        return;
    }

    keyboard_ui_mode_t mode = keyboard_ui_get_mode();

    switch (b) {
    case GP_BTN_0:
        if (mode == KB_MODE_MENU) {
            keyboard_ui_menu_select();
        } else if (mode == KB_MODE_MOUSE) {
            hid_send_mouse(0, 0, HID_MS_BTN_LEFT, 0);
            hid_send_mouse(0, 0, 0, 0);
        } else {
            /* Normal keypress: applies + clears sticky modifiers
             * and narrates inside keyboard_ui_press_current(). */
            keyboard_ui_press_current();
        }
        break;
    case GP_BTN_1:
        if (mode == KB_MODE_MOUSE) {
            hid_send_mouse(0, 0, HID_MS_BTN_RIGHT, 0);
            hid_send_mouse(0, 0, 0, 0);
        } else if (mode == KB_MODE_KEYBOARD) {
            /* Shifted keypress: latch a one-shot Shift, then press
             * the selected key (which clears it again). */
            keyboard_ui_oneshot_mod(HID_MOD_LSHIFT);
            keyboard_ui_press_current();
        }
        break;
    case GP_BTN_2:
        send_fixed(HID_USAGE_SPACE);
        break;
    case GP_BTN_3:
        send_fixed(HID_USAGE_ENTER);
        break;
    case GP_BTN_4:
        send_fixed(HID_USAGE_BACKSPACE);
        break;
    case GP_BTN_5:
        /* Sticky Ctrl: held until the next character is pressed. */
        keyboard_ui_toggle_mod(HID_MOD_LCTRL);
        break;
    case GP_BTN_6:
        /* Sticky AltGr (right Alt). */
        keyboard_ui_toggle_mod(HID_MOD_RALT);
        break;
    case GP_BTN_9:
        /* On down: enter mouse mode. */
        keyboard_ui_set_mode(KB_MODE_MOUSE);
        break;
    case GP_BTN_7:
    case GP_BTN_8:
    default:
        /* Unused. */
        break;
    }
}

static void handle_up(gamepad_button_t b)
{
    s_b[b].down = false;
    if (b == GP_BTN_9) {
        /* On up: enter keyboard mode. */
        keyboard_ui_set_mode(KB_MODE_KEYBOARD);
    }
}

static void tick_repeat(uint32_t now)
{
    for (int i = 0; i < GP_BTN_COUNT; ++i) {
        if (!s_b[i].down) continue;
        if (!is_dir((gamepad_button_t)i)) continue;
        uint32_t since_down = now - s_b[i].down_at_ms;
        if (since_down < INITIAL_REPEAT_MS) continue;
        if (now - s_b[i].last_repeat_ms < REPEAT_INTERVAL_MS) continue;
        s_b[i].last_repeat_ms = now;
        dir_apply((gamepad_button_t)i);
    }
}

static void router_task(void *arg)
{
    QueueHandle_t q = (QueueHandle_t)arg;
    gamepad_event_t ev;

    while (1) {
        /* Wake up at least every 20 ms so we can service
         * hold-to-repeat even while no new events arrive. */
        if (xQueueReceive(q, &ev, pdMS_TO_TICKS(20)) == pdTRUE) {
            ESP_LOGD(TAG, "%s %s",
                     gamepad_button_name(ev.button),
                     ev.pressed ? "down" : "up");
            if (ev.pressed) handle_down(ev.button, ev.time_ms);
            else            handle_up(ev.button);
        }
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        tick_repeat(now);
    }
}

void input_router_start(QueueHandle_t events)
{
    if (!events) return;
    xTaskCreatePinnedToCore(router_task, "input_rt", 4096,
                            (void *)events, 5, NULL, 0);
}
