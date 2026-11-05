// Autoplay runs reach the normal result screen, but nothing is saved:
//  - Save sandbox: every game save goes through SaveManagement -> PlayerPrefs (cloud sync uploads that same
//    data). While the autoplay level / its result screen is the active scene, PlayerPrefs writes are dropped.
//    The sandbox ends by itself the first time a write happens in any other scene.
//  - The result screen's auto-sync widget does not sync and says "Not Saved".
//  - The rating letter (φ) is shown upside down with Clawd sitting on top: Clawd played this one.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

// ---------------- save sandbox ----------------
static bool g_sandbox = false;
static int g_scenes[4];
static int g_sceneCount = 0;
static int g_blocked = 0;

void sandboxBegin(bool on) {
    g_sandbox = on;
    g_sceneCount = 0;
    g_blocked = 0;
    if (on) sandboxAddScene();
}

void sandboxAddScene() {
    if (!g_sandbox) return;
    int h = activeSceneHandle();
    for (int i = 0; i < g_sceneCount; i++)
        if (g_scenes[i] == h) return;
    if (g_sceneCount < 4) g_scenes[g_sceneCount++] = h;
}

static Il2CppClass *g_LOCk, *g_PCk;

bool sandboxActive() {
    if (!g_sandbox) return false;
    int h = activeSceneHandle();
    for (int i = 0; i < g_sceneCount; i++)
        if (g_scenes[i] == h) return true;
    // A scene we haven't seen yet: it still belongs to the run if it is the result screen (or a retried
    // level). Checked by object, not by Start order, so it works before any Start() of that scene runs.
    if (findObjectOfType(g_LOCk) || findObjectOfType(g_PCk)) {
        sandboxAddScene();
        return true;
    }
    LOG("save sandbox ended (%d writes were dropped)", g_blocked);
    g_sandbox = false;
    return false;
}

static bool drop(const char* what) {
    if (!sandboxActive()) return false;
    if (g_blocked++ == 0) LOG("autoplay: dropping save writes (%s)", what);
    return true;
}

typedef bool (*SetStrFn)(void*, void*);
typedef bool (*SetIntFn)(void*, int);
typedef void (*DelKeyFn)(void*);
typedef void (*DelAllFn)();
static SetStrFn o_setString;
static SetIntFn o_setInt;
static DelKeyFn o_deleteKey;
static DelAllFn o_deleteAll;

// icalls take no MethodInfo argument
static bool h_setString(void* k, void* v) { return drop("SetString") ? true : o_setString(k, v); }
static bool h_setInt(void* k, int v) { return drop("SetInt") ? true : o_setInt(k, v); }
static void h_deleteKey(void* k) {
    if (!drop("DeleteKey")) o_deleteKey(k);
}
static void h_deleteAll() {
    if (drop("DeleteAll")) return;
    LOG("savedata: the game is replacing the local save (PlayerPrefs.DeleteAll)");
    saveWipe(false);  // the game loading a (cloud) save: PhiMe's own keys survive, its phime.json is merged
    o_deleteAll();
    saveWipe(true);
}

// our own records (course mode) are written past the sandbox
static const MethodInfo* g_ppSetString;
void prefSetStringRaw(const char* key, const char* value) {
    if (o_setString) o_setString(str(key), str(value));
    else if (g_ppSetString) callm<void>(g_ppSetString, str(key), str(value));
}
void prefSetIntRaw(const char* key, int v) {
    if (o_setInt) o_setInt(str(key), v);
    else prefSetInt(key, v);
}

// ---------------- "Not Saved" sync widget ----------------
typedef void (*VoidFn)(Obj*, const MethodInfo*);
typedef Obj* (*ObjFn)(Obj*, const MethodInfo*);
static VoidFn orig_ASC_Start;
static ObjFn orig_LOC_Start;
static struct {
    int ascAnimator, ascTextSize, ascTextDisplay, ascResult;
    int locRatings;
} R;
static Handle g_sync;
static float g_syncT = -1;
static int g_syncStep = 0;

