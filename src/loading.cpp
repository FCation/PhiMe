// Faster loading screen: the "Loading" scene shown when entering / leaving a chapter (random illustration +
// a tip). Song loading (SongSelector -> level) is a different path and is left alone.
//
// How Loading (4.0.x) spends its time, none of it waiting for real work:
//   LoadImage coroutine : shows a random cover, then WaitForSeconds(0.9 + random 0..0.6) BEFORE it even starts
//                         SceneManager.LoadSceneAsync(next scene)
//   OnGUI               : once the async load reaches 0.9 (loaded, waiting for activation) it sets
//                         startTime = realtimeSinceStartup (when IsZero(startTime)) and only activates the
//                         scene once realtimeSinceStartup - startTime >= 1
//   LoadOver            : the 0.67 s cut-out transition (kept)
// We shorten the first wait (the yielded WaitForSeconds is edited right after MoveNext returns, before Unity
// reads it) and pre-set startTime far in the past, so the scene activates as soon as it has loaded.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

typedef void (*VoidFn)(Obj*, const MethodInfo*);
typedef bool (*MoveNextFn)(Obj*, const MethodInfo*);
static VoidFn orig_Start;
static MoveNextFn orig_LoadImage;

static struct {
    Il2CppClass* WaitForSeconds;
    int startTime, liCurrent, wfsSeconds;
} L;

static const float kPreLoadWait = 0.1f;  // was 0.9 - 1.5 s

static void hk_Start(Obj* self, const MethodInfo* m) {
    orig_Start(self, m);
    configLoad();
    challengeBeforeLevel("loading");  // the game's course mode: this song's mirror switch
    if (!g_cfg.fastLoad) return;
    // OnGUI only resets startTime when it is ~0, so this makes "time since loaded" >= 1 s right away
    fld<float>(self, L.startTime) = -10000.f;
    LOG("loading: fast (activate as soon as loaded)");
}

static bool hk_LoadImage(Obj* self, const MethodInfo* m) {
    bool more = orig_LoadImage(self, m);
    if (!more || !g_cfg.fastLoad) return more;
    Obj* cur = fld<Obj*>(self, L.liCurrent);
    if (cur && il2cpp_object_get_class(cur) == L.WaitForSeconds) {
        float& s = fld<float>(cur, L.wfsSeconds);
        if (s > kPreLoadWait) {
            LOG("loading: wait %.2fs -> %.2fs", s, kPreLoadWait);
            s = kPreLoadWait;
        }
    }
    return more;
}

bool loadingInstall() {
    Il2CppClass* Loading = findClass("", "Loading");
    L.WaitForSeconds = findClass("UnityEngine", "WaitForSeconds");
    Il2CppClass* LI = findNestedPrefix(Loading, "<LoadImage>d__");
    if (!Loading || !L.WaitForSeconds || !LI) return false;
    L.startTime = fieldOffset(Loading, "startTime");
    L.liCurrent = fieldOffset(LI, "<>2__current");
    L.wfsSeconds = fieldOffset(L.WaitForSeconds, "m_Seconds");
    if (L.startTime < 0 || L.liCurrent < 0 || L.wfsSeconds < 0) return false;
    orig_Start = (VoidFn)hookMethod(findMethod(Loading, "Start", 0), (void*)hk_Start);
    orig_LoadImage = (MoveNextFn)hookMethod(findMethod(LI, "MoveNext", 0), (void*)hk_LoadImage);
    return orig_Start && orig_LoadImage;
}
