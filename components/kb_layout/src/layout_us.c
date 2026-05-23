/*
 * US English keyboard layout.
 *
 * Ported from the upstream Rust project's keymap_us.toml.
 * The grid mirrors a standard 60 % keyboard rearranged into 5
 * rows of 14 keys so the whole layout fits in a single screen
 * on the 640x172 Waveshare panel.
 *
 *   row 0:  ` 1 2 3 4 5 6 7 8 9 0 - = BS
 *   row 1:  Tab q w e r t y u i o p [ ] \
 *   row 2:  a s d f g h j k l ; ' Enter . .
 *   row 3:  Shift z x c v b n m , . / Up . .
 *   row 4:  Ctrl Alt Space . . . . . . . . Left Down Right
 *
 * (Empty cells render as no-op spacers in keyboard_ui.)
 */

#include "kb_layout.h"

#define K(lu, ls, u)         { (lu), (ls), (u), KB_KEY_SPECIAL_NONE }
#define KSP(lu, ls, u, sp)   { (lu), (ls), (u), (sp)               }
#define KNONE                { "",   "",   HID_USAGE_NONE, KB_KEY_SPECIAL_NONE }

#define ROWS 5
#define COLS 14

static const kb_key_t s_keys[ROWS * COLS] = {
    /* Row 0: number row + backspace */
    K("`","~",HID_USAGE_GRAVE), K("1","!",HID_USAGE_1), K("2","@",HID_USAGE_2),
    K("3","#",HID_USAGE_3), K("4","$",HID_USAGE_4), K("5","%",HID_USAGE_5),
    K("6","^",HID_USAGE_6), K("7","&",HID_USAGE_7), K("8","*",HID_USAGE_8),
    K("9","(",HID_USAGE_9), K("0",")",HID_USAGE_0), K("-","_",HID_USAGE_MINUS),
    K("=","+",HID_USAGE_EQUAL),
    KSP("BS","BS",HID_USAGE_BACKSPACE,KB_KEY_SPECIAL_BACKSPACE),

    /* Row 1: tab + top alpha + brackets + backslash */
    KSP("Tab","Tab",HID_USAGE_TAB,KB_KEY_SPECIAL_TAB),
    K("q","Q",HID_USAGE_Q), K("w","W",HID_USAGE_W), K("e","E",HID_USAGE_E),
    K("r","R",HID_USAGE_R), K("t","T",HID_USAGE_T), K("y","Y",HID_USAGE_Y),
    K("u","U",HID_USAGE_U), K("i","I",HID_USAGE_I), K("o","O",HID_USAGE_O),
    K("p","P",HID_USAGE_P),
    K("[","{",HID_USAGE_LBRACKET), K("]","}",HID_USAGE_RBRACKET),
    K("\\","|",HID_USAGE_BACKSLASH),

    /* Row 2: home row + Enter (Enter spans visually but emits one HID code) */
    K("a","A",HID_USAGE_A), K("s","S",HID_USAGE_S), K("d","D",HID_USAGE_D),
    K("f","F",HID_USAGE_F), K("g","G",HID_USAGE_G), K("h","H",HID_USAGE_H),
    K("j","J",HID_USAGE_J), K("k","K",HID_USAGE_K), K("l","L",HID_USAGE_L),
    K(";",":",HID_USAGE_SEMICOLON), K("'","\"",HID_USAGE_APOSTROPHE),
    KSP("Ent","Ent",HID_USAGE_ENTER,KB_KEY_SPECIAL_ENTER),
    KNONE, KNONE,

    /* Row 3: bottom alpha + punctuation + up-arrow filler */
    K("z","Z",HID_USAGE_Z), K("x","X",HID_USAGE_X), K("c","C",HID_USAGE_C),
    K("v","V",HID_USAGE_V), K("b","B",HID_USAGE_B), K("n","N",HID_USAGE_N),
    K("m","M",HID_USAGE_M),
    K(",","<",HID_USAGE_COMMA), K(".",">",HID_USAGE_PERIOD),
    K("/","?",HID_USAGE_SLASH),
    KNONE, KNONE, KNONE, KNONE,

    /* Row 4: space row (Space spans visually but emits one HID code) */
    KNONE, KNONE,
    KSP("Space","Space",HID_USAGE_SPACE,KB_KEY_SPECIAL_SPACE),
    KNONE, KNONE, KNONE, KNONE, KNONE, KNONE, KNONE, KNONE,
    KNONE, KNONE, KNONE,
};

const kb_layout_t kb_layout_us = {
    .name = "US",
    .rows = ROWS,
    .cols = COLS,
    .keys = s_keys,
};
