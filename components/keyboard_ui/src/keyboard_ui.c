/*
 * Virtual-keyboard renderer and selection state machine.
 *
 * Layout
 * ------
 *   +----------------------------------------------------------+
 *   | status bar : layout | mods | BLE | mode                  |
 *   +----------------------------------------------------------+
 *   | key grid: rows x cols, selected cell highlighted         |
 *   +----------------------------------------------------------+
 *
 * The grid auto-sizes to (display_width / cols) x (display_height
 * - status_height) / rows. On the 640x172 Waveshare panel and
 * the US 14x5 layout that gives roughly 45x30-pixel cells with
 * 2x-scaled 8x8 glyphs centered inside.
 *
 * Drawing happens on a FreeRTOS task driven by a redraw queue
 * so the input handlers can fire keyboard_ui_request_redraw()
 * cheaply from any context.
 */

#include "keyboard_ui.h"

#include <stdio.h>
#include <string.h>

#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "display.h"
#include "theme.h"
#include "fonts.h"
#include "kb_layout.h"
#include "ble_hid.h"

static const char *TAG = "kb_ui";

#define STATUS_BAR_H 14
#define NVS_NAMESPACE "sk_ui"

typedef struct {
    int  sel_row;
    int  sel_col;
    uint8_t  mod_sticky;    /* persistent across keypresses     */
    uint8_t  mod_oneshot;   /* cleared after the next keypress  */
    bool     ble_connected;
    uint32_t passkey;       /* 0 = none, else 6-digit value     */
    keyboard_ui_mode_t mode;
    char     ble_status[32];
} ui_state_t;

static ui_state_t s_st;
static QueueHandle_t s_redraw_q;

/* ----- NVS persistence ----- */

static void nvs_save_strings(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "layout", kb_layout_active()->name);
    nvs_set_str(h, "theme",  theme_active()->name);
    nvs_commit(h);
    nvs_close(h);
}

static void nvs_load_strings(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    char buf[16];
    size_t len = sizeof(buf);
    if (nvs_get_str(h, "layout", buf, &len) == ESP_OK) {
        kb_layout_set_active_by_name(buf);
    }
    len = sizeof(buf);
    if (nvs_get_str(h, "theme", buf, &len) == ESP_OK) {
        theme_set_active_by_name(buf);
    }
    nvs_close(h);
}

/* ----- Drawing ----- */

static void draw_status_bar(const theme_t *th)
{
    int w = display_width();
    display_fill_rect(0, 0, w, STATUS_BAR_H, th->status_bar_bg);

    char line[80];
    /* Left side: layout name + modifier indicators. */
    snprintf(line, sizeof(line), "[%s]", kb_layout_active()->name);
    display_draw_string(2, 3, line, 1, th->status_ind_fg,
                        th->status_bar_bg, true);

    int x = 2 + (int)strlen(line) * 8 + 8;
    const struct { const char *t; uint8_t m; } mods[] = {
        { "Sh", KB_MOD_LSHIFT },
        { "Ct", KB_MOD_LCTRL  },
        { "Al", KB_MOD_LALT   },
    };
    for (size_t i = 0; i < sizeof(mods) / sizeof(mods[0]); ++i) {
        bool on = (s_st.mod_sticky | s_st.mod_oneshot) & mods[i].m;
        uint16_t bg = on ? th->status_ind_active_bg : th->status_bar_bg;
        uint16_t fg = on ? th->status_ind_active_fg : th->status_ind_fg;
        display_fill_rect(x - 2, 1, 2 * 8 + 4, STATUS_BAR_H - 2, bg);
        display_draw_string(x, 3, mods[i].t, 1, fg, bg, false);
        x += 2 * 8 + 8;
    }

    /* Right side: BLE indicator + status text. */
    uint16_t ble_col = s_st.ble_connected ? th->conn_connected
                                          : th->conn_disconnected;
    display_fill_rect(w - 8, 3, 6, STATUS_BAR_H - 6, ble_col);

    if (s_st.passkey) {
        char pk[16];
        snprintf(pk, sizeof(pk), "PIN %06lu",
                 (unsigned long)s_st.passkey);
        int len = (int)strlen(pk);
        display_draw_string(w - 16 - len * 8, 3, pk, 1,
                            th->status_ind_fg, th->status_bar_bg, true);
    } else if (s_st.ble_status[0]) {
        int len = (int)strlen(s_st.ble_status);
        if (len > 16) len = 16;
        display_draw_string(w - 16 - len * 8, 3, s_st.ble_status, 1,
                            th->status_ind_fg, th->status_bar_bg, true);
    }
}

