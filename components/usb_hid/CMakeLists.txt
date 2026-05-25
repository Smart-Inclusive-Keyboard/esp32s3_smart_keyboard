## USB HID backend.
##
## The TinyUSB managed component (espressif/esp_tinyusb) is only
## pulled in when SK_HID_TRANSPORT_USB is selected, so classic
## ESP32 / BLE-only builds don't drag the USB stack into the image.
## When BLE is the active transport this component compiles down to
## tiny no-op stubs (see the #else branch in src/usb_hid.c).
set(reqs "")
if(CONFIG_SK_HID_TRANSPORT_USB)
    list(APPEND reqs esp_tinyusb)
endif()

idf_component_register(
    SRCS "src/usb_hid.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${reqs}
)
