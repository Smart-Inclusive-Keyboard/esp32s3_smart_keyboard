/*
 * Gamepad-event -> keyboard/HID action router.
 */

#include "input_router.h"

#include <stdbool.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "gamepad_i2c.h"
#include "keyboard_ui.h"
#include "hid.h"
#include "kb_layout.h"
#include "narrator.h"

static const char *TAG = "input_router";

/* Hold-to-repeat parameters. */
#define INITIAL_REPEAT_MS 350
#define REPEAT_INTERVAL_MS 80

#define MOUSE_STEP 8  /* pixels per poll while a D-pad is held */

/* Per-button state, used to drive hold-to-repeat and chord
 * detection. */
typedef struct {
    bool     down;
    uint32_t down_at_ms;
    uint32_t last_repeat_ms;
} btn_state_t;

static btn_state_t s_b[GP_BTN_COUNT];

/* True while both L and R are held (used to detect the
 * "toggle mouse mode" chord on the up-edge of either). */
static bool s_lr_chord_active;

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
    if (keyboard_ui_get_mode() == KB_MODE_MOUSE) {
        hid_send_mouse(dc * MOUSE_STEP, dr * MOUSE_STEP, 0, 0);
    } else {
        if (keyboard_ui_move(dr, dc)) {
            const kb_layout_t *l = kb_layout_active();
            /* narrator (no-op when disabled) */
            (void)l;
            narrator_speak_selection();
        }
    }
}

static void press_with_mod(uint8_t mod)
{
    keyboard_ui_oneshot_mod(mod);
    keyboard_ui_press_current();
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

    switch (b) {
    case GP_BTN_1:
        if (keyboard_ui_get_mode() == KB_MODE_MOUSE) {
            hid_send_mouse(0, 0, HID_MS_BTN_LEFT, 0);
            hid_send_mouse(0, 0, 0, 0);
        } else {
            press_with_mod(0);
        }
        break;
    case GP_BTN_3:
        press_with_mod(HID_MOD_LSHIFT);
        break;
    case GP_BTN_2:
        if (keyboard_ui_get_mode() == KB_MODE_MOUSE) {
            hid_send_mouse(0, 0, HID_MS_BTN_RIGHT, 0);
            hid_send_mouse(0, 0, 0, 0);
        } else {
            press_with_mod(HID_MOD_LCTRL);
        }
        break;
    case GP_BTN_4:
        press_with_mod(HID_MOD_LALT);
        break;
    case GP_BTN_5:
        if (s_b[GP_BTN_6].down) {
            s_lr_chord_active = true;
            keyboard_ui_set_mode(
                keyboard_ui_get_mode() == KB_MODE_MOUSE
                    ? KB_MODE_KEYBOARD : KB_MODE_MOUSE);
        } else {
            hid_send_key(0, HID_USAGE_BACKSPACE);
            hid_release_all();
        }
        break;
    case GP_BTN_6:
        if (s_b[GP_BTN_5].down) {
            s_lr_chord_active = true;
            keyboard_ui_set_mode(
                keyboard_ui_get_mode() == KB_MODE_MOUSE
                    ? KB_MODE_KEYBOARD : KB_MODE_MOUSE);
        } else {
            keyboard_ui_toggle_mod(HID_MOD_LSHIFT);
        }
        break;
    case GP_BTN_8:
        keyboard_ui_cycle_theme();
        break;
    case GP_BTN_7:
        keyboard_ui_cycle_layout();
        break;
    default:
        break;
    }
}

static void handle_up(gamepad_button_t b)
{
    s_b[b].down = false;
    if (b == GP_BTN_5 || b == GP_BTN_6) {
        /* Releasing one half of the 5+6 (shoulder) chord clears
         * the latch so the next press of either isn't
         * misinterpreted as a mode toggle. */
        if (!s_b[GP_BTN_5].down && !s_b[GP_BTN_6].down) {
            s_lr_chord_active = false;
        }
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
