## BLE HID backend.
##
## NimBLE is only required when BLE is the active transport, so we
## gate the source compile + bt dependency on SK_HID_TRANSPORT_BLE.
## When USB is selected the file still exists but the archive will
## hold the no-op stub branch below in src/ble_hid.c so the unified
## hid facade can link against the public symbols unconditionally.
## SK_HID_TRANSPORT_BLE already `select`s BT_ENABLED + BT_NIMBLE_ENABLED
## (see main/Kconfig.projbuild), so depending on the bt component whenever
## the BLE transport is active is always correct. Keeping a single gate
## here also matches the #if in src/ble_hid.c.
set(reqs nvs_flash)
if(CONFIG_SK_HID_TRANSPORT_BLE)
    list(APPEND reqs bt)
endif()

idf_component_register(
    SRCS "src/ble_hid.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${reqs}
)

