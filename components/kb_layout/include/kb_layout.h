#pragma once

/*
 * Keyboard layout data.
 *
 * A layout is a static table of `kb_key_t` entries arranged in a
 * rectangular grid (rows x cols). The grid drives both the
 * on-screen rendering (keyboard_ui) and the BLE HID dispatch
 * (input_router): given a row/col selection and a modifier mask,
 * input_router emits modifier + HID-usage to ble_hid.
 *
 * Ported one-to-one from the upstream Rust project's keymap_*.toml
 * so behaviour (incl. shifted glyphs) matches.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Display label is an ASCII string (1-3 chars) for portability
 * with the embedded 8x8 font. Non-ASCII layouts (UA, FR
 * dead-keys, etc.) use ASCII transliterations until a full
 * UTF-8 capable font lands. */
typedef struct {
    const char *label_unshifted;   /* what we draw idle    */
    const char *label_shifted;     /* what we draw w/Shift */
    uint8_t     hid_usage;         /* USB HID usage ID     */
    /* If non-zero, this key is a "special" (Backspace, Enter,
     * Space, Tab, Escape) and should be rendered with an
     * accent color. */
    uint8_t     special;
} kb_key_t;

typedef struct {
    const char     *name;          /* "US", "DE", "FR", "UA"  */
    int             rows;
    int             cols;
    const kb_key_t *keys;           /* rows * cols entries     */
} kb_layout_t;

/* HID Usage IDs for the most common keys, mirroring USB HID
 * Keyboard/Keypad Page (0x07). Full table:
 * https://usb.org/sites/default/files/hut1_22.pdf */
#define HID_USAGE_NONE       0x00
#define HID_USAGE_A          0x04
#define HID_USAGE_B          0x05
#define HID_USAGE_C          0x06
#define HID_USAGE_D          0x07
#define HID_USAGE_E          0x08
#define HID_USAGE_F          0x09
#define HID_USAGE_G          0x0A
#define HID_USAGE_H          0x0B
#define HID_USAGE_I          0x0C
#define HID_USAGE_J          0x0D
#define HID_USAGE_K          0x0E
#define HID_USAGE_L          0x0F
#define HID_USAGE_M          0x10
#define HID_USAGE_N          0x11
#define HID_USAGE_O          0x12
#define HID_USAGE_P          0x13
#define HID_USAGE_Q          0x14
#define HID_USAGE_R          0x15
#define HID_USAGE_S          0x16
#define HID_USAGE_T          0x17
#define HID_USAGE_U          0x18
#define HID_USAGE_V          0x19
#define HID_USAGE_W          0x1A
#define HID_USAGE_X          0x1B
#define HID_USAGE_Y          0x1C
#define HID_USAGE_Z          0x1D
#define HID_USAGE_1          0x1E
#define HID_USAGE_2          0x1F
#define HID_USAGE_3          0x20
#define HID_USAGE_4          0x21
#define HID_USAGE_5          0x22
#define HID_USAGE_6          0x23
#define HID_USAGE_7          0x24
#define HID_USAGE_8          0x25
#define HID_USAGE_9          0x26
#define HID_USAGE_0          0x27
#define HID_USAGE_ENTER      0x28
#define HID_USAGE_ESCAPE     0x29
#define HID_USAGE_BACKSPACE  0x2A
#define HID_USAGE_TAB        0x2B
#define HID_USAGE_SPACE      0x2C
#define HID_USAGE_MINUS      0x2D
#define HID_USAGE_EQUAL      0x2E
#define HID_USAGE_LBRACKET   0x2F
#define HID_USAGE_RBRACKET   0x30
#define HID_USAGE_BACKSLASH  0x31
#define HID_USAGE_SEMICOLON  0x33
#define HID_USAGE_APOSTROPHE 0x34
#define HID_USAGE_GRAVE      0x35
#define HID_USAGE_COMMA      0x36
#define HID_USAGE_PERIOD     0x37
#define HID_USAGE_SLASH      0x38

#define KB_KEY_SPECIAL_NONE      0
#define KB_KEY_SPECIAL_BACKSPACE 1
#define KB_KEY_SPECIAL_ENTER     2
#define KB_KEY_SPECIAL_SPACE     3
#define KB_KEY_SPECIAL_TAB       4
#define KB_KEY_SPECIAL_ESC       5

/* External tables defined in src/layout_<lang>.c. */
extern const kb_layout_t kb_layout_us;
extern const kb_layout_t kb_layout_de;
extern const kb_layout_t kb_layout_fr;
extern const kb_layout_t kb_layout_ua;

/* Active layout (defaults via Kconfig, runtime override via
 * kb_layout_set_active_by_name() persisted in NVS by the UI). */
const kb_layout_t *kb_layout_active(void);
bool               kb_layout_set_active_by_name(const char *name);

/* Enumerate built-in layouts. */
int                kb_layout_count(void);
const kb_layout_t *kb_layout_by_index(int i);
const kb_layout_t *kb_layout_by_name(const char *name);

/* Convenience: bounds-checked grid lookup. Returns NULL on
 * out-of-range. */
static inline const kb_key_t *
kb_layout_key_at(const kb_layout_t *l, int row, int col)
{
    if (!l || row < 0 || row >= l->rows || col < 0 || col >= l->cols) {
        return NULL;
    }
    return &l->keys[row * l->cols + col];
}

#ifdef __cplusplus
}
#endif
