if(CONFIG_BOARD_HAS_PSRAM AND CONFIG_BOARD_HAS_SPEAKER AND CONFIG_NARRATOR_ENABLE)
    # WAV assets live under wav/ and are embedded only when the
    # narrator is enabled. To populate them, drop the upstream
    # clackups/smart-keyboard audio/*.wav files into
    # components/narrator/wav/ -- see assets/README.md for the
    # full filename list.
    file(GLOB _wavs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "wav/*.wav")
    idf_component_register(
        SRCS "src/narrator.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES audio kb_layout
        EMBED_FILES ${_wavs}
    )
else()
    # Stub mode: every public function compiles to a no-op so
    # callers don't need to #ifdef.
    idf_component_register(
        SRCS "src/narrator_stub.c"
        INCLUDE_DIRS "include"
    )
endif()
