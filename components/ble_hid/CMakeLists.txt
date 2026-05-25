## BLE HID backend.
##
## NimBLE is only required when BLE is the active transport, but
## src/ble_hid.c always lists `#include <nimble/...>` inside an
## `#if CONFIG_SK_HID_TRANSPORT_BLE` block. ESP-IDF >= 5.5's include
## checker scans those `#include` directives statically (it does not
## evaluate the `#if`), so it demands `bt` in PRIV_REQUIRES whenever
## the bt component is part of the build, regardless of our own
## Kconfig choice. Querying BUILD_COMPONENTS makes the requirement
## track that reality exactly:
##   * BLE transport selected -> Kconfig selects BT_ENABLED +
##     BT_NIMBLE_ENABLED, the bt component is built, and we
##     correctly PRIV_REQUIRE it.
##   * USB transport selected on a BLE-capable chip with BT still
##     enabled in sdkconfig -> bt component is built and the include
##     checker is happy.
##   * USB transport on a chip with no BT support (ESP32-S2) -> bt
##     component is not built, the `#include` is hidden behind the
##     inactive `#if`, and the include checker has nothing to flag.
set(reqs nvs_flash)
idf_build_get_property(build_components BUILD_COMPONENTS)
if("bt" IN_LIST build_components)
    list(APPEND reqs bt)
endif()

idf_component_register(
    SRCS "src/ble_hid.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${reqs}
)

