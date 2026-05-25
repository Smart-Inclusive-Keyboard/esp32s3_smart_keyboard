/*
 * Virtual-keyboard renderer and selection state machine.
 *
 * Layout
 * ------
 *   +----------------------------------------------------------+
 *   | status bar : layout | mods | HID | mode                  |
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
#include "hid.h"

static const char *TAG = "kb_ui";

#define STATUS_BAR_H 14
#define NVS_NAMESPACE "sk_ui"

typedef struct {
    int  sel_row;
    int  sel_col;
    uint8_t  mod_sticky;    /* persistent across keypresses     */
    uint8_t  mod_oneshot;   /* cleared after the next keypress  */
    bool     hid_connected;
    uint32_t passkey;       /* 0 = none, else 6-digit value     */
    keyboard_ui_mode_t mode;
    char     hid_status[32];
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
        { "Sh", HID_MOD_LSHIFT },
        { "Ct", HID_MOD_LCTRL  },
        { "Al", HID_MOD_LALT   },
    };
    for (size_t i = 0; i < sizeof(mods) / sizeof(mods[0]); ++i) {
        bool on = (s_st.mod_sticky | s_st.mod_oneshot) & mods[i].m;
        uint16_t bg = on ? th->status_ind_active_bg : th->status_bar_bg;
        uint16_t fg = on ? th->status_ind_active_fg : th->status_ind_fg;
        display_fill_rect(x - 2, 1, 2 * 8 + 4, STATUS_BAR_H - 2, bg);
        display_draw_string(x, 3, mods[i].t, 1, fg, bg, false);
        x += 2 * 8 + 8;
    }

    /* Right side: HID link indicator + status text. */
    uint16_t hid_col = s_st.hid_connected ? th->conn_connected
                                          : th->conn_disconnected;
    display_fill_rect(w - 8, 3, 6, STATUS_BAR_H - 6, hid_col);

    if (s_st.passkey) {
        char pk[16];
        snprintf(pk, sizeof(pk), "PIN %06lu",
                 (unsigned long)s_st.passkey);
        int len = (int)strlen(pk);
        display_draw_string(w - 16 - len * 8, 3, pk, 1,
                            th->status_ind_fg, th->status_bar_bg, true);
    } else if (s_st.hid_status[0]) {
        int len = (int)strlen(s_st.hid_status);
        if (len > 16) len = 16;
        display_draw_string(w - 16 - len * 8, 3, s_st.hid_status, 1,
                            th->status_ind_fg, th->status_bar_bg, true);
    }
}

/* ----- Icon glyphs ----- */
/*
 * Symbolic icons drawn directly via display_set_pixel so they
 * don't need extra entries in the ASCII font table. Each helper
 * fills the supplied (x, y, w, h) bbox with `bg`, then paints the
 * symbol in `fg`. The bbox is the inner padded area of the key
 * cell (a few pixels of margin removed by the caller).
 */

static void icon_fill_triangle(int x, int y, int w, int h,
                               uint16_t fg, char dir)
{
    /* Solid isoceles triangle pointing in the given direction
     * ('U' up, 'D' down, 'L' left, 'R' right), inscribed in the
     * (w x h) bbox. */
    if (dir == 'U' || dir == 'D') {
        for (int row = 0; row < h; ++row) {
            int frac = (dir == 'U') ? row : (h - 1 - row);
            int half = (frac * (w / 2)) / (h > 1 ? h - 1 : 1);
            int x0 = x + w / 2 - half;
            int x1 = x + w / 2 + half;
            for (int px = x0; px <= x1; ++px) {
                display_set_pixel(px, y + row, fg);
            }
        }
    } else {
        for (int col = 0; col < w; ++col) {
            int frac = (dir == 'L') ? col : (w - 1 - col);
            int half = (frac * (h / 2)) / (w > 1 ? w - 1 : 1);
            int y0 = y + h / 2 - half;
            int y1 = y + h / 2 + half;
            for (int py = y0; py <= y1; ++py) {
                display_set_pixel(x + col, py, fg);
            }
        }
    }
}

static void icon_space(int x, int y, int w, int h, uint16_t fg)
{
    /* Horizontal bar with short downward tabs at both ends -- the
     * conventional space-bar glyph used by IDEs / editors. */
    int bar_h = h / 6; if (bar_h < 1) bar_h = 1;
    int tab_h = h / 4; if (tab_h < 2) tab_h = 2;
    int margin = w / 8;
    int x0 = x + margin;
    int x1 = x + w - margin - 1;
    int by = y + h - 1 - bar_h - 1;
    /* horizontal bar */
    display_fill_rect(x0, by, x1 - x0 + 1, bar_h, fg);
    /* left + right tabs */
    display_fill_rect(x0, by - tab_h + 1, bar_h, tab_h, fg);
    display_fill_rect(x1 - bar_h + 1, by - tab_h + 1, bar_h, tab_h, fg);
}

