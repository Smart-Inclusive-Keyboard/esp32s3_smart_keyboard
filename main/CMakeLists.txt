idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        board display fonts theme kb_layout
        keyboard_ui gamepad_i2c input_router
        ble_hid narrator audio
        nvs_flash esp_timer
)
