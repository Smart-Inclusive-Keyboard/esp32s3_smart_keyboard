idf_component_register(
    SRCS "src/keyboard_ui.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES display theme fonts kb_layout hid nvs_flash narrator
)
