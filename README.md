# esp32-smart-keyboard

ESP-IDF firmware that recreates the
[clackups/smart-keyboard](https://github.com/clackups/smart-keyboard)
project on ESP32-family hardware: a virtual keyboard rendered on an
on-board screen, navigated with an external gamepad (receive-only
UART HID link),
that acts as a **HID keyboard + mouse** toward a host computer over
either **Bluetooth LE** (NimBLE) or **USB** (TinyUSB) -- the
transport is a build-time Kconfig choice, gated by what the chosen
ESP32 model actually supports. With PSRAM and a speaker, an
optional narrator speaks each letter as the user navigates.

## Status

Work in progress. The default supported board is the
**Waveshare ESP32-S3-Touch-LCD-3.5B** (320x480 IPS, AXS15231B
QSPI controller, ESP32-S3 + 16 MB flash + 8 MB octal PSRAM).
Additional boards plug in via Kconfig.

## Quick start

```sh
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py menuconfig          # SMART KEYBOARD ->  pick board, layout, theme
idf.py build flash monitor
```

Requires **ESP-IDF v5.3 or newer** (NimBLE host, `esp_lcd`
managed components).

## Architecture

```
main/                  app entry, splash, task wiring
components/
  board/               per-board pin / capability HAL (board.h)
  display/             AXS15231B framebuffer + draw API
  fonts/               embedded bitmap fonts (8x16, 16x32)
  theme/               color palette table (default: green on black)
  kb_layout/           keyboard layouts (US default; DE / FR / UA stubs)
  keyboard_ui/         virtual-keyboard state machine + rendering
  gamepad_uart/        receive-only UART HID-report parser
  input_router/        gamepad events -> UI nav + HID dispatch
  hid/                 transport-agnostic HID facade
  ble_hid/             NimBLE composite HID backend (keyboard + mouse)
  usb_hid/             TinyUSB composite HID backend (keyboard + mouse)
  audio/               I2S WAV player (sound output is I2S-only)
  narrator/            letter-name playback (conditional)
docs/
  HARDWARE.md          per-board pinouts + UART gamepad wiring
  CONFIGURATION.md     Kconfig options + NVS overrides
  MULTI_LANG.md        how to add a new keyboard layout
```

Each subsystem is its own IDF component with a small public header
under `include/`. Hardware variants are pure Kconfig choices --
no `#ifdef` ladders outside the `board/` component.

## Button mapping (default)

The action button is **combined** with the on-screen key under the
selection cursor. Adopted from the upstream Rust project; remappable
via `input_router`.

| Button         | Action                                              |
| -------------- | --------------------------------------------------- |
| D-pad          | Move selection (hold = repeat)                      |
| A              | Send the selected key (lowercase / unshifted)       |
| X              | Send Shift + selected key                           |
| B              | Send Ctrl  + selected key                           |
| Y              | Send Alt   + selected key                           |
| Left shoulder  | Backspace                                           |
| Right shoulder | Toggle Shift sticky / Mouse-mode (long press)       |
| Start          | Open settings menu (layout / theme / narrator)      |
| Select         | Cycle keyboard layout                               |

## License

MIT. Audio assets (when present under `components/narrator/wav/`)
and the keyboard-layout tables are ported from
[clackups/smart-keyboard](https://github.com/clackups/smart-keyboard),
which is also MIT-licensed.
