if(CONFIG_BOARD_HAS_PSRAM AND CONFIG_BOARD_HAS_SPEAKER AND CONFIG_NARRATOR_ENABLE)
    # WAV assets live under wav/ and are embedded only when the
    # narrator is enabled. To populate them, drop the upstream
    # clackups/smart-keyboard audio/*.wav files into
    # components/narrator/wav/ -- see assets/README.md for the
    # full filename list.
    file(GLOB _wavs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "wav/*.wav")
    if(_wavs)
        # NOTE: use REQUIRES (not PRIV_REQUIRES) so the audio and
        # kb_layout component INCLUDE_DIRS are guaranteed to be on
        # the compile line for narrator.c during the early
        # expansion phase. With EMBED_FILES in play, PRIV_REQUIRES
        # propagation has been observed to lose audio.h visibility
        # on some IDF versions (fatal error: audio.h: No such
        # file or directory).
        idf_component_register(
            SRCS "src/narrator.c"
            INCLUDE_DIRS "include"
            REQUIRES audio kb_layout
            EMBED_FILES ${_wavs}
        )
    else()
        # No assets dropped in yet -- fall back to the no-op stub
        # so the firmware still builds. Without at least one file,
        # idf_component_register(EMBED_FILES) leaves the component
        # half-registered and downstream sources lose visibility
        # of audio.h (see PRIV_REQUIRES propagation).
        message(WARNING
            "narrator: CONFIG_NARRATOR_ENABLE is set but "
            "components/narrator/wav/ has no .wav files. "
            "Building the no-op stub. Drop the upstream "
            "smart-keyboard audio/*.wav pack into "
            "components/narrator/wav/ to enable spoken letters.")
        idf_component_register(
            SRCS "src/narrator_stub.c"
            INCLUDE_DIRS "include"
        )
    endif()
else()
    # Stub mode: every public function compiles to a no-op so
    # callers don't need to #ifdef.
    idf_component_register(
        SRCS "src/narrator_stub.c"
        INCLUDE_DIRS "include"
    )
endif()