static void setSyncTexts(Obj* asc) {
    const int offs[] = {R.ascTextSize, R.ascTextDisplay, R.ascResult};
    for (int off : offs) {
        Obj* t = off >= 0 ? fld<Obj*>(asc, off) : nullptr;
        // a course-mode record is kept by PhiMe (phime.json) and goes up with the game's next cloud sync
        if (alive(t)) setText(t, courseLevelActive() && !autoplayLevelActive() ? "Saved, Uploads Next Sync" : "Not Saved");
    }
}

// The game's sync after a song uploads phime.json too (savedata.cpp): it waits until this play's judgements are in
// it (details.cpp reads them a moment after the result screen opens, 1.5 s at most).
static Handle g_ascWait;
static const MethodInfo* g_ascMi = nullptr;
static float g_ascWaited = 0;
static void resultFrame(void*);
static void ascWaitTick(float dt) {
    Obj* asc = (Obj*)g_ascWait.get();
    if (!asc) return;
    g_ascWaited += dt;
    // at least a moment (LevelOverControl.Start may come after us), then until the play is recorded, 3 s at most
    if (g_ascWaited < 0.2f || (detailsResultPending() && g_ascWaited < 3)) return;
    g_ascWait.clear();
    if (alive(asc)) orig_ASC_Start(asc, g_ascMi);
}

static void hk_ASC_Start(Obj* self, const MethodInfo* m) {
    if (!sandboxActive()) {
        g_ascWait.set(self);
        g_ascMi = m;
        g_ascWaited = 0;
        onEveryFrame(resultFrame);
        return;
    }
    // no cloud sync for an autoplay run: replay the widget's own "show -> result" animation with our text
    LOG("autoplay: cloud sync skipped, showing Not Saved");
    g_sync.set(self);
    g_syncT = 0;
    g_syncStep = 0;
    setSyncTexts(self);
}

static void syncTick(float dt) {
    if (g_syncT < 0) return;
    Obj* asc = (Obj*)g_sync.get();
    if (!alive(asc)) {
        g_sync.clear();
        g_syncT = -1;
        return;
    }
    g_syncT += dt;
    Obj* anim = R.ascAnimator >= 0 ? fld<Obj*>(asc, R.ascAnimator) : nullptr;
    if (g_syncStep == 0 && g_syncT >= 0.6f) {
        setTrigger(anim, "Show");
        g_syncStep = 1;
    } else if (g_syncStep == 1 && g_syncT >= 1.4f) {
        setSyncTexts(asc);
        setTrigger(anim, "Failed");
        g_syncStep = 2;
    } else if (g_syncStep == 2) {
        setSyncTexts(asc);  // keep our text even if something rewrites it
        if (g_syncT > 6) g_syncT = -1;
    }
}

// ---------------- upside-down φ with Clawd ----------------
static Handle g_loc, g_decorated, g_ratingClawd;

static void ratingTick() {
    Obj* loc = (Obj*)g_loc.get();
    if (!loc) return;
    if (!alive(loc)) {
        g_loc.clear();
        g_decorated.clear();
        g_ratingClawd.clear();
        return;
    }
    Il2CppArray* ratings = fld<Il2CppArray*>(loc, R.locRatings);
    if (!ratings) return;
    Obj** gos = (Obj**)arrayData(ratings);
    Obj* shown = nullptr;
    for (uintptr_t i = 0; i < ratings->max_length; i++)
        if (gos[i] && alive(gos[i]) && activeSelf(gos[i])) {
            shown = gos[i];
            break;
        }
    if (!shown) return;
    Obj* tr = goTransform(shown);
    setEuler(tr, Vec3{0, 0, 180});  // every frame: wins over the result animation
    if (g_decorated.get() == shown && alive((Obj*)g_ratingClawd.get())) return;

    // Clawd sits on the letter's (visual) top. The letter is rotated 180°, so its local bottom edge is the
    // top on screen; Clawd is rotated back so it stands upright. The letter's rect has some empty margin
    // above the glyph, so Clawd sinks into it (local +y = down on screen) to stand on the φ itself.
    Rect r{0, 0, 300, 300};
    rectOf(tr, &r);
    float w = r.w * 0.62f, h = w * 72.f / 128.f;
    float sink = h * 0.12f + r.h * 0.09f;
    Obj* c = image(tr, "PhiMeClawd", SPR_CLAWD, rgb(0xFFFFFF), Vec2{0.5f, 0}, Vec2{0, -h * 0.5f + sink},
                   Vec2{w, h});
    setEuler(rt(c), Vec3{0, 0, 180});
    g_decorated.set(shown);
    g_ratingClawd.set(c);
    LOG("result: rating flipped, Clawd added");
}

