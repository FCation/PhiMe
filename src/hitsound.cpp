// 正解音 (on-time hit sounds): every note's hit sound plays exactly at the note's time, whatever the touch timing.
//
// How the game plays them (4.0.x): ClickControl / DragControl / FlickControl / HoldControl.Judge call the static
// delegate SEPlayer.PlayHitFx(index) on a Perfect / Good: 7 = tap (and a hold's head), 8 = drag, 9 = flick. The
// call is inlined (delegate->invoke_impl(invoke_impl_this, index, method)), so the delegate itself is patched: its
// invoke_impl points to ours, which drops the game's 7 / 8 / 9 while the mode is on in a level. We then play the
// same delegate's sound for each note of JudgeControl.chartNoteSortByTime when ProgressControl.nowTime reaches its
// realTime. A note is not known to be hit before its time (a late hit is still to come), so every note sounds.
// Seeks (practice, the resume replay's rewind) and big jumps move the cursor silently; the replay then sounds.
//
// The sounds follow the MUSIC, not the chart: nowTime = music time - (chart offset + global offset + the player's
// delay setting, GameInformation.offset). The delay only moves the chart (it makes up for e.g. Bluetooth latency,
// which the music and our sounds share), so a note's sound plays at nowTime + delay = its realTime.
#include "mod.h"
#include "practice.h"

using namespace il2cpp;
using namespace unity;

// Il2CppDelegate fields the inlined call reads (see ClickControl.Judge)
static const int kDelImpl = 0x18, kDelMethod = 0x28, kDelThis = 0x40;

static struct {
    Il2CppClass* SE;
    FieldInfo* fx;
    int pcNow, pcPlaying, pcJc, jcSorted, noteType, noteTime, giOffset;
    Il2CppClass* GI;
    int listItems = -1, listSize = -1;
    bool ok;
} H;

typedef void (*FxFn)(void* self, int index, const MethodInfo* m);
static FxFn g_origFx;
static bool g_inLevel = false, g_ours = false;
static Handle g_pc;
static int g_cursor = 0, g_played = 0;
static float g_last = -1e9f;

static void hk_fx(void* self, int index, const MethodInfo* m) {
    if (g_inLevel && g_cfg.correctSound && !g_ours && index >= 7 && index <= 9) return;
    if (g_origFx) g_origFx(self, index, m);
}

static Obj* fxDelegate() {
    Obj* d = nullptr;
    if (H.fx) il2cpp_field_static_get_value(H.fx, &d);
    return d;
}

// (re)patch the current delegate: the game may make a new one
static Obj* ensurePatched() {
    Obj* d = fxDelegate();
    if (!d) return nullptr;
    void*& impl = fld<void*>(d, kDelImpl);
    if (impl != (void*)hk_fx) {
        if (!impl) return nullptr;
        g_origFx = (FxFn)impl;
        impl = (void*)hk_fx;
        LOG("hitsound: PlayHitFx delegate patched");
    }
    return d;
}

static void play(Obj* d, int index) {
    if (!g_origFx) return;
    g_ours = true;
    g_origFx(fld<void*>(d, kDelThis), index, fld<const MethodInfo*>(d, kDelMethod));
    g_ours = false;
    g_played++;
}

static int listView(Obj* list, Obj*** out) {
    if (!list) return 0;
    if (H.listItems < 0) {
        Il2CppClass* k = il2cpp_object_get_class(list);
        H.listItems = fieldOffset(k, "_items");
        H.listSize = fieldOffset(k, "_size");
        if (H.listItems < 0 || H.listSize < 0) return 0;
    }
    Il2CppArray* items = fld<Il2CppArray*>(list, H.listItems);
    int n = fld<int>(list, H.listSize);
    if (!items || n <= 0 || (uintptr_t)n > items->max_length) return 0;
    *out = (Obj**)arrayData(items);
    return n;
}

// the player's delay setting (seconds), part of the level's offset
static float userDelay() {
    FieldInfo* f = H.GI && H.giOffset >= 0 ? il2cpp_class_get_field_from_name(H.GI, "_main") : nullptr;
    Obj* gi = nullptr;
    if (f) il2cpp_field_static_get_value(f, &gi);
    float d = gi ? fld<float>(gi, H.giOffset) : 0;
    return d > -5 && d < 5 ? d : 0;
}

