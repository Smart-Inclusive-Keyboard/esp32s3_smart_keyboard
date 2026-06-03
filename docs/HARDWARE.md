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
| SPI gamepad SCLK / MOSI / MISO | 9 / 10 / 11 | SPI3 host, no chip-select  |
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
- The gamepad SPI bus is hard-wired to GPIO 9 / 10 / 11 (SCLK,
  MOSI, MISO) on SPI3. There is no dedicated CS pin -- the
  gamepad is the only slave on the bus and `/CS` stays
  asserted.
- The I2C gamepad transport is not wired by default on this
  board (every Kconfig default pin clashes with display,
  audio or touch). Edit the `.i2c_*` fields in
  `board_waveshare_esp32s3_touch_lcd_35b.c` if you need it.
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

The external gamepad can be wired to either an I2C bus or an SPI
bus; the firmware is always the bus master / host, and the
gamepad is always the slave. Pick a transport with
`CONFIG_SK_GAMEPAD_TRANSPORT_{I2C,SPI}` in menuconfig.

Both transports use the same fixed 4-byte HID-style report:

```
byte 0:  X axis, int8_t (-128..127, 0 = centred)
byte 1:  Y axis, int8_t (positive = down)
byte 2:  face button bitmap: button 1=0x01, 2=0x02, 3=0x04, 4=0x08
byte 3:  aux  button bitmap: button 5=0x01, 6=0x02, 7=0x04, 8=0x08
```

The firmware refers to gamepad buttons by their HID number
(1..8) rather than by vendor letter names (A/B/X/Y or
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

### I2C transport

Read from the configured 7-bit slave address every
`CONFIG_SK_GAMEPAD_POLL_MS` milliseconds. The driver does no
register addressing -- it issues a raw 4-byte read every poll
interval. To use a controller with a different protocol, edit
`gamepad_parse_report()` in
`components/gamepad_i2c/src/gamepad_i2c.c`.

### SPI transport

The device asserts CS and issues a single full-duplex 4-byte
transaction every `CONFIG_SK_GAMEPAD_POLL_MS` milliseconds; the
MOSI byte is a dummy command (`0x00`) and the gamepad clocks the
4-byte report back on MISO. SCLK / MOSI / MISO / CS pins, SPI
host, clock frequency, and SPI mode (CPOL/CPHA) are all set in
menuconfig under **SMART KEYBOARD -> Gamepad -> SPI gamepad**.
The decoder is the same `gamepad_parse_report()` style routine
in `components/gamepad_spi/src/gamepad_spi.c`.

## Power

The Waveshare board is USB-powered (5 V) with a 3.3 V LDO and a
LiPo charger. `CONFIG_BOARD_HAS_BATTERY` is not yet selected for
any shipping board -- when it is, `board_t.battery_adc_channel`
gates the on-screen battery indicator.