static void icon_enter(int x, int y, int w, int h, uint16_t fg)
{
    /* Return-arrow: a horizontal stroke at mid-height running from
     * the right edge leftward, with a vertical drop to the right
     * end, plus an arrowhead at the left tip. */
    int stroke = h / 8; if (stroke < 1) stroke = 1;
    int margin_x = w / 6;
    int margin_y = h / 4;
    int top = y + margin_y;
    int mid_y = y + h - margin_y - 1;
    int left_x = x + margin_x;
    int right_x = x + w - margin_x - 1;
    /* horizontal */
    display_fill_rect(left_x, mid_y - stroke + 1,
                      right_x - left_x + 1, stroke, fg);
    /* vertical drop on the right */
    display_fill_rect(right_x - stroke + 1, top,
                      stroke, mid_y - top + 1, fg);
    /* arrowhead at the left tip */
    int head = h / 4; if (head < 2) head = 2;
    icon_fill_triangle(left_x - head + 1, mid_y - head + 1,
                       head, head * 2 - 1, fg, 'L');
}

/* True if this key should be drawn as an icon, not text. */
static bool key_uses_icon(const kb_key_t *k)
{
    if (!k) return false;
    if (k->special == KB_KEY_SPECIAL_SPACE) return true;
    if (k->special == KB_KEY_SPECIAL_ENTER) return true;
    switch (k->hid_usage) {
    case HID_USAGE_UP: case HID_USAGE_DOWN:
    case HID_USAGE_LEFT: case HID_USAGE_RIGHT:
        return true;
    default: return false;
    }
}

static void draw_key_icon(const kb_key_t *k, int x, int y,
                          int cell_w, int cell_h, uint16_t fg)
{
    int pad = 3;
    int ix = x + pad, iy = y + pad;
    int iw = cell_w - 2 * pad, ih = cell_h - 2 * pad;
    if (iw < 4 || ih < 4) return;
    if (k->special == KB_KEY_SPECIAL_SPACE) {
        icon_space(ix, iy, iw, ih, fg);
        return;
    }
    if (k->special == KB_KEY_SPECIAL_ENTER) {
        icon_enter(ix, iy, iw, ih, fg);
        return;
    }
    char dir = 0;
    switch (k->hid_usage) {
    case HID_USAGE_UP:    dir = 'U'; break;
    case HID_USAGE_DOWN:  dir = 'D'; break;
    case HID_USAGE_LEFT:  dir = 'L'; break;
    case HID_USAGE_RIGHT: dir = 'R'; break;
    default: return;
    }
    /* Triangles look balanced when slightly smaller than the
     * padded cell, especially in the constrained-axis direction. */
    int tw = iw * 2 / 3;
    int th = ih * 2 / 3;
    if (tw < 4) tw = iw;
    if (th < 4) th = ih;
    icon_fill_triangle(ix + (iw - tw) / 2, iy + (ih - th) / 2,
                       tw, th, fg, dir);
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

    /* Largest 8x8-multiple that fits vertically -- upper bound on
     * each cell's text scale. */
    int max_scale = (cell_h - 4) / 8;
    if (max_scale < 1) max_scale = 1;
    if (max_scale > 3) max_scale = 3;

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

            if (key_uses_icon(k)) {
                draw_key_icon(k, x, y, cell_w, cell_h, fg);
                continue;
            }

            const char *lbl = ((s_st.mod_sticky | s_st.mod_oneshot)
                               & HID_MOD_LSHIFT)
                              ? k->label_shifted : k->label_unshifted;
            if (!lbl || !*lbl) continue;

            /* Shrink multi-letter labels so they fit the cell:
             * pick the largest 8x8 scale (capped by max_scale) that
             * leaves a small horizontal margin. Single-glyph keys
             * therefore stay big, while "Caps" / "Bksp" / "PgUp"
             * drop down to a smaller, fully readable size. */
            int lbl_len = (int)strlen(lbl);
            int fit_scale = (cell_w - 2) / (lbl_len * 8);
            if (fit_scale < 1) fit_scale = 1;
            int scale = fit_scale < max_scale ? fit_scale : max_scale;
            int gw = 8 * scale;
            int tx = x + (cell_w - lbl_len * gw) / 2;
            int ty = y + (cell_h - gw) / 2;
            display_draw_string(tx, ty, lbl, scale, fg, bg, true);
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
    snprintf(s_st.hid_status, sizeof(s_st.hid_status), "HID: idle");

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

void keyboard_ui_redraw_now(void)
{
    /* Synchronous full redraw from the calling context. Used by
     * app_main() to paint the keyboard once before HID / gamepad
     * init begins, so the user sees a visible UI even while those
     * (potentially long-running) subsystems come up. */
    redraw_all();
}

bool keyboard_ui_move(int drow, int dcol)
{
    const kb_layout_t *l = kb_layout_active();
    int nr = s_st.sel_row + drow;
    int nc = s_st.sel_col + dcol;
    if (nr < 0) nr = 0;
    if (nr >= l->rows) nr = l->rows - 1;
    if (nc < 0) nc = 0;
    if (nc >= l->cols) nc = l->cols - 1;
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
    hid_send_key(mods, k->hid_usage);
    hid_release_all();
    s_st.mod_oneshot = 0;
    keyboard_ui_request_redraw();
}

void keyboard_ui_set_hid_status(const char *text, bool connected)
{
    s_st.hid_connected = connected;
    if (text) {
        snprintf(s_st.hid_status, sizeof(s_st.hid_status), "%s", text);
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
