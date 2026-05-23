# Hardware

## Supported boards

### Waveshare ESP32-S3-Touch-LCD-3.49 (default)

| Function           | GPIO       | Notes                              |
| ------------------ | ---------- | ---------------------------------- |
| LCD QSPI CS        | 9          | AXS15231B QSPI panel               |
| LCD QSPI SCK       | 12         |                                    |
| LCD QSPI D0..D3    | 11, 13, 14, 10 |                                |
| LCD RST            | 17         | hard reset                         |
| LCD TE             | 18         | tearing-effect input               |
| LCD BL             | 8          | active LOW, LEDC PWM @ 50 kHz      |
| I2C gamepad SDA    | 11 (Kconfig)| External header                   |
| I2C gamepad SCL    | 10 (Kconfig)| External header                   |
| I2S MCLK / BCLK / LRCK / DOUT | 7 / 15 / 46 / 45 | on-board ES8311 codec + speaker |

- 16 MB flash (QIO, 80 MHz)
- 8 MB octal PSRAM (mode O, 80 MHz)
- On-board I2S speaker via ES8311 codec. The board automatically
  selects `BOARD_HAS_SPEAKER`, so the narrator is compiled in by
  default. The pins above are hard-coded in
  `components/board/src/board_waveshare_esp32s3_touch_lcd_349.c`
  and override the generic `AUDIO_I2S_*` Kconfig defaults.

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
byte 2:  face button bitmap A=0x01 B=0x02 X=0x04 Y=0x08
byte 3:  aux  button bitmap L=0x01 R=0x02 SELECT=0x04 START=0x08
```

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