static void draw_keyboard(const theme_t *th)
{
    const kb_layout_t *l = kb_layout_active();
    int w = display_width();
    int h = display_height() - STATUS_BAR_H;
    int y0 = STATUS_BAR_H;
    int cell_w = w / l->cols;
    int cell_h = h / l->rows;
    if (cell_w < 8 || cell_h < 8) return;  /* not enough room */

    /* Per-glyph scale: pick the largest 8x8-multiple that fits. */
    int label_scale = (cell_h - 4) / 8;
    if (label_scale < 1) label_scale = 1;
    if (label_scale > 3) label_scale = 3;

    int sel_scale = label_scale;  /* selected cell same scale */

    for (int r = 0; r < l->rows; ++r) {
        for (int c = 0; c < l->cols; ++c) {
            const kb_key_t *k = kb_layout_key_at(l, r, c);
            if (!k) continue;
            int x = c * cell_w;
            int y = y0 + r * cell_h;
            bool selected = (r == s_st.sel_row && c == s_st.sel_col);

            uint16_t bg, fg;
            if (selected) {
                bg = th->nav_sel_bg;
                fg = th->nav_sel_fg;
            } else if (k->special != KB_KEY_SPECIAL_NONE) {
                bg = th->key_mod_bg;
                fg = th->key_mod_label;
            } else {
                bg = th->key_bg;
                fg = th->key_label;
            }

            display_fill_rect(x, y, cell_w, cell_h, bg);

            const char *lbl = ((s_st.mod_sticky | s_st.mod_oneshot)
                               & KB_MOD_LSHIFT)
                              ? k->label_shifted : k->label_unshifted;
            if (!lbl || !*lbl) continue;

            int lbl_len = (int)strlen(lbl);
            int gw = 8 * (selected ? sel_scale : label_scale);
            int tx = x + (cell_w - lbl_len * gw) / 2;
            int ty = y + (cell_h - gw) / 2;
            display_draw_string(tx, ty, lbl,
                                selected ? sel_scale : label_scale,
                                fg, bg, true);
        }
    }
}

static void draw_mouse_overlay(const theme_t *th)
{
    int w = display_width();
    int y0 = STATUS_BAR_H;
    int h = display_height() - STATUS_BAR_H;
    display_fill_rect(0, y0, w, h, th->win_bg);
    const char *msg = "MOUSE MODE";
    int len = (int)strlen(msg);
    int scale = 3;
    display_draw_string((w - len * 8 * scale) / 2,
                        y0 + (h - 8 * scale) / 2,
                        msg, scale, th->key_label, th->win_bg, true);
}

static void redraw_all(void)
{
    const theme_t *th = theme_active();
    display_clear(th->win_bg);
    draw_status_bar(th);
    if (s_st.mode == KB_MODE_MOUSE) {
        draw_mouse_overlay(th);
    } else {
        draw_keyboard(th);
    }
    display_flush();
}

/* ----- Task / queue ----- */

static void ui_task(void *arg)
{
    (void)arg;
    uint8_t dummy;
    /* Initial paint */
    redraw_all();
    while (1) {
        if (xQueueReceive(s_redraw_q, &dummy, portMAX_DELAY) == pdTRUE) {
            /* Coalesce: drain anything queued behind us. */
            while (xQueueReceive(s_redraw_q, &dummy, 0) == pdTRUE) { }
            redraw_all();
        }
    }
}

/* ----- Public API ----- */

void keyboard_ui_init(void)
{
    memset(&s_st, 0, sizeof(s_st));
    s_st.sel_row = 0;
    s_st.sel_col = 0;
    s_st.mode    = KB_MODE_KEYBOARD;
    snprintf(s_st.ble_status, sizeof(s_st.ble_status), "BLE: idle");

    nvs_load_strings();
    s_redraw_q = xQueueCreate(4, sizeof(uint8_t));
    ESP_LOGI(TAG, "ready; layout=%s theme=%s",
             kb_layout_active()->name, theme_active()->name);
}