static int soundOf(int type) { return type == 2 ? 8 : type == 4 ? 9 : 7; }  // 1 tap, 2 drag, 3 hold, 4 flick

static void seek(Obj** notes, int n, float now) {
    int lo = 0, hi = n;  // first note later than now
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (notes[mid] && fld<float>(notes[mid], H.noteTime) <= now) lo = mid + 1;
        else hi = mid;
    }
    g_cursor = lo;
    g_last = now;
}

static void hsFrame(void*) {
    Obj* pc = (Obj*)g_pc.get();
    if (!pc) return;
    if (!alive(pc)) {
        g_pc.clear();
        g_inLevel = false;
        LOG("hitsound: level over, %d on-time sounds", g_played);
        return;
    }
    g_inLevel = true;
    if (!g_cfg.correctSound) {
        g_last = -1e9f;  // switched off mid-level: re-seek when on again
        return;
    }
    Obj* d = ensurePatched();
    Obj* jc = fld<Obj*>(pc, H.pcJc);
    Obj** notes = nullptr;
    int n = jc ? listView(fld<Obj*>(jc, H.jcSorted), &notes) : 0;
    if (!d || n <= 0) return;
    float now = fld<float>(pc, H.pcNow) + userDelay();  // music time, in chart seconds
    // the practice section picker is open: dragging its timeline moves the chart preview, follow it silently
    // (a pause needs nothing: the time stands still; the resume replay must keep sounding)
    if (editorIsOpen() || g_prac.st == PR_EDIT) {
        seek(notes, n, now);
        return;
    }
    // a jump (practice seek, retry, first frame, a long hitch, the resume replay rewinding): no sounds, just
    // follow. The replay itself then sounds like normal play.
    if (now < g_last - 0.02f || now > g_last + 1.0f) {
        seek(notes, n, now);
        return;
    }
    int burst = 0;
    while (g_cursor < n) {
        Obj* note = notes[g_cursor];
        if (note && fld<float>(note, H.noteTime) > now) break;
        if (note && burst < 6) {  // chords sound together, as in the game; a hitch does not pile up
            play(d, soundOf(fld<int>(note, H.noteType)));
            burst++;
        }
        g_cursor++;
    }
    g_last = now;
}

void hitsoundLevelStart(Obj* pc) {
    if (!H.ok) return;
    g_pc.set(pc);
    g_cursor = 0;
    g_last = -1e9f;
    g_played = 0;
    g_inLevel = true;
    ensurePatched();
    onEveryFrame(hsFrame);
    LOG("hitsound: level start, on-time sounds %s, following the music (delay setting %+.0f ms)",
        g_cfg.correctSound ? "on" : "off", userDelay() * 1000);
}

bool hitsoundInstall() {
    H.SE = findClass("", "SEPlayer");
    Il2CppClass* PC = findClass("", "ProgressControl");
    Il2CppClass* JC = findClass("", "JudgeControl");
    Il2CppClass* Note = findClass("", "ChartNote");
    if (!H.SE || !PC || !JC || !Note) return false;
    H.fx = il2cpp_class_get_field_from_name(H.SE, "PlayHitFx");
    H.pcNow = fieldOffset(PC, "nowTime");
    H.pcPlaying = fieldOffset(PC, "isPlaying");
    H.pcJc = fieldOffset(PC, "judgeControl");
    H.jcSorted = fieldOffset(JC, "chartNoteSortByTime");
    H.noteType = fieldOffset(Note, "type");
    H.noteTime = fieldOffset(Note, "realTime");
    H.GI = findClass("", "GameInformation");
    H.giOffset = H.GI ? fieldOffset(H.GI, "offset") : -1;
    H.ok = H.fx && H.pcNow >= 0 && H.pcJc >= 0 && H.jcSorted >= 0 && H.noteType >= 0 && H.noteTime >= 0;
    LOG("hitsound: on-time hit sounds %s", H.ok ? "ready" : "off (fields missing)");
    return H.ok;
}
