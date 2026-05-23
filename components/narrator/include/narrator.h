#pragma once

/*
 * Letter-name narrator.
 *
 * Speaks the name of the currently selected key as the user
 * navigates the on-screen keyboard. Compiled in only when the
 * board has PSRAM + speaker and CONFIG_NARRATOR_ENABLE is set;
 * the stub keeps callers ifdef-free.
 *
 * The vocabulary mirrors the upstream Rust project's audio/
 * folder: A-Z, 0-9, "shift", "ctrl", "alt", "enter", "space",
 * "backspace", and the punctuation letters. Files are embedded
 * via EMBED_FILES at build time.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the audio backend and load the embedded clip table.
 * Idempotent. */
void narrator_init(void);

/* Speak the name of the currently selected key (queried from
 * kb_layout_active()). Preempts any in-progress narration. */
void narrator_speak_selection(void);

/* Speak by raw HID usage ID. */
void narrator_speak_hid(unsigned hid_usage);

#ifdef __cplusplus
}
#endif
