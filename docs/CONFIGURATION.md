# Configuration

All build-time options live under
**Smart Keyboard** in `idf.py menuconfig`.

## Board selection

`SK_BOARD_*` -- exactly one. Selecting a board automatically
"selects" the capability symbols that gate optional components:

| Symbol                       | Meaning                                |
| ---------------------------- | -------------------------------------- |
| `BOARD_HAS_PSRAM`            | Component code may allocate from PSRAM |
| `BOARD_HAS_DISPLAY`          | Some panel driver is configured        |
| `BOARD_HAS_DISPLAY_AXS15231B`| Build the AXS15231B QSPI backend       |
| `BOARD_HAS_SPEAKER`          | I2S DAC wired -> compile in audio +    |
|                              | narrator                               |
| `BOARD_HAS_BATTERY`          | VBAT exposed -> battery indicator      |

The Audio / Narrator menu is **hidden** unless `BOARD_HAS_PSRAM`
and `BOARD_HAS_SPEAKER` are both set, because narration requires
both to be useful.

## I2C gamepad

| Option                            | Default | Range            |
| --------------------------------- | ------- | ---------------- |
| `SK_GAMEPAD_I2C_PORT`             | 0       | 0..1             |
| `SK_GAMEPAD_I2C_SDA_GPIO`         | 11      | any GPIO         |
| `SK_GAMEPAD_I2C_SCL_GPIO`         | 10      | any GPIO         |
| `SK_GAMEPAD_I2C_FREQ_HZ`          | 100000  | 50 kHz..400 kHz  |
| `SK_GAMEPAD_I2C_ADDR`             | 0x52    | 0x08..0x77       |
| `SK_GAMEPAD_POLL_MS`              | 16      | 5..100           |
| `SK_GAMEPAD_AXIS_DEADZONE`        | 40      | 0..127           |

## BLE HID

| Option                  | Default        | Purpose                  |
| ----------------------- | -------------- | ------------------------ |
| `SK_BLE_DEVICE_NAME`    | `SmartKeyboard`| advertised local name    |
| `SK_BLE_MANUFACTURER`   | `clackups`     | shown in some host UIs   |
| `SK_BLE_BOND_ON_PAIR`   | y              | persist bonds to NVS     |

## Keyboard layout

`SK_LAYOUT_DEFAULT` is the compile-time default. The runtime
choice is persisted to NVS by `keyboard_ui_cycle_layout()` (bound
to the **Select** button by default) so power-cycling preserves
the last selection.

## Theme

Same model as the layout: a Kconfig default that can be cycled
at runtime via **Start** (bound to `keyboard_ui_cycle_theme()`).
Five built-ins ship: `green_on_black` (default),
`darkgreen_on_black`, `white_on_black`, `black_on_white`,
`default`.

## Audio / narrator (conditional)

| Option                | Default | Purpose                         |
| --------------------- | ------- | ------------------------------- |
| `NARRATOR_ENABLE`     | y       | speak each selected key         |
| `AUDIO_I2S_PORT`      | 0       | I2S port (0 or 1)               |
| `AUDIO_I2S_BCLK_GPIO` | 5       |                                 |
| `AUDIO_I2S_LRCK_GPIO` | 6       |                                 |
| `AUDIO_I2S_DOUT_GPIO` | 7       |                                 |
| `AUDIO_VOLUME`        | 70      | 0..100, applied digitally       |

Sound output is **always** I2S -- there is no PWM / internal-DAC
fallback. On boards without `BOARD_HAS_SPEAKER` selected the
audio + narrator components compile to no-op stubs so calls from
`input_router` are free.

## NVS overrides

Runtime preferences live under the `sk_ui` NVS namespace:

| Key      | Type | Set by                          |
| -------- | ---- | ------------------------------- |
| `layout` | str  | `keyboard_ui_cycle_layout()`    |
| `theme`  | str  | `keyboard_ui_cycle_theme()`     |

To reset to the Kconfig defaults, erase the namespace from a
serial console or wipe NVS with `idf.py erase-flash`.
