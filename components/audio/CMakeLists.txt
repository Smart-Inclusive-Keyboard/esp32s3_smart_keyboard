if(CONFIG_BOARD_HAS_PSRAM AND CONFIG_BOARD_HAS_SPEAKER)
    set(_audio_srcs "src/audio.c")
else()
    # Compile a tiny stub so the public API still links when the
    # board has no speaker (narrator code calls into us either way).
    set(_audio_srcs "src/audio_stub.c")
endif()

idf_component_register(
    SRCS ${_audio_srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES board driver esp_psram
)
