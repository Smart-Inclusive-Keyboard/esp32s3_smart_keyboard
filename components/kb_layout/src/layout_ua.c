/*
 * Ukrainian (UA) keyboard layout -- standard JCUKEN (yi-tse-u-ka-en).
 *
 * Ported from the upstream Rust project's keymap_ua.toml. The
 * firmware sends the USB HID usage of the *physical* QWERTY key
 * position; a host configured for the Ukrainian layout then emits
 * the corresponding Cyrillic glyph. Because the embedded font8x8
 * only covers ASCII 0x20..0x7E (see AGENTS.md), the on-screen
 * labels are short Latin transliterations of the Cyrillic letters
 * rather than the glyphs themselves.
 *
 * So the narrator still speaks the *Ukrainian* letter (not its
 * transliteration), each character key carries explicit narrator
 * clip tokens (sound_unshifted / sound_shifted) naming the
 * components/narrator/wav/ua_*.wav clips keyed by Unicode
 * codepoint. Special keys (Space, Enter, arrows, F-keys, ...)
 * carry no token and fall back to the language-neutral clips via
 * their HID usage.
 *
 * Grid mirrors layout_us.c (6 rows x 17 cols): function-key row
 * with the Lng / Mnu UI keys, the main JCUKEN block, a navigation
 * cluster and an arrow cluster.
 */

#include "kb_layout.h"

/* Key with explicit narrator clip tokens. */
#define KS(lu, ls, u, su, ss) { (lu), (ls), (u), KB_KEY_SPECIAL_NONE, (su), (ss) }
/* Special key (accent color, no narrator token). */
#define KSP(lu, ls, u, sp)   { (lu), (ls), (u), (sp), NULL, NULL }
#define KNONE                { "",   "",   HID_USAGE_NONE, KB_KEY_SPECIAL_NONE, NULL, NULL }

#define ROWS 6
#define COLS 17

