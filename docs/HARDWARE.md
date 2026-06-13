# Hardware

## Supported boards

### Waveshare ESP32-S3-Touch-LCD-3.5B (default)

| Function           | GPIO       | Notes                              |
| ------------------ | ---------- | ---------------------------------- |
| LCD QSPI CS        | 12         | AXS15231B QSPI panel               |
| LCD QSPI SCK       | 5          |                                    |
| LCD QSPI D0..D3    | 1, 2, 3, 4 |                                    |
| LCD RST            | -1         | wired to TCA9554 I/O expander pin 1, not a GPIO |
| LCD TE             | -1         | not wired                          |
| LCD BL             | 6          | active HIGH, LEDC PWM              |
| Gamepad UART RX    | 11         | receive-only, 8-N-1, 115200 baud   |
| I2S MCLK / BCLK / LRCK / DOUT | 44 / 13 / 15 / 16 | on-board ES8311 codec + speaker |
| Touch I2C SDA / SCL | 8 / 7 | AXS15231B-family capacitive touch, addr 0x3B, shares the codec I2C bus |

- 16 MB flash, 8 MB octal PSRAM, ESP32-S3 (N16R8).
- Native panel resolution is 320x480 portrait; the firmware
  presents it as 480x320 landscape and rotates 90 deg CW in
  software in the display flush path.
- On-board I2S speaker via ES8311 codec. The board selects
  `BOARD_HAS_SPEAKER` so the narrator is compiled in by
  default; the I2S pins above are hard-coded in
  `components/board/src/board_waveshare_esp32s3_touch_lcd_35b.c`.
- The gamepad is wired to a single receive-only UART RX pin,
  GPIO 11 by default. That pin is free on this board; the
  companion gamepad board drives it TX-only at 115200 baud
  8-N-1. Change the pin / port / baud via the
  `CONFIG_SK_GAMEPAD_UART_*` options in menuconfig.
- The capacitive touchscreen overlay is driven by the same
  AXS15231B-family "magic packet" I2C protocol; on the 3.5B it
  sits on the codec I2C bus (SDA = 8, SCL = 7, addr 0x3B) and
  reports coordinates in the panel's native 320 x 480 portrait
  space. The firmware maps them onto the logical 480 x 320
  landscape framebuffer via
  `mirror_x = mirror_y = swap_xy = true` in `board_t::touch`.

### Generic ESP32-S3 / Generic ESP32

Placeholder boards. Edit
`components/board/src/board_generic_s3.c` (or
`board_generic_esp32.c`) to fill in the pin map for your custom
wiring, then select the matching board in menuconfig.

## Gamepad wiring

The external gamepad is a separate board that streams its HID
report into this firmware over a one-way (receive-only) UART
link. Wire the gamepad's TX line to the configured RX GPIO
(`CONFIG_SK_GAMEPAD_UART_RX_GPIO`, GPIO 11 by default) and share
a common ground. The link is 8-N-1 at
`CONFIG_SK_GAMEPAD_UART_BAUD` baud (115200 by default); this
firmware never transmits.

The report is a fixed 6-byte frame, identical to the HID report
emitted by the companion gamepad firmware
([clackups/esp32s3_dual_foc_gp](https://github.com/clackups/esp32s3_dual_foc_gp)):

```
byte 0:  buttons 0..7  (bit i set = HID button i+1 pressed)
byte 1:  buttons 8..9  (bits 0..1) + 6 bits padding
byte 2:  X axis, signed 16-bit LE low byte
byte 3:  X axis high byte (-32767..32767, 0 = centred, positive = right)
byte 4:  Y axis, signed 16-bit LE low byte
byte 5:  Y axis high byte (-32767..32767, 0 = centred, positive = down)
```

The firmware refers to gamepad buttons by their HID number
(1..10) rather than by vendor letter names (A/B/X/Y or
Cross/Circle/Square/Triangle), so the same code works across
controllers whose silkscreens disagree. The default `input_router`
mapping is:

- button 1 -> press selected key (or left mouse click in mouse mode)
- button 2 -> press with Ctrl (or right mouse click in mouse mode)
- button 3 -> press with Shift
- button 4 -> press with Alt
- button 5 -> Backspace (or "toggle mouse mode" when chorded with 6)
- button 6 -> sticky Shift toggle (or "toggle mouse mode" when chorded with 5)
- button 7 -> cycle keyboard layout
- button 8 -> cycle color theme

### UART transport

The driver installs the UART in receive-only mode (no TX / RTS /
CTS pin is driven) and reads one 6-byte frame at a time. The
analog axes are reduced to discrete D-pad directions using
`CONFIG_SK_GAMEPAD_AXIS_DEADZONE`. To adapt the wire format, edit
`gamepad_parse_report()` in
`components/gamepad_uart/src/gamepad_uart.c`.

## Power

The Waveshare board is USB-powered (5 V) with a 3.3 V LDO and a
LiPo charger. `CONFIG_BOARD_HAS_BATTERY` is not yet selected for
any shipping board -- when it is, `board_t.battery_adc_channel`
gates the on-screen battery indicator.
