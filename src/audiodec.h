#pragma once

#ifdef AUDIODEC_HOST_TEST
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#else
#include "platform.h"
typedef short int16_t;
#endif

struct PcmAudio { int16_t* samples; uint32_t frames; int channels; int rate; };
enum AudioError {
    AUE_OK, AUE_UNKNOWN_FORMAT, AUE_CORRUPT, AUE_UNSUPPORTED, AUE_TOO_LONG, AUE_NO_MEMORY
};

// Input is borrowed; out must not own an earlier allocation. Failure zeroes out.
// Channels above two are averaged into even/odd stereo lanes, then clipped.
bool audioDecode(const uint8_t* data, uint32_t size, PcmAudio* out,
                 AudioError* err, char* detail, int detailCap);
void audioFree(PcmAudio* a);
const char* audioErrorName(AudioError e);
