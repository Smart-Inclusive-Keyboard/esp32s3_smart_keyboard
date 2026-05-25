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
    ## BT_NIMBLE_ENABLED is a member of ESP-IDF's `choice BT_HOST`,
    ## which Kconfig will not let us `select` from the project
    ## Kconfig. If the user's sdkconfig has Bluedroid (or no host)
    ## selected, the `bt` component skips its nimble include path
    ## and we'd otherwise fail deep in the compile with a cryptic
    ## "nimble/ble.h: No such file or directory". Fail loudly here
    ## with an actionable message instead.
    if(NOT CONFIG_BT_NIMBLE_ENABLED)
        message(FATAL_ERROR
            "SK_HID_TRANSPORT_BLE requires the NimBLE Bluetooth host. "
            "Run `idf.py menuconfig` and set "
            "Component config -> Bluetooth -> Host -> "
            "\"NimBLE - BLE only\", or delete sdkconfig and rebuild "
            "to pick up sdkconfig.defaults (which sets "
            "CONFIG_BT_NIMBLE_ENABLED=y).")
    endif()
    list(APPEND srcs "src/ble_hid.c")
    list(APPEND reqs bt)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${reqs}
)

