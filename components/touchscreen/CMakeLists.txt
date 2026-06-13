idf_component_register(
    SRCS "src/touchscreen.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES board driver esp_timer display esp_driver_gpio esp_driver_i2c
)
