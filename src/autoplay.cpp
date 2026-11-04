// Autoplay: every note gets the game's own Perfect judgement.
//
// How the game judges (Phigros 4.0.x):
//   Click / Hold head : JudgeControl sets ChartNote.isJudged on touch; XxxControl.Judge() then scores
//                       |realTime - nowTime| against JudgeControl.PerfectTimeRange.
//   Hold body         : HoldControl.Judge() looks for a finger near the note every frame; when none is
//                       found `_safeFrame` counts down and the hold breaks.
//   Drag              : DragControl.isJudged is set by a nearby finger, Perfect when the time arrives.
//   Flick             : ChartNote.isJudgedForFlick, Perfect when the time arrives.
// We flip those flags right before NoteUpdateManager.Update runs the per-note Judge() calls, so scoring,
// hit sounds, effects and combo all come from the game itself.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

ModConfig g_cfg;

void configLoad() {
    if (g_cfg.loaded) return;
    g_cfg.autoplay = prefGetInt("PhiMeMod.autoplay", 0) != 0;
    g_cfg.practice = prefGetInt("PhiMeMod.practice", 0) != 0;
    g_cfg.practiceSpeed = prefGetInt("PhiMeMod.practiceSpeed", 100);
    if (g_cfg.practiceSpeed < 5 || g_cfg.practiceSpeed > 200) g_cfg.practiceSpeed = 100;
    g_cfg.practiceLoop = prefGetInt("PhiMeMod.practiceLoop", 0) != 0;
    g_cfg.practiceKeepPitch = prefGetInt("PhiMeMod.practiceKeepPitch", 1) != 0;
    g_cfg.resumePreview = prefGetInt("PhiMeMod.resumePreview", 1) != 0;
    g_cfg.resumePreviewTenths = prefGetInt("PhiMeMod.resumePreviewTenths", 30);
    if (g_cfg.resumePreviewTenths < 0 || g_cfg.resumePreviewTenths > 30) g_cfg.resumePreviewTenths = 30;
    g_cfg.accShow = prefGetInt("PhiMeMod.accShow", 0) != 0;
    g_cfg.accMode = prefGetInt("PhiMeMod.accMode", 0) == 1 ? 1 : 0;
    g_cfg.accSwap = prefGetInt("PhiMeMod.accSwap", 0) != 0;
    g_cfg.judgeShow = prefGetInt("PhiMeMod.judgeShow", 0) != 0;
    g_cfg.fastLoad = prefGetInt("PhiMeMod.fastLoad", 1) != 0;
    g_cfg.critical = prefGetInt("PhiMeMod.critical", 0) != 0;
    g_cfg.course = prefGetInt("PhiMeMod.course", 0) != 0;
    g_cfg.lang = prefGetInt("PhiMeMod.lang", 0) == 1 ? 1 : 0;
    g_cfg.resultAccBig = prefGetInt("PhiMeMod.resultAccBig", 0) != 0;
    g_cfg.correctSound = prefGetInt("PhiMeMod.correctSound", 0) != 0;
    g_cfg.skin = prefGetInt("PhiMeMod.skin", 0) != 0;
    g_cfg.skinId = prefGetInt("PhiMeMod.skinId", 0);
    g_cfg.maskOn = prefGetInt("PhiMeMod.maskOn", 0) != 0;
    {
        int m = prefGetInt("PhiMeMod.maskPct", 25);
        g_cfg.maskPct = m < 0 ? 0 : m > 50 ? 50 : m;
    }
    int* vols[3] = {&g_cfg.volTap, &g_cfg.volDrag, &g_cfg.volFlick};
    const char* volKeys[3] = {"PhiMeMod.volTap", "PhiMeMod.volDrag", "PhiMeMod.volFlick"};
    for (int i = 0; i < 3; i++) {
        int v = prefGetInt(volKeys[i], 100);
        *vols[i] = v < 0 ? 0 : v > 200 ? 200 : v;
    }
    if (g_cfg.skin) g_cfg.critical = false;  // skins have no Critical Perfect
    setFontLanguage(g_cfg.lang == 1);
    g_cfg.loaded = true;
}

