#pragma once

#ifdef SKINPACK_HOST_TEST
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#else
#include "platform.h"
#endif

enum SkinFile {
    SK_CLICK, SK_CLICK_MH, SK_DRAG, SK_DRAG_MH, SK_FLICK, SK_FLICK_MH,
    SK_HOLD, SK_HOLD_MH, SK_HIT_FX, SK_SND_CLICK, SK_SND_DRAG, SK_SND_FLICK,
    SK_FILE_COUNT
};
struct SkinBlob { uint8_t* data; uint32_t size; int w, h; };
struct SkinPack {
    char name[128], author[128], description[512];
    int hitFxCols, hitFxRows;
    float hitFxDuration, hitFxScale;
    bool hitFxRotate, hitFxTinted, hideParticles;
    int holdTop, holdBottom, holdTopMH, holdBottomMH;
    bool holdKeepHead, holdRepeat, holdCompact;
    uint32_t colorPerfect, colorGood;
    SkinBlob files[SK_FILE_COUNT];
};
enum SkinError {
    SKE_OK, SKE_NOT_ZIP, SKE_ZIP_CORRUPT, SKE_ZIP_UNSUPPORTED, SKE_TOO_BIG,
    SKE_NO_INFO, SKE_INFO_SYNTAX, SKE_INFO_VALUE, SKE_MISSING_FILE,
    SKE_BAD_IMAGE, SKE_BAD_SOUND, SKE_NO_MEMORY
};
struct SkinResult { SkinError err; char detail[256]; };

// out must not own a previous pack. Failure leaves it zeroed and safe to free.
// Input is borrowed only for this call; success transfers all blobs to out.
bool skinPackLoad(const uint8_t* zip, uint32_t size, SkinPack* out, SkinResult* res);
void skinPackFree(SkinPack* p);
const char* skinErrorName(SkinError e);
