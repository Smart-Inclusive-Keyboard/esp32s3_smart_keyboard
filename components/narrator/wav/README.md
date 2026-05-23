# Narrator assets

This directory holds WAV files embedded into the firmware at
build time via `EMBED_FILES` (see
`components/narrator/CMakeLists.txt`). The narrator component is
**only** compiled in when the selected board has both PSRAM and
a speaker and `CONFIG_NARRATOR_ENABLE` is set; otherwise the
embed list is skipped entirely.

## Where do the WAVs come from?

They are taken verbatim from the upstream
[clackups/smart-keyboard](https://github.com/clackups/smart-keyboard)
project's `audio/` folder (MIT-licensed, same as this firmware).
The letter clips are the US-English set (`us_<letter>.wav` upstream),
renamed to drop the `us_` prefix so they match the symbol names
the narrator declares. `esc.wav` upstream is imported as
`escape.wav`. The narrator expects the following filenames:

```
a.wav, b.wav, ..., z.wav
0.wav, 1.wav, ..., 9.wav       (optional, not yet wired)
space.wav
enter.wav
backspace.wav
tab.wav
escape.wav
shift.wav
ctrl.wav
alt.wav
```

## Format

Standard RIFF/WAVE, **PCM 16-bit mono, 22050 Hz**. The I2S
driver re-clocks to the file's reported sample rate, so other
rates work too, but 22050 / 16-bit is what the upstream pack
uses and what the audio buffer sizes in `components/audio/` are
tuned for.

## License

MIT, inherited from the upstream project. See its `LICENSE`
file for the exact text.