static const kb_key_t s_keys[ROWS * COLS] = {
    /* Row 0: Esc + F1..F12 (cols 0-12) + Lng + Mnu (13-14) + spacers. */
    KSP("Esc","Esc",HID_USAGE_ESCAPE,KB_KEY_SPECIAL_ESC),
    KSP("F1", "F1", HID_USAGE_F1, KB_KEY_SPECIAL_FN),
    KSP("F2", "F2", HID_USAGE_F2, KB_KEY_SPECIAL_FN),
    KSP("F3", "F3", HID_USAGE_F3, KB_KEY_SPECIAL_FN),
    KSP("F4", "F4", HID_USAGE_F4, KB_KEY_SPECIAL_FN),
    KSP("F5", "F5", HID_USAGE_F5, KB_KEY_SPECIAL_FN),
    KSP("F6", "F6", HID_USAGE_F6, KB_KEY_SPECIAL_FN),
    KSP("F7", "F7", HID_USAGE_F7, KB_KEY_SPECIAL_FN),
    KSP("F8", "F8", HID_USAGE_F8, KB_KEY_SPECIAL_FN),
    KSP("F9", "F9", HID_USAGE_F9, KB_KEY_SPECIAL_FN),
    KSP("F10","F10",HID_USAGE_F10,KB_KEY_SPECIAL_FN),
    KSP("F11","F11",HID_USAGE_F11,KB_KEY_SPECIAL_FN),
    KSP("F12","F12",HID_USAGE_F12,KB_KEY_SPECIAL_FN),
    KSP("Lng","Lng",HID_USAGE_NONE,KB_KEY_SPECIAL_LANG),
    KSP("Mnu","Mnu",HID_USAGE_NONE,KB_KEY_SPECIAL_MENU),
    KNONE, KNONE,

    /* Row 1: number row + Bksp (cols 0-13) + nav cluster (14-16).
     * Shifted glyphs follow the Ukrainian layout (e.g. Shift+3 is
     * the numero sign, shown here as the ASCII transliteration
     * "No"; Shift+grave is the hryvnia sign, not representable in
     * ASCII, so its label is blank but the narrator still speaks
     * it). */
    KS("'", "",  HID_USAGE_GRAVE, "ua_apostrophe", "ua_u20b4"),
    KS("1", "!", HID_USAGE_1,     "ua_1", "ua_u0021"),
    KS("2", "\"",HID_USAGE_2,     "ua_2", "ua_u0022"),
    KS("3", "No",HID_USAGE_3,     "ua_3", "ua_u2116"),
    KS("4", ";", HID_USAGE_4,     "ua_4", "ua_semicolon"),
    KS("5", "%", HID_USAGE_5,     "ua_5", "ua_u0025"),
    KS("6", ":", HID_USAGE_6,     "ua_6", "ua_u003a"),
    KS("7", "?", HID_USAGE_7,     "ua_7", "ua_u003f"),
    KS("8", "*", HID_USAGE_8,     "ua_8", "ua_u002a"),
    KS("9", "(", HID_USAGE_9,     "ua_9", "ua_u0028"),
    KS("0", ")", HID_USAGE_0,     "ua_0", "ua_u0029"),
    KS("-", "_", HID_USAGE_MINUS, "ua_minus", "ua_u005f"),
    KS("=", "+", HID_USAGE_EQUAL, "ua_equals", "ua_u002b"),
    KSP("Bksp","Bksp",HID_USAGE_BACKSPACE,KB_KEY_SPECIAL_BACKSPACE),
    KSP("Ins","Ins",HID_USAGE_INSERT,KB_KEY_SPECIAL_NAV),
    KSP("Hom","Hom",HID_USAGE_HOME,  KB_KEY_SPECIAL_NAV),
    KSP("PUp","PUp",HID_USAGE_PAGEUP,KB_KEY_SPECIAL_NAV),

    /* Row 2: Tab + top JCUKEN row + backslash (cols 0-13) + nav. */
    KSP("Tab","Tab",HID_USAGE_TAB,KB_KEY_SPECIAL_TAB),
    KS("j",  "", HID_USAGE_Q,        "ua_u0439", NULL),  /* j (i-kratke) */
    KS("c",  "", HID_USAGE_W,        "ua_u0446", NULL),  /* c (tse) */
    KS("u",  "", HID_USAGE_E,        "ua_u0443", NULL),  /* u */
    KS("k",  "", HID_USAGE_R,        "ua_u043a", NULL),  /* k (ka) */
    KS("e",  "", HID_USAGE_T,        "ua_u0435", NULL),  /* e */
    KS("n",  "", HID_USAGE_Y,        "ua_u043d", NULL),  /* n (en) */
    KS("g",  "", HID_USAGE_U,        "ua_u0433", NULL),  /* g (he) */
    KS("sh", "", HID_USAGE_I,        "ua_u0448", NULL),  /* sh (sha) */
    KS("sch","", HID_USAGE_O,        "ua_u0449", NULL),  /* sch (shcha) */
    KS("z",  "", HID_USAGE_P,        "ua_u0437", NULL),  /* z (ze) */
    KS("kh", "", HID_USAGE_LBRACKET, "ua_u0445", NULL),  /* kh (kha) */
    KS("yi", "", HID_USAGE_RBRACKET, "ua_u0457", NULL),  /* yi */
    KS("\\","|", HID_USAGE_BACKSLASH,"ua_backslash","ua_u007c"),
    KSP("Del","Del",HID_USAGE_DELETE,  KB_KEY_SPECIAL_NAV),
    KSP("End","End",HID_USAGE_END,     KB_KEY_SPECIAL_NAV),
    KSP("PDn","PDn",HID_USAGE_PAGEDOWN,KB_KEY_SPECIAL_NAV),

    /* Row 3: Caps + home JCUKEN row + Enter (cols 0-12), spacers. */
    KSP("Cap","Cap",HID_USAGE_CAPSLOCK,KB_KEY_SPECIAL_MOD),
    KS("f", "", HID_USAGE_A,         "ua_u0444", NULL),  /* f (ef) */
    KS("i", "", HID_USAGE_S,         "ua_u0456", NULL),  /* i */
    KS("v", "", HID_USAGE_D,         "ua_u0432", NULL),  /* v (ve) */
    KS("a", "", HID_USAGE_F,         "ua_u0430", NULL),  /* a */
    KS("p", "", HID_USAGE_G,         "ua_u043f", NULL),  /* p (pe) */
    KS("r", "", HID_USAGE_H,         "ua_u0440", NULL),  /* r (er) */
    KS("o", "", HID_USAGE_J,         "ua_u043e", NULL),  /* o */
    KS("l", "", HID_USAGE_K,         "ua_u043b", NULL),  /* l (el) */
    KS("d", "", HID_USAGE_L,         "ua_u0434", NULL),  /* d (de) */
    KS("zh","", HID_USAGE_SEMICOLON, "ua_u0436", NULL),  /* zh (zhe) */
    KS("ye","", HID_USAGE_APOSTROPHE,"ua_u0454", NULL),  /* ye */
    KSP("Ent","Ent",HID_USAGE_ENTER,KB_KEY_SPECIAL_ENTER),
    KNONE, KNONE, KNONE, KNONE,

    /* Row 4: LShift + bottom JCUKEN row + RShift (cols 0-11),
     * spacers, ArrowUp at col 15. */
    KSP("Sft","Sft",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KS("ya","", HID_USAGE_Z,     "ua_u044f", NULL),  /* ya */
    KS("ch","", HID_USAGE_X,     "ua_u0447", NULL),  /* ch (che) */
    KS("s", "", HID_USAGE_C,     "ua_u0441", NULL),  /* s (es) */
    KS("m", "", HID_USAGE_V,     "ua_u043c", NULL),  /* m (em) */
    KS("y", "", HID_USAGE_B,     "ua_u0438", NULL),  /* y (y) */
    KS("t", "", HID_USAGE_N,     "ua_u0442", NULL),  /* t (te) */
    KS("'", "", HID_USAGE_M,     "ua_u044c", NULL),  /* soft sign */
    KS("b", "", HID_USAGE_COMMA, "ua_u0431", NULL),  /* b (be) */
    KS("yu","", HID_USAGE_PERIOD,"ua_u044e", NULL),  /* yu */
    KS(".", ",", HID_USAGE_SLASH,"ua_period","ua_comma"),
    KSP("Sft","Sft",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KNONE, KNONE, KNONE,
    KSP("Up", "Up", HID_USAGE_UP,KB_KEY_SPECIAL_ARROW),
    KNONE,

    /* Row 5: Ctrl Win Alt + Space(x6) + AltGr RCtrl + arrow cluster.
     * Identical to the US layout: these keys are language-neutral. */
    KSP("Ctl","Ctl",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KSP("Win","Win",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KSP("Alt","Alt",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KSP("",     "",     HID_USAGE_SPACE,KB_KEY_SPECIAL_SPACE),
    KSP("",     "",     HID_USAGE_SPACE,KB_KEY_SPECIAL_SPACE),
    KSP("Space","Space",HID_USAGE_SPACE,KB_KEY_SPECIAL_SPACE),
    KSP("",     "",     HID_USAGE_SPACE,KB_KEY_SPECIAL_SPACE),
    KSP("",     "",     HID_USAGE_SPACE,KB_KEY_SPECIAL_SPACE),
    KSP("",     "",     HID_USAGE_SPACE,KB_KEY_SPECIAL_SPACE),
    KSP("AGr","AGr",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KSP("Ctl","Ctl",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KNONE, KNONE, KNONE,
    KSP("Lft","Lft",HID_USAGE_LEFT, KB_KEY_SPECIAL_ARROW),
    KSP("Dn", "Dn", HID_USAGE_DOWN, KB_KEY_SPECIAL_ARROW),
    KSP("Rgt","Rgt",HID_USAGE_RIGHT,KB_KEY_SPECIAL_ARROW),
};

const kb_layout_t kb_layout_ua = {
    .name = "UA",
    .rows = ROWS,
    .cols = COLS,
    .keys = s_keys,
};
