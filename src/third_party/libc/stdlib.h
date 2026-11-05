/* PhiMe: freestanding stand-in for the third-party decoders (dr_mp3, dr_flac); only the build sees it */
#pragma once
#include "../../platform.h"
extern "C" void* realloc(void* p, size_t n);
