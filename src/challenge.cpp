// The game's own course (challenge) mode, song select: our dock is hidden there (it covered the 1st / 2nd / 3rd
// bar) and three 镜像 pills sit under the three song slots, one mirror switch per song.
//
// How the mirror reaches a level: LevelStartInfo.mirror (an auto property). Where the challenge keeps the
// LevelStartInfo of each song is not known for sure, so before every level (Loading.Start, and ProgressControl.Start
// before the original) every LevelStartInfo we can reach gets the switch of its song:
//   - fields of type LevelStartInfo (or bool fields named *mirror*) on GameInformation.challengeSongItems[i]
//   - GameInformation.getCollectionControl.levelStartInfo, for the song nowChallengeIndex points at
// The log says which of these exist; the first run tells what works.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

static struct {
    bool ok;
    Il2CppClass *GI, *LSI;
    FieldInfo* giMain;
    int giIndex, giItems, giGcc, gccLsi, lsiMirror;
    int ssCtrl, ctrlCovers;
    int listItems = -1, listSize = -1;
} H;

static bool g_mir[3];
static bool g_armed = false;  // song select was in challenge mode when the player left it
static int g_curSong = -1;    // the challenge song being entered (0..2), set by EnterLevel; -1 outside a challenge
static Handle g_ss, g_root, g_btn[3], g_fill[3], g_label[3], g_grp[3];
static float g_show = 0;  // the pills fade in / out with challenge mode (0..1)

static void loadSwitches() {
    int v = prefGetInt("PhiMeMod.challengeMirror", 0);
    for (int i = 0; i < 3; i++) g_mir[i] = (v >> i) & 1;
}
static void saveSwitches() {
    int v = 0;
    for (int i = 0; i < 3; i++) v |= (g_mir[i] ? 1 : 0) << i;
    prefSetIntRaw("PhiMeMod.challengeMirror", v);
}