void keyboard_ui_start_task(void)
{
    xTaskCreatePinnedToCore(ui_task, "kb_ui", 6144, NULL, 4, NULL, 0);
}

void keyboard_ui_request_redraw(void)
{
    if (!s_redraw_q) return;
    uint8_t tok = 1;
    xQueueSend(s_redraw_q, &tok, 0);  /* never blocks */
}

bool keyboard_ui_move(int drow, int dcol)
{
    const kb_layout_t *l = kb_layout_active();
    int nr = s_st.sel_row + drow;
    int nc = s_st.sel_col + dcol;
    if (nr < 0) nr = 0; if (nr >= l->rows) nr = l->rows - 1;
    if (nc < 0) nc = 0; if (nc >= l->cols) nc = l->cols - 1;
    if (nr == s_st.sel_row && nc == s_st.sel_col) return false;
    s_st.sel_row = nr;
    s_st.sel_col = nc;
    keyboard_ui_request_redraw();
    return true;
}

void keyboard_ui_toggle_mod(uint8_t mod)
{
    if (mod == 0) {
        s_st.mod_sticky = 0;
        s_st.mod_oneshot = 0;
    } else {
        s_st.mod_sticky ^= mod;
    }
    keyboard_ui_request_redraw();
}

void keyboard_ui_oneshot_mod(uint8_t mod)
{
    s_st.mod_oneshot |= mod;
    keyboard_ui_request_redraw();
}

void keyboard_ui_press_current(void)
{
    const kb_layout_t *l = kb_layout_active();
    const kb_key_t *k = kb_layout_key_at(l, s_st.sel_row, s_st.sel_col);
    if (!k || k->hid_usage == HID_USAGE_NONE) return;

    uint8_t mods = s_st.mod_sticky | s_st.mod_oneshot;
    ble_hid_send_key(mods, k->hid_usage);
    ble_hid_release_all();
    s_st.mod_oneshot = 0;
    keyboard_ui_request_redraw();
}

void keyboard_ui_set_ble_status(const char *text, bool connected)
{
    s_st.ble_connected = connected;
    if (text) {
        snprintf(s_st.ble_status, sizeof(s_st.ble_status), "%s", text);
    }
    /* Clear any stale pairing PIN once we transition to connected. */
    if (connected) s_st.passkey = 0;
    keyboard_ui_request_redraw();
}

void keyboard_ui_set_passkey(uint32_t passkey)
{
    s_st.passkey = passkey;
    keyboard_ui_request_redraw();
}

void keyboard_ui_set_mode(keyboard_ui_mode_t mode)
{
    if (s_st.mode == mode) return;
    s_st.mode = mode;
    keyboard_ui_request_redraw();
}

keyboard_ui_mode_t keyboard_ui_get_mode(void) { return s_st.mode; }

int keyboard_ui_selected_hid_usage(void)
{
    const kb_layout_t *l = kb_layout_active();
    const kb_key_t *k = kb_layout_key_at(l, s_st.sel_row, s_st.sel_col);
    return k ? k->hid_usage : HID_USAGE_NONE;
}

void keyboard_ui_cycle_layout(void)
{
    const kb_layout_t *cur = kb_layout_active();
    int n = kb_layout_count();
    int idx = 0;
    for (int i = 0; i < n; ++i) {
        if (kb_layout_by_index(i) == cur) { idx = i; break; }
    }
    const kb_layout_t *nxt = kb_layout_by_index((idx + 1) % n);
    kb_layout_set_active_by_name(nxt->name);
    /* Clamp selection to new grid bounds. */
    if (s_st.sel_row >= nxt->rows) s_st.sel_row = nxt->rows - 1;
    if (s_st.sel_col >= nxt->cols) s_st.sel_col = nxt->cols - 1;
    nvs_save_strings();
    keyboard_ui_request_redraw();
}

void keyboard_ui_cycle_theme(void)
{
    const theme_t *cur = theme_active();
    int n = theme_count();
    int idx = 0;
    for (int i = 0; i < n; ++i) {
        if (theme_by_index(i) == cur) { idx = i; break; }
    }
    const theme_t *nxt = theme_by_index((idx + 1) % n);
    theme_set_active_by_name(nxt->name);
    nvs_save_strings();
    keyboard_ui_request_redraw();
}
