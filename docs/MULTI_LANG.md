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

4. **Add the file to the component**: append it to
   `components/kb_layout/CMakeLists.txt`.

5. **Register the layout** in
   `components/kb_layout/src/kb_layout.c` by adding it to
   `s_all[]`.

6. **Surface it in Kconfig** by adding a new
   `config SK_LAYOUT_<LANG>` entry to `Kconfig.projbuild` and
   teaching `kconfig_default_name()` about it.

## Caveats

The embedded font (`components/fonts/`) is the public-domain
font8x8 covering ASCII 0x20..0x7E. Non-ASCII glyphs (umlauts,
accents, Cyrillic) currently render as a `?` fallback. The
upstream Rust project's `keymap_de.toml`, `keymap_fr.toml`, and
`keymap_ua.toml` use such glyphs, so the German / French /
Ukrainian layouts ship as 1x1 placeholders until a UTF-8 font
is in place. The plumbing (NVS persistence, status bar label,
runtime cycling) is wired up and ready.
