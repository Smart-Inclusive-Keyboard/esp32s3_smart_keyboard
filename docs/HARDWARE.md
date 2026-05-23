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
| I2S BCLK / LRCK / DOUT | 5 / 6 / 7 (Kconfig) | external DAC (optional)  |

- 16 MB flash (QIO, 80 MHz)
- 8 MB octal PSRAM (mode O, 80 MHz)
- No on-board speaker. Wire an external I2S DAC (e.g.
  PCM5102A, MAX98357A) and enable
  `BOARD_HAS_SPEAKER` manually in menuconfig to get narration.

### Generic ESP32-S3 / Generic ESP32

Placeholder boards. Edit
`components/board/src/board_generic_s3.c` (or
`board_generic_esp32.c`) to fill in the pin map for your custom
wiring, then select the matching board in menuconfig.

## I2C gamepad wiring

The gamepad protocol is a fixed 4-byte HID-style report read
from the configured 7-bit slave address:

```
byte 0:  X axis, int8_t (-128..127, 0 = centred)
byte 1:  Y axis, int8_t (positive = down)
byte 2:  face button bitmap A=0x01 B=0x02 X=0x04 Y=0x08
byte 3:  aux  button bitmap L=0x01 R=0x02 SELECT=0x04 START=0x08
```

To use a controller with a different protocol, edit
`gamepad_parse_report()` in
`components/gamepad_i2c/src/gamepad_i2c.c`. The driver does no
register addressing -- it issues a raw 4-byte read every poll
interval.

## Power

The Waveshare board is USB-powered (5 V) with a 3.3 V LDO and a
LiPo charger. `CONFIG_BOARD_HAS_BATTERY` is not yet selected for
any shipping board -- when it is, `board_t.battery_adc_channel`
gates the on-screen battery indicator.
