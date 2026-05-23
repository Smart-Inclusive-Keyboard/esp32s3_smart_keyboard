idf_component_register(
    SRCS "src/gamepad_spi.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES board driver esp_timer gamepad_i2c
)