void configSave() {
    prefSetInt("PhiMeMod.autoplay", g_cfg.autoplay ? 1 : 0);
    prefSetInt("PhiMeMod.practice", g_cfg.practice ? 1 : 0);
    prefSetInt("PhiMeMod.practiceSpeed", g_cfg.practiceSpeed);
    prefSetInt("PhiMeMod.practiceLoop", g_cfg.practiceLoop ? 1 : 0);
    prefSetInt("PhiMeMod.practiceKeepPitch", g_cfg.practiceKeepPitch ? 1 : 0);
    prefSetInt("PhiMeMod.resumePreview", g_cfg.resumePreview ? 1 : 0);
    prefSetInt("PhiMeMod.resumePreviewTenths", g_cfg.resumePreviewTenths);
    prefSetInt("PhiMeMod.accShow", g_cfg.accShow ? 1 : 0);
    prefSetInt("PhiMeMod.accMode", g_cfg.accMode);
    prefSetInt("PhiMeMod.accSwap", g_cfg.accSwap ? 1 : 0);
    prefSetInt("PhiMeMod.judgeShow", g_cfg.judgeShow ? 1 : 0);
    prefSetInt("PhiMeMod.fastLoad", g_cfg.fastLoad ? 1 : 0);
    prefSetInt("PhiMeMod.critical", g_cfg.critical ? 1 : 0);
    prefSetInt("PhiMeMod.course", g_cfg.course ? 1 : 0);
    prefSetInt("PhiMeMod.lang", g_cfg.lang);
    prefSetInt("PhiMeMod.resultAccBig", g_cfg.resultAccBig ? 1 : 0);
    prefSetInt("PhiMeMod.correctSound", g_cfg.correctSound ? 1 : 0);
    prefSetInt("PhiMeMod.skin", g_cfg.skin ? 1 : 0);
    prefSetInt("PhiMeMod.skinId", g_cfg.skinId);
    prefSetInt("PhiMeMod.maskOn", g_cfg.maskOn ? 1 : 0);
    prefSetInt("PhiMeMod.maskPct", g_cfg.maskPct);
    prefSetInt("PhiMeMod.volTap", g_cfg.volTap);
    prefSetInt("PhiMeMod.volDrag", g_cfg.volDrag);
    prefSetInt("PhiMeMod.volFlick", g_cfg.volFlick);
    setFontLanguage(g_cfg.lang == 1);
    prefSave();
}

static struct {
    int numPC, numClick, numDrag, numHold, numFlick;
    int pcNow;
    int clickNote, dragNote, dragJudged, flickNote, holdNote, holdJudged, holdSafe;
    int noteJudged, noteFlick, noteTime;
    int listItems = -1, listSize = -1;
    // human-judge blocking
    int jcFingers, jcPC, jcStop, fmList, fPhase, fFlick, pcLevelInfo, liCanPause;
    const MethodInfo* checkPause;
} O;

typedef void (*VoidFn)(Obj*, const MethodInfo*);
static VoidFn orig_NUM_Update, orig_PC_Start, orig_JC_Update;

static bool g_levelAuto = false;  // autoplay was on when this level started
static float g_prevNow = -1e9f;

bool autoplayLevelActive() { return g_levelAuto; }
static Handle g_badge;

// View into a System.Collections.Generic.List<T> of reference types. Returns the element count.
static int listView(Obj* list, Obj*** out) {
    if (!list) return 0;
    if (O.listItems < 0) {
        Il2CppClass* k = il2cpp_object_get_class(list);
        O.listItems = fieldOffset(k, "_items");
        O.listSize = fieldOffset(k, "_size");
        if (O.listItems < 0 || O.listSize < 0) return 0;
    }
    Il2CppArray* items = fld<Il2CppArray*>(list, O.listItems);
    int n = fld<int>(list, O.listSize);
    if (!items || n <= 0 || (uintptr_t)n > items->max_length) return 0;
    *out = (Obj**)arrayData(items);
    return n;
}