static int listView(Obj* list, Obj*** out) {
    if (!list) return 0;
    const char* cn = il2cpp_class_get_name(il2cpp_object_get_class(list));
    size_t cl = strlen(cn);
    if (cl > 2 && cn[cl - 2] == '[' && cn[cl - 1] == ']') {
        *out = (Obj**)arrayData((Il2CppArray*)list);
        return (int)((Il2CppArray*)list)->max_length;
    }
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

// ---------------- song select ----------------
static Obj* control() {
    Obj* ss = (Obj*)g_ss.get();
    if (!alive(ss) || H.ssCtrl < 0) return nullptr;
    Obj* c = fld<Obj*>(ss, H.ssCtrl);
    return alive(c) ? c : nullptr;
}
bool challengeUiActive() {
    Obj* c = control();
    return c && activeInHierarchy(gameObject(c));
}

static void paint(int i) {
    Obj* f = (Obj*)g_fill[i].get();
    Obj* l = (Obj*)g_label[i].get();
    if (alive(f)) setColor(f, g_mir[i] ? rgb(0xD97757, 0.95f) : rgb(0x141413, 0.72f));
    if (alive(l)) setColor(l, rgb(g_mir[i] ? 0xFFFFFF : 0xE8E6DC));
}
static void onPill(void* u) {
    int i = (int)(intptr_t)u;
    g_mir[i] = !g_mir[i];
    saveSwitches();
    paint(i);
    LOG("challenge: song %d mirror=%d", i + 1, g_mir[i]);
}

static const Vec2 kPill{150, 50};
static void build() {
    Obj* root = overlayCanvas("PhiMeChallengeMirror", 29000);
    g_root.set(root);
    for (int i = 0; i < 3; i++) {
        Obj* border = image(root, "mirror", SPR_ROUND, rgb(0xE8E6DC, 0.5f), Vec2{0.5f, 0.5f}, Vec2{0, -2000},
                            Vec2{kPill.x + 3, kPill.y + 3}, 25, true);
        Obj* fill = image(rt(border), "fill", SPR_ROUND, rgb(0x141413, 0.72f), Vec2{0.5f, 0.5f}, Vec2{0, 0}, kPill, 24);
        Obj* l = text(rt(border), T("镜像", "Mirror"), 22, rgb(0xE8E6DC), MiddleCenter, true, Vec2{0.5f, 0.5f},
                      Vec2{0.5f, 0.5f}, Vec2{0, 1}, kPill);
        g_btn[i].set(border);
        g_grp[i].set(addCanvasGroup(gameObject(border)));
        setGroupAlpha((Obj*)g_grp[i].get(), 0);
        g_fill[i].set(fill);
        g_label[i].set(l);
        onClick(gameObject(border), border, onPill, (void*)(intptr_t)i);
        paint(i);
    }
}

// per frame (practice.cpp's song select tick): pills under the slots while challenge mode is up
void challengeSongSelectFrame() {
    bool on = challengeUiActive();
    if (control()) g_armed = on;  // remembered when song select goes away
    Obj* root = (Obj*)g_root.get();
    // fade in / out (0.2 s) instead of appearing / vanishing at once
    float step = unscaledDt() / 0.2f;
    g_show = on ? (g_show + step > 1 ? 1 : g_show + step) : (g_show - step < 0 ? 0 : g_show - step);
    if (!on && g_show <= 0) {
        if (alive(root)) setActive(gameObject(root), false);
        return;
    }
    if (!alive(root)) {
        if (!on) return;
        build();
        root = (Obj*)g_root.get();
        LOG("challenge: song select in course mode, 3 mirror pills (song mirrors %d %d %d)", g_mir[0], g_mir[1], g_mir[2]);
    }
    setActive(gameObject(root), true);
    Obj* c = control();
    Obj** covers = nullptr;
    int n = c && H.ctrlCovers >= 0 ? listView(fld<Obj*>(c, H.ctrlCovers), &covers) : 0;
    static int logged = 0;
    for (int i = 0; i < 3; i++) {
        Obj* b = (Obj*)g_btn[i].get();
        if (!alive(b)) continue;
        float x0, x1, y0, y1;
        Obj* cov = i < n ? covers[i] : nullptr;
        if (!alive(cov) || !screenBounds(rt(cov), &x0, &x1, &y0, &y1)) {
            setPos(rt(b), Vec2{0, -2000});
            continue;
        }
        Vec2 lo = screenToCanvas(Vec2{(x0 + x1) / 2, y0});
        setPos(rt(b), Vec2{lo.x, lo.y - 14 - kPill.y / 2});
        // as visible as its slot (the game fades / animates the slots), times our own fade
        float e = g_show * g_show * (3 - 2 * g_show);
        setGroupAlpha((Obj*)g_grp[i].get(), e * uiAlphaOf(rt(cov)));
        if (logged < 3) {
            logged++;
            LOG("challenge: slot %d on screen x %.0f..%.0f y %.0f..%.0f", i + 1, x0, x1, y0, y1);
        }
    }
}

void challengeSongSelectStart(Obj* ss) {
    if (!H.ok) return;
    g_ss.set(ss);
    g_root.clear();
    g_curSong = -1;  // back in song select: no challenge running
    loadSwitches();
}

// ---------------- before a level ----------------
static int setMirrors(Obj* o, bool v, int depth) {
    if (!o || depth > 1) return 0;
    int done = 0;
    Il2CppClass* k = il2cpp_object_get_class(o);
    if (k == H.LSI) {
        fld<bool>(o, H.lsiMirror) = v;
        return 1;
    }
    for (Il2CppClass* c = k; c; c = il2cpp_class_get_parent(c)) {
        void* it = nullptr;
        while (FieldInfo* f = il2cpp_class_get_fields(c, &it)) {
            if (il2cpp_field_get_flags(f) & 0x10) continue;  // static
            const Il2CppType* t = il2cpp_field_get_type(f);
            int tt = il2cpp_type_get_type(t);
            size_t off = il2cpp_field_get_offset(f);
            const char* fn = il2cpp_field_get_name(f);
            if (tt == IL2CPP_TYPE_BOOLEAN && fn) {
                bool m = false;
                for (const char* p = fn; *p && !m; p++) m = (p[0] == 'm' || p[0] == 'M') && !memcmp(p + 1, "irror", 5);
                if (m) {
                    fld<bool>(o, (int)off) = v;
                    done++;
                }
            } else if (tt == IL2CPP_TYPE_CLASS || tt == 0x15 /* GENERICINST: List<LevelStartInfo> */) {
                Obj* l = fld<Obj*>(o, (int)off);
                if (!l) continue;
                Il2CppClass* lk = il2cpp_object_get_class(l);
                if (lk == H.LSI) {
                    fld<bool>(l, H.lsiMirror) = v;
                    done++;
                } else if (tt == 0x15 && !memcmp(il2cpp_class_get_name(lk), "List`1", 7)) {  // only List<T>
                    Obj** e = nullptr;
                    int c = listView(l, &e);
                    for (int i = 0; i < c; i++)
                        if (e[i] && il2cpp_object_get_class(e[i]) == H.LSI) {
                            fld<bool>(e[i], H.lsiMirror) = v;
                            done++;
                        }
                } else if (depth == 0 && tt == IL2CPP_TYPE_CLASS) {
                    done += setMirrors(l, v, depth + 1);  // e.g. the coroutine's <>4__this (ChallengeModeControl)
                }
            }
        }
    }
    return done;
}

void challengeBeforeLevel(const char* when) {
    if (!H.ok || !g_armed) return;
    Obj* gi = nullptr;
    il2cpp_field_static_get_value(H.giMain, &gi);
    if (!gi) return;
    int idx = H.giIndex >= 0 ? fld<int>(gi, H.giIndex) : -1;
    Obj** items = nullptr;
    int n = H.giItems >= 0 ? listView(fld<Obj*>(gi, H.giItems), &items) : 0;
    int viaItems = 0, viaGcc = 0;
    for (int i = 0; i < n && i < 3; i++) viaItems += setMirrors(items[i], g_mir[i], 0);
    Obj* gcc = H.giGcc >= 0 ? fld<Obj*>(gi, H.giGcc) : nullptr;
    Obj* lsi = gcc && H.gccLsi >= 0 ? fld<Obj*>(gcc, H.gccLsi) : nullptr;
    if (lsi && idx >= 0 && idx < 3) {
        fld<bool>(lsi, H.lsiMirror) = g_mir[idx];
        viaGcc = 1;
    }
    LOG("challenge: %s, song %d of %d, mirror %d -> %d item fields, %s", when, idx + 1, n, idx >= 0 && idx < 3 ? g_mir[idx] : -1,
        viaItems, viaGcc ? "current LevelStartInfo set" : "no current LevelStartInfo");
}

// The challenge's own scene enters each level with ChallengeModeControl.<EnterLevel>d__47: whatever LevelStartInfo
// it (or its control) holds gets this song's switch, after every step of the coroutine.
typedef bool (*MoveNextFn)(Obj*, const MethodInfo*);
static MoveNextFn orig_enterNext;
static bool hk_enterNext(Obj* self, const MethodInfo* m) {
    bool more = orig_enterNext(self, m);
    Obj* gi = nullptr;
    if (H.giMain) il2cpp_field_static_get_value(H.giMain, &gi);
    int idx = gi && H.giIndex >= 0 ? fld<int>(gi, H.giIndex) : -1;
    if (idx >= 0 && idx < 3) g_curSong = idx;  // nowChallengeIndex moves on before the level starts
    if (idx >= 0 && idx < 3 && self) {
        int n = setMirrors(self, g_mir[idx], 0);
        static int logged = 0;
        if (logged++ < 6) LOG("challenge: entering song %d, mirror %d -> %d LevelStartInfo field(s)", idx + 1, g_mir[idx], n);
    }
    return more;
}

// The level's own LevelControl.<Start>d__46 holds the LevelStartInfo it plays (<levelStartInfo>5__3): during a
// challenge it gets the switch of the song EnterLevel last entered, before and after each step of the coroutine.
static MoveNextFn orig_levelNext;
static int g_lsiField = -1;
static bool hk_levelNext(Obj* self, const MethodInfo* m) {
    auto apply = [&](const char* when) {
        if (g_curSong < 0 || g_curSong > 2 || g_lsiField < 0 || !self) return;
        Obj* lsi = fld<Obj*>(self, g_lsiField);
        if (!lsi || il2cpp_object_get_class(lsi) != H.LSI) return;
        bool& mir = fld<bool>(lsi, H.lsiMirror);
        if (mir != g_mir[g_curSong]) {
            LOG("challenge: song %d level info mirror %d -> %d (%s a step)", g_curSong + 1, mir, g_mir[g_curSong], when);
            mir = g_mir[g_curSong];
        }
    };
    apply("before");
    bool more = orig_levelNext(self, m);
    apply("after");
    return more;
}
bool challengeInstall() {
    H.GI = findClass("", "GameInformation");
    H.LSI = findClass("", "LevelStartInfo");
    Il2CppClass* SS = findClass("", "SongSelector");
    Il2CppClass* GCC = findClass("", "GetCollectionControl");
    Il2CppClass* CTRL = findClass("Phigros2.SongSelector.Scripts", "ChallengeSongSelectorControl");
    if (!H.GI || !H.LSI || !SS) return false;
    H.giMain = il2cpp_class_get_field_from_name(H.GI, "_main");
    H.giIndex = fieldOffset(H.GI, "nowChallengeIndex");
    H.giItems = fieldOffset(H.GI, "challengeSongItems");
    H.giGcc = fieldOffset(H.GI, "getCollectionControl");
    H.gccLsi = GCC ? fieldOffset(GCC, "levelStartInfo") : -1;
    H.lsiMirror = fieldOffset(H.LSI, "<mirror>k__BackingField");
    H.ssCtrl = fieldOffset(SS, "challengeSongSelectorControl");
    H.ctrlCovers = CTRL ? fieldOffset(CTRL, "songCoverImages") : -1;
    H.ok = H.giMain && H.lsiMirror >= 0 && H.ssCtrl >= 0;
    Il2CppClass* CMC = findClass("Phigros2.ChallengeMode.Scripts", "ChallengeModeControl");
    Il2CppClass* EL = CMC ? findNestedPrefix(CMC, "<EnterLevel>d__") : nullptr;
    const MethodInfo* next = EL ? findMethod(EL, "MoveNext", 0) : nullptr;
    if (H.ok && next) orig_enterNext = (MoveNextFn)hookMethod(next, (void*)hk_enterNext);
    LOG("challenge: EnterLevel hook %s", orig_enterNext ? "on" : "MISSING");
    Il2CppClass* LC = findClass("", "LevelControl");
    Il2CppClass* LS = LC ? findNestedPrefix(LC, "<Start>d__") : nullptr;
    g_lsiField = LS ? fieldOffset(LS, "<levelStartInfo>5__3") : -1;
    const MethodInfo* lnext = LS ? findMethod(LS, "MoveNext", 0) : nullptr;
    if (H.ok && lnext && g_lsiField >= 0) orig_levelNext = (MoveNextFn)hookMethod(lnext, (void*)hk_levelNext);
    LOG("challenge: level start hook %s (field %d)", orig_levelNext ? "on" : "MISSING", g_lsiField);
    LOG("challenge: %s (index %d, items %d, collection info %d/%d, covers %d)", H.ok ? "ready" : "off", H.giIndex,
        H.giItems, H.giGcc, H.gccLsi, H.ctrlCovers);
    return H.ok;
}
