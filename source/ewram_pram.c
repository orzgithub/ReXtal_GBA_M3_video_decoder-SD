#if EWRAM_AS_PRAM
#include <gba_base.h>
#include "media_source.h"

__attribute__((used))
uint8_t audio_window[AUDIO_WINDOW_SIZE] EWRAM_BSS;

__attribute__((used))
uint8_t video_window[VIDEO_WINDOW_SIZE] EWRAM_BSS;
#endif