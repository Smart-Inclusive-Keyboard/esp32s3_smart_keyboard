/*
 * Stub backend for boards without a speaker.
 * Keeps the narrator code call-site clean of #ifdefs.
 */
#include "audio.h"
int  audio_init(void)                                  { return 0; }
int  audio_play_wav(const void *data, size_t len)      { (void)data; (void)len; return 0; }
void audio_stop(void)                                  { }
void audio_set_volume(int percent)                     { (void)percent; }
bool audio_is_playing(void)                            { return false; }
