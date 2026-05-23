idf_component_register(
    SRCS "src/gamepad_i2c.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES board driver esp_timer
)
