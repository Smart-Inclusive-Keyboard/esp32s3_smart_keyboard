idf_component_register(
    SRCS "src/touchscreen.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES board driver esp_timer display
)
