// What PhiMe costs per frame, by part, logged every 10 s while a song runs ("perf:" lines), next to the game's own
// note update for scale. For low-end devices that drop frames: their log says which part to look at.
#pragma once
#include "platform.h"

enum {
    PF_GAME_NOTES,  // the game's own NoteUpdateManager.Update (pass 1): the baseline, not PhiMe's
    PF_PASS2,       // 大小 P's second note update (only the held-back plain-Perfect taps)
    PF_CRIT,        // 大小 P's own bookkeeping around them
    PF_SKIN_SCAN,   // skin: giving notes the skin's sprites
    PF_SKIN_FX,     // skin: the hit effects' frames
    PF_MASK,        // 上隐条
    PF_HUD,         // accuracy line / judgement counts / hiding the score
    PF_CLONE,       // our part of every Instantiate (hit effects, popups)
    PF_CALLBACKS,   // every per-frame callback together (includes skin and mask)
    PF_COUNT
};

uint64_t perfNow();                     // ns, monotonic
void perfAdd(int slot, uint64_t ns);    // main thread
void perfFrame();                       // once per frame (unity.cpp frameDispatch)

struct PerfScope {
    int slot;
    uint64_t t0;
    explicit PerfScope(int s) : slot(s), t0(perfNow()) {}
    ~PerfScope() { perfAdd(slot, perfNow() - t0); }
};
