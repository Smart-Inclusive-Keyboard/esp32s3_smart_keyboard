# Adding a keyboard layout

The keyboard layout is a static `kb_layout_t` describing a
`rows x cols` grid of `kb_key_t` entries. Layouts live in
`components/kb_layout/src/layout_<lang>.c` and are registered in
`components/kb_layout/src/kb_layout.c` so that
`kb_layout_cycle_layout()` walks them in order.

## Steps

1. **Pick a short identifier** (`"US"`, `"DE"`, `"FR"`, `"UA"`,
   ...). It is used both as the menu label in the status bar
   and as the value persisted in NVS.

2. **Copy `layout_us.c`** to `layout_<lang>.c`.

3. **Fill in the grid.** Each entry is

   ```c
   K("a", "A", HID_USAGE_A)
   ```

   - first arg: unshifted ASCII label drawn on the cell,
   - second arg: shifted ASCII label,
   - third arg: USB HID usage ID (see `kb_layout.h` for the
     `HID_USAGE_*` constants).

   Use `KSP(...)` for special keys (Enter, Space, Backspace,
   Tab, Escape) so the renderer colours them with the modifier
   palette; use `KNONE` for empty grid slots.

   Two special keys are reserved for UI actions and carry no HID
   usage: `KB_KEY_SPECIAL_LANG` (the on-screen **Lng** key, which
   rotates through the enabled languages) and
   `KB_KEY_SPECIAL_MENU` (the **Mnu** key, which opens the
   gamepad-navigated settings menu). The reference layouts place
   them on the function-key row, just right of F12.

4. **Add the file to the component**: append it to
   `components/kb_layout/CMakeLists.txt`.

5. **Register the layout** in
   `components/kb_layout/src/kb_layout.c` by adding it to
   `s_all[]`.

6. **Surface it in Kconfig** by adding a new
   `config SK_LAYOUT_<LANG>` entry to `Kconfig.projbuild` and
   teaching `kconfig_default_name()` about it.

## Narrating non-Latin layouts

The on-screen labels must be 7-bit ASCII (see the caveat below),
so a non-Latin layout draws Latin transliterations. To make the
narrator still speak the real character, give each key explicit
narrator clip tokens via the `sound_unshifted` / `sound_shifted`
fields of `kb_key_t` (the `KS(...)` macro in `layout_ua.c` is the
reference). A token names a WAV file under
`components/narrator/wav/<token>.wav` and must be registered in
the `S_TOKEN_CLIPS[]` table in
`components/narrator/src/narrator.c`. The Ukrainian layout keys
its clips by Unicode codepoint (e.g. `ua_u0430` for the letter
`a`). When a key carries no token the narrator falls back to the
language-neutral clip resolved from its HID usage.

## Caveats

The embedded font (`components/fonts/`) is the public-domain
font8x8 covering ASCII 0x20..0x7E. Non-ASCII glyphs (umlauts,
accents, Cyrillic) cannot be drawn directly, so layouts use
short Latin transliterations for their on-screen labels (the
Ukrainian layout, for example, sends the HID usage of the
physical QWERTY position so a host set to the Ukrainian layout
emits the Cyrillic glyph, while the narrator speaks the real
letter via per-key sound tokens). The German (`DE`) and French
(`FR`) layouts still ship as 1x1 placeholders until they are
filled in the same way.
