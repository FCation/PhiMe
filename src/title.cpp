// The title screen (SplashScene, "tap to start"): the mod's version right after the game's version line
// (SplashScene.gameRuntimeInfo). The game may fill that text after Start, so it is checked every frame while
// the screen is up and the suffix is put back whenever the text is replaced.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

typedef void (*VoidFn)(Obj*, const MethodInfo*);
static VoidFn orig_Splash_Start;
static int g_info = -1;
static const MethodInfo* g_hOverflow = nullptr;
static Handle g_splash;
static Il2CppString* g_done = nullptr;  // the string we last wrote

static void titleFrame(void*) {
    if (!g_splash.h) return;
    Obj* sp = (Obj*)g_splash.get();
    if (!alive(sp)) {
        g_splash.clear();
        g_done = nullptr;
        return;
    }
    Obj* t = fld<Obj*>(sp, g_info);
    if (!alive(t)) return;
    Il2CppString* cur = textStr(t);
    if (!cur || cur == g_done) return;
    char b[1024];
    int n = utf8(cur, b, sizeof b - 64);
    if (n <= 0) return;  // not filled in yet
    for (int i = 0; i + 5 <= n; i++)
        if (!memcmp(b + i, "PhiMe", 5)) {  // already there
            g_done = cur;
            return;
        }
    // after the first line (the version), the rest as it was
    int cut = 0;
    while (cut < n && b[cut] != '\n') cut++;
    char out[1100];
    snprintf(out, sizeof out, "%.*s  ·  PhiMe Mod v" PM_VERSION "%s", cut, b, b + cut);
    if (g_hOverflow) callm<void>(g_hOverflow, t, 1);  // never wrap the longer line
    setText(t, out);
    g_done = textStr(t);
    LOG("title: version line '%.*s' + PhiMe Mod v" PM_VERSION, cut, b);
}

// No tutorial: SplashScene.NextScene (tapping the title) starts the "Introduction" chart while SaveManagement int
// "IsFirstRun" is 0, and only the tutorial's own result screen sets it to 1 (it can't be skipped in the game).
// On a fresh profile we set it before the tap. Nothing else is recorded by finishing the tutorial.
static void skipTutorial() {
    Il2CppClass* SM = findClass("", "SaveManagement");
    static const char* const sig[] = {"String", "Int32"};
    const MethodInfo* load = SM ? findMethodSig(SM, "LoadInt", 2, sig) : nullptr;
    const MethodInfo* save = SM ? findMethodSig(SM, "SaveInt", 2, sig) : nullptr;
    if (!load || !save) {
        LOG("title: SaveManagement.LoadInt / SaveInt not found, the tutorial is left as it is");
        return;
    }
    Il2CppString* key = str("IsFirstRun");
    int def = 0;
    void* a[] = {key, &def};
    bool ok = false;
    Obj* r = invokeObj(load, nullptr, a, &ok);
    if (!ok || !r || *(int*)il2cpp_object_unbox(r) != 0) return;
    int one = 1;
    void* b[] = {key, &one};
    if (invokeSafe(save, nullptr, b)) {
        prefSave();
        LOG("title: first start, the tutorial is skipped (IsFirstRun = 1)");
    }
}

static void hk_Splash_Start(Obj* self, const MethodInfo* m) {
    skipTutorial();
    orig_Splash_Start(self, m);
    g_splash.set(self);
    g_done = nullptr;
    onEveryFrame(titleFrame);
}

bool titleInstall() {
    Il2CppClass* SS = findClass("", "SplashScene");
    Il2CppClass* Text = findClass("UnityEngine.UI", "Text");
    const MethodInfo* start = SS ? findMethod(SS, "Start", 0) : nullptr;
    g_info = SS ? fieldOffset(SS, "gameRuntimeInfo") : -1;
    g_hOverflow = Text ? findMethod(Text, "set_horizontalOverflow", 1) : nullptr;
    if (!start || g_info < 0) {
        LOG("title: SplashScene.gameRuntimeInfo not found, no version on the title screen");
        return false;
    }
    orig_Splash_Start = (VoidFn)hookMethod(start, (void*)hk_Splash_Start);
    return orig_Splash_Start != nullptr;
}
