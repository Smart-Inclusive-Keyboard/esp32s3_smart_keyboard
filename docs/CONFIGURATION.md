# Configuration

All build-time options live under
**SMART KEYBOARD** in `idf.py menuconfig`.

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

## Gamepad transport

`SK_GAMEPAD_TRANSPORT_I2C` (default) or `SK_GAMEPAD_TRANSPORT_SPI`
picks the bus the external gamepad is wired to. The device is
always the bus master / host; the gamepad is always the slave.
Only the selected transport's driver is compiled in, but the
4-byte report format and `gamepad_event_t` queue are identical
across both, so `input_router` is transport-agnostic.

Shared options (apply regardless of transport):

| Option                            | Default | Range            |
| --------------------------------- | ------- | ---------------- |
| `SK_GAMEPAD_POLL_MS`              | 16      | 5..100           |
| `SK_GAMEPAD_AXIS_DEADZONE`        | 40      | 0..127           |

### I2C gamepad (`SK_GAMEPAD_TRANSPORT_I2C`)

| Option                            | Default | Range            |
| --------------------------------- | ------- | ---------------- |
| `SK_GAMEPAD_I2C_PORT`             | 0       | 0..1             |
| `SK_GAMEPAD_I2C_SDA_GPIO`         | 11      | any GPIO         |
| `SK_GAMEPAD_I2C_SCL_GPIO`         | 10      | any GPIO         |
| `SK_GAMEPAD_I2C_FREQ_HZ`          | 100000  | 50 kHz..400 kHz  |
| `SK_GAMEPAD_I2C_ADDR`             | 0x52    | 0x08..0x77       |

### SPI gamepad (`SK_GAMEPAD_TRANSPORT_SPI`)

The device drives SCLK / MOSI / CS toward the gamepad slave and
clocks the 4-byte report back on MISO. One full-duplex 4-byte
transaction is issued per poll.

| Option                            | Default | Range                |
| --------------------------------- | ------- | -------------------- |
| `SK_GAMEPAD_SPI_HOST`             | 2       | 1..3 (SPI2 / SPI3)   |
| `SK_GAMEPAD_SPI_SCLK_GPIO`        | 12      | any GPIO             |
| `SK_GAMEPAD_SPI_MOSI_GPIO`        | 11      | any GPIO             |
| `SK_GAMEPAD_SPI_MISO_GPIO`        | 13      | any GPIO             |
| `SK_GAMEPAD_SPI_CS_GPIO`          | 10      | any GPIO             |
| `SK_GAMEPAD_SPI_FREQ_HZ`          | 1000000 | 100 kHz..10 MHz      |
| `SK_GAMEPAD_SPI_MODE`             | 0       | 0..3 (CPOL/CPHA)     |

On boards that already use SPI2 for the display (e.g. the
Waveshare 3.5B), prefer host 3 for the gamepad.

## HID transport

The firmware acts as a HID peripheral over either Bluetooth LE or
USB. The transport is selected at build time via
`SK_HID_TRANSPORT`; each option is only offered when the target
SoC actually has the corresponding controller
(`SOC_BLE_SUPPORTED` / `SOC_USB_OTG_SUPPORTED`).

| Option                  | Targets        | Backend                  |
| ----------------------- | -------------- | ------------------------ |
| `SK_HID_TRANSPORT_BLE`  | classic ESP32, S3 | NimBLE GATT HID       |
| `SK_HID_TRANSPORT_USB`  | ESP32-S2, S3, P4  | TinyUSB composite HID |

The classic ESP32 only exposes the BLE option; ESP32-S2 only the
USB option; ESP32-S3 lets you pick either.

## BLE HID

Only visible when `SK_HID_TRANSPORT_BLE` is selected.

| Option                  | Default        | Purpose                  |
| ----------------------- | -------------- | ------------------------ |
| `SK_BLE_DEVICE_NAME`    | `SmartKeyboard`| advertised local name    |
| `SK_BLE_MANUFACTURER`   | `clackups`     | shown in some host UIs   |
| `SK_BLE_BOND_ON_PAIR`   | y              | persist bonds to NVS     |

## USB HID

Only visible when `SK_HID_TRANSPORT_USB` is selected.

| Option                  | Default        | Purpose                  |
| ----------------------- | -------------- | ------------------------ |
| `SK_USB_MANUFACTURER`   | `clackups`     | USB iManufacturer string |
| `SK_USB_PRODUCT`        | `SmartKeyboard`| USB iProduct string      |
| `SK_USB_SERIAL`         | `000001`       | USB iSerial string       |

When building a pure-USB image you can also disable the Bluetooth
stack in `menuconfig > Component config > Bluetooth` to reclaim
the ~60 KB it occupies in flash + ~30 KB in RAM.

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