template <typename F>
static void eachInList(Obj* list, F fn) {
    Obj** e = nullptr;
    int n = listView(list, &e);
    for (int i = 0; i < n; i++)
        if (e[i]) fn(e[i]);
}

// ---- block human judging while autoplay runs ----
// JudgeControl.Update walks the fingers twice: fingers that just touched down (phase == Began) go to
// CheckPause, then to CheckNote; flicking fingers go to CheckFlick. Real touches would therefore produce
// Good/Bad on top of autoplay. We call CheckPause ourselves (so the pause button keeps working), then hide
// "just touched" / "flicking" from the original Update and restore the real values right after it.
static void hk_JC_Update(Obj* self, const MethodInfo* m) {
    bool replay = resumeActive();  // resume preview: only the pause button, no note judging at all
    if (!g_levelAuto && !replay) {
        orig_JC_Update(self, m);
        return;
    }
    Obj* fm = fld<Obj*>(self, O.jcFingers);
    Obj** fingers = nullptr;
    int n = fm ? listView(fld<Obj*>(fm, O.fmList), &fingers) : 0;
    if (n > 32) n = 32;

    Obj* pc = fld<Obj*>(self, O.jcPC);
    Obj* li = pc ? fld<Obj*>(pc, O.pcLevelInfo) : nullptr;
    bool judging = pc && fld<float>(pc, O.pcNow) >= 0 && (replay || !fld<bool>(self, O.jcStop));
    if (judging && li && fld<bool>(li, O.liCanPause) && O.checkPause) {
        for (int i = 0; i < n; i++)
            if (fingers[i] && fld<int>(fingers[i], O.fPhase) == 0) callm<void>(O.checkPause, self, i);
    }
    if (replay) return;

    int savedPhase[32];
    bool savedFlick[32];
    for (int i = 0; i < n; i++) {
        if (!fingers[i]) continue;
        savedPhase[i] = fld<int>(fingers[i], O.fPhase);
        savedFlick[i] = fld<bool>(fingers[i], O.fFlick);
        if (savedPhase[i] == 0) fld<int>(fingers[i], O.fPhase) = 1;  // Began -> Moved
        fld<bool>(fingers[i], O.fFlick) = false;
    }
    orig_JC_Update(self, m);
    for (int i = 0; i < n; i++) {
        if (!fingers[i]) continue;
        fld<int>(fingers[i], O.fPhase) = savedPhase[i];
        fld<bool>(fingers[i], O.fFlick) = savedFlick[i];
    }
}

static void autoplayTick(Obj* num) {
    Obj* pc = fld<Obj*>(num, O.numPC);
    if (!pc) return;
    float now = fld<float>(pc, O.pcNow);
    float dt = now - g_prevNow;
    if (dt < 0 || dt > 0.1f) dt = 1.f / 60;
    g_prevNow = now;
    const float lead = dt * 0.5f;  // centre the hit inside the frame

    eachInList(fld<Obj*>(num, O.numClick), [&](Obj* c) {
        Obj* n = fld<Obj*>(c, O.clickNote);
        if (n && !fld<bool>(n, O.noteJudged) && fld<float>(n, O.noteTime) - now <= lead) fld<bool>(n, O.noteJudged) = true;
    });
    eachInList(fld<Obj*>(num, O.numHold), [&](Obj* h) {
        Obj* n = fld<Obj*>(h, O.holdNote);
        if (n && !fld<bool>(n, O.noteJudged) && fld<float>(n, O.noteTime) - now <= lead) fld<bool>(n, O.noteJudged) = true;
        if (fld<bool>(h, O.holdJudged)) fld<int>(h, O.holdSafe) = 3;  // a "finger" never leaves the hold
    });
    eachInList(fld<Obj*>(num, O.numDrag), [&](Obj* d) {
        Obj* n = fld<Obj*>(d, O.dragNote);
        float dlt = n ? fld<float>(n, O.noteTime) - now : 1;
        if (n && !fld<bool>(d, O.dragJudged) && dlt <= 0.08f && dlt >= -0.08f) fld<bool>(d, O.dragJudged) = true;
    });
    eachInList(fld<Obj*>(num, O.numFlick), [&](Obj* f) {
        Obj* n = fld<Obj*>(f, O.flickNote);
        if (n && !fld<bool>(n, O.noteFlick) && fld<float>(n, O.noteTime) - now <= 0.08f) fld<bool>(n, O.noteFlick) = true;
    });
}

