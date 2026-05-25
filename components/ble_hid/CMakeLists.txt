## BLE HID backend.
##
## ESP-IDF >= 5.5's include-checker scans `#include` directives
## statically and does NOT evaluate the surrounding `#if`. That means
## any nimble header listed in src/ble_hid.c is seen by the checker
## even when the BLE transport isn't selected, and gets flagged as
## "bt component not in requirements list". The only robust way to
## silence it is to keep the source file out of the build entirely
## when BLE isn't selected. The unified hid.c facade already gates
## every call into this backend on CONFIG_SK_HID_TRANSPORT_BLE, so
## no stub TU is required for the link to succeed.
set(srcs "")
set(reqs nvs_flash)
if(CONFIG_SK_HID_TRANSPORT_BLE)
    list(APPEND srcs "src/ble_hid.c")
    list(APPEND reqs bt)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${reqs}
)