static void resultFrame(void*) {
    float dt = unscaledDt();
    ascWaitTick(dt);
    syncTick(dt);
    ratingTick();
}

static Obj* hk_LOC_Start(Obj* self, const MethodInfo* m) {
    PM_ONCE("hook alive: LevelOverControl.Start");
    critResultStart(self);
    courseResultStart(self);
    detailsResultStart(self);
    resAccStart(self);
    if (autoplayLevelActive()) {
        sandboxAddScene();  // the result scene belongs to the sandbox too
        g_loc.set(self);
        g_decorated.clear();
        g_ratingClawd.clear();
        onEveryFrame(resultFrame);
    } else {
        g_loc.clear();
    }
    return orig_LOC_Start(self, m);
}

bool resultInstall() {
    Il2CppClass* PP = findClass("UnityEngine", "PlayerPrefs");
    Il2CppClass* ASC = findClass("", "AutoSyncControl");
    Il2CppClass* LOC = findClass("", "LevelOverControl");
    g_LOCk = LOC;
    g_PCk = findClass("", "ProgressControl");
    if (!PP || !LOC || !g_PCk) return false;
    g_ppSetString = findMethod(PP, "SetString", 2);

    o_setString = (SetStrFn)hookIcall(findMethod(PP, "TrySetSetString", 2),
                                      "UnityEngine.PlayerPrefs::TrySetSetString(System.String,System.String)",
                                      (void*)h_setString);
    o_setInt = (SetIntFn)hookIcall(findMethod(PP, "TrySetInt", 2),
                                   "UnityEngine.PlayerPrefs::TrySetInt(System.String,System.Int32)", (void*)h_setInt);
    o_deleteKey = (DelKeyFn)hookIcall(findMethod(PP, "DeleteKey", 1), "UnityEngine.PlayerPrefs::DeleteKey(System.String)",
                                      (void*)h_deleteKey);
    o_deleteAll = (DelAllFn)hookIcall(findMethod(PP, "DeleteAll", 0), "UnityEngine.PlayerPrefs::DeleteAll()",
                                      (void*)h_deleteAll);
    // SetString carries every record/unlock write; without it the sandbox can't be trusted
    if (!o_setString) LOG("ERROR: save sandbox unavailable");

    R.locRatings = fieldOffset(LOC, "ratings");
    const MethodInfo* locStart = findMethod(LOC, "Start", 0);
    if (locStart && R.locRatings >= 0) orig_LOC_Start = (ObjFn)hookMethod(locStart, (void*)hk_LOC_Start);

    if (ASC) {
        R.ascAnimator = fieldOffset(ASC, "animator");
        R.ascTextSize = fieldOffset(ASC, "syncTextSize");
        R.ascTextDisplay = fieldOffset(ASC, "syncTextDisplay");
        R.ascResult = fieldOffset(ASC, "syncResult");
        const MethodInfo* ascStart = findMethod(ASC, "Start", 0);
        if (ascStart) orig_ASC_Start = (VoidFn)hookMethod(ascStart, (void*)hk_ASC_Start);
    }
    return o_setString != nullptr && orig_LOC_Start != nullptr;
}