// "CLAWD PLAYING…" badge at the bottom centre, with a little hopping Clawd
static Handle g_badgeClawd;
static float g_badgeT = 0;

static void badgeTick() {
    Obj* c = (Obj*)g_badgeClawd.get();
    if (!c) return;
    if (!alive(c)) {
        g_badgeClawd.clear();
        return;
    }
    g_badgeT += unscaledDt();
    setPos(rt(c), Vec2{34, 1 + clawdHop(g_badgeT, 6)});
}

static void hk_NUM_Update(Obj* self, const MethodInfo* m) {
    PM_ONCE("hook alive: NoteUpdateManager.Update");
    bool replay = resumeBeforeNotes(self);  // resume preview judges its own replay notes
    if (g_levelAuto) {
        if (!replay) autoplayTick(self);
        badgeTick();
    }
    // Critical mode runs the original itself (after autoplay set its flags), in one or two passes
    if (!critRunNotes(self, orig_NUM_Update, m, !replay)) orig_NUM_Update(self, m);
    if (replay) resumeAfterNotes();
    practiceTickFromUpdate();
}

static void showBadge() {
    Obj* root = overlayCanvas("PhiMeAutoBadge", 600);
    g_badge.set(root);
    // ivory pill with a hairline border, Clawd + muted label: quiet enough not to distract from the chart
    Obj* border = image(root, "pill", SPR_ROUND, rgb(0xE8E6DC, 0.9f), Vec2{0.5f, 0}, Vec2{0, 36}, Vec2{284, 44}, 22);
    Obj* pill = image(rt(border), "fill", SPR_ROUND, rgb(0xFAF9F5, 0.9f), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{280, 40},
                      20);
    Obj* prt = rt(pill);
    Obj* clawd = image(prt, "clawd", SPR_CLAWD, rgb(0xFFFFFF), Vec2{0, 0.5f}, Vec2{34, 1}, Vec2{40, 22});
    g_badgeClawd.set(clawd);
    g_badgeT = 0;
    text(prt, "CLAWD PLAYING…", 19, rgb(0xD97757), MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f},
         Vec2{22, 0}, Vec2{220, 36});
}

static void hk_PC_Start(Obj* self, const MethodInfo* m) {
    configLoad();
    g_levelAuto = g_cfg.autoplay || maskPreviewPending();  // the hide bar preview plays itself
    g_prevNow = -1e9f;
    LOG("level start, autoplay=%d", g_levelAuto);
    // autoplay and practice runs never save (sandbox set before the original Start, which may save)
    sandboxBegin(g_levelAuto || g_cfg.practice || maskPreviewPending());
    challengeBeforeLevel("level start");
    orig_PC_Start(self, m);
    courseLevelStart(self);  // after the original: it knows by then whether this is the game's own challenge
    if (g_levelAuto && !maskPreviewPending()) showBadge();  // (not on the hide bar preview)
    practiceLevelStart(self);
    hitsoundLevelStart(self);
    skipLevelStart(self);
    skinLevelStart(self);
    maskLevelStart(self);
}

