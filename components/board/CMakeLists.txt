idf_component_register(
    SRCS
        "src/board.c"
        "src/board_waveshare_esp32s3_touch_lcd_349.c"
        "src/board_waveshare_esp32s3_touch_lcd_35b.c"
        "src/board_generic_s3.c"
        "src/board_generic_esp32.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver esp_lcd esp_psram
)