bool autoplayInstall() {
    Il2CppClass* NUM = findClass("", "NoteUpdateManager");
    Il2CppClass* PC = findClass("", "ProgressControl");
    Il2CppClass* Click = findClass("", "ClickControl");
    Il2CppClass* Drag = findClass("", "DragControl");
    Il2CppClass* Hold = findClass("", "HoldControl");
    Il2CppClass* Flick = findClass("", "FlickControl");
    Il2CppClass* Note = findClass("", "ChartNote");
    if (!NUM || !PC || !Click || !Drag || !Hold || !Flick || !Note) return false;

    O.numPC = fieldOffset(NUM, "progressControl");
    O.numClick = fieldOffset(NUM, "clickControls");
    O.numDrag = fieldOffset(NUM, "dragControls");
    O.numHold = fieldOffset(NUM, "holdControls");
    O.numFlick = fieldOffset(NUM, "flickControls");
    O.pcNow = fieldOffset(PC, "nowTime");
    O.clickNote = fieldOffset(Click, "noteInfor");
    O.dragNote = fieldOffset(Drag, "noteInfor");
    O.dragJudged = fieldOffset(Drag, "isJudged");
    O.flickNote = fieldOffset(Flick, "noteInfor");
    O.holdNote = fieldOffset(Hold, "noteInfor");
    O.holdJudged = fieldOffset(Hold, "judged");
    O.holdSafe = fieldOffset(Hold, "_safeFrame");
    O.noteJudged = fieldOffset(Note, "isJudged");
    O.noteFlick = fieldOffset(Note, "isJudgedForFlick");
    O.noteTime = fieldOffset(Note, "realTime");
    const int all[] = {O.numPC, O.numClick, O.numDrag, O.numHold, O.numFlick, O.pcNow, O.clickNote, O.dragNote,
                       O.dragJudged, O.flickNote, O.holdNote, O.holdJudged, O.holdSafe, O.noteJudged,
                       O.noteFlick, O.noteTime};
    for (int v : all)
        if (v < 0) return false;

    // human-judge blocking (optional: autoplay still works without it, touches just aren't ignored)
    Il2CppClass* JC = findClass("", "JudgeControl");
    Il2CppClass* FM = findClass("", "FingerManagement");
    Il2CppClass* Fingers = findClass("", "Fingers");
    Il2CppClass* LI = findClass("", "LevelInformation");
    const MethodInfo* jcUpdate = nullptr;
    if (JC && FM && Fingers && LI) {
        O.jcFingers = fieldOffset(JC, "fingerManagement");
        O.jcPC = fieldOffset(JC, "progressControl");
        O.jcStop = fieldOffset(JC, "stopJudge");
        O.fmList = fieldOffset(FM, "fingers");
        O.fPhase = fieldOffset(Fingers, "phase");
        O.fFlick = fieldOffset(Fingers, "isNewFlick");
        O.pcLevelInfo = fieldOffset(PC, "levelInformation");
        O.liCanPause = fieldOffset(LI, "canPause");
        O.checkPause = findMethod(JC, "CheckPause", 1);
        const int jc[] = {O.jcFingers, O.jcPC, O.jcStop, O.fmList, O.fPhase, O.fFlick, O.pcLevelInfo, O.liCanPause};
        bool ok = O.checkPause != nullptr;
        for (int v : jc) ok = ok && v >= 0;
        if (ok) jcUpdate = findMethod(JC, "Update", 0);
    }
    if (!jcUpdate) LOG("warning: touch blocking unavailable");

    const MethodInfo* numUpdate = findMethod(NUM, "Update", 0);
    const MethodInfo* pcStart = findMethod(PC, "Start", 0);
    if (!numUpdate || !pcStart) return false;

    orig_NUM_Update = (VoidFn)hookMethod(numUpdate, (void*)hk_NUM_Update);
    orig_PC_Start = (VoidFn)hookMethod(pcStart, (void*)hk_PC_Start);
    if (jcUpdate) orig_JC_Update = (VoidFn)hookMethod(jcUpdate, (void*)hk_JC_Update);
    return true;
}
