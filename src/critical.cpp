// "区分大小 P": Critical Perfect (|offset| <= half the game's Perfect window: 40 ms, 20 ms in course mode) vs Perfect.
//   - accuracy unchanged; the in-level score shows +1 per Critical (an all-Critical run reads 1000000 + notes).
//     The game's own score (results, records, cloud) is left as it is.
//   - hit effects: Critical keeps the game's golden Perfect effect, Perfect gets an orange copy, Good unchanged
//   - judgement counts get a fourth number; the judge line shows AP+ (gold) / AP (orange) / FC (blue)
//
// How the game judges (4.0.x). NoteUpdateManager.Update: every judge line's UpdateInfo, then for the click,
// drag, hold and flick lists in turn: culling, NoteMove(), and Judge() for notes due within 2 s; a note whose
// Judge() returns true is destroyed and removed.
//   ClickControl.Judge : judged = isJudged | noteInfor.isJudged (set by a touch); offset = realTime - nowTime;
//                        Perfect when |offset| < JudgeControl.PerfectTimeRange (static) ->
//                        ScoreControl.Perfect(code, -offset, position, false) -> Instantiate(perfectJudge)
//   Drag / Flick       : no timing, always Perfect when hit -> Critical
//   HoldControl.Judge  : the head is judged the same way (offset kept in _judgeTime); the Perfect is scored when
//                        the hold ends (judgeOver); its sparks come from its own `perfect` prefab
// Perfect() and the Judge() calls are direct branches (no hook). Instead we run the game's own Update so that
// every Perfect comes out right:
//   - pass 1: the whole update as usual, with the game's Perfect effect, except that taps that are about to be
//     a plain Perfect have their touch flags held back for the moment
//   - pass 2 (only on frames that have such taps): flags back, drag / hold / flick lists emptied for the call,
//     ScoreControl.perfectJudge = the orange copy -> only those taps are judged. They are within the Perfect
//     window, so pass 1 never misses them; NoteMove / UpdateInfo just run twice with the same time.
//   - holds about to have their head judged get the matching `perfect` prefab before the update, so even the
//     first spark is right
// Criticals are counted from what each pass scored: pass 1 Perfects minus holds ending with a plain-Perfect
// head; pass 2 Perfects are all plain.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

static float perfectRange();
// half the game's own Perfect window, as it stands this frame: 80 -> 40 ms normally; in course mode (ours or
// the game's challenge mode) 40 ms + half a frame -> 20 ms + a quarter frame
static float critWindow() { return perfectRange() * 0.5f; }
#define kCritWindow critWindow()

static struct {
    bool ok;
    int numPC, numLists[4], pcNow, pcScore, pcJudge, pcLevelInfo;
    int ccNote, ccJudged, ccScore, noteTime, noteJudged;
    int hNote, hIsJudged, hJudged, hJudgeOver, hPerfect, hJudgeTime, hPrefab, hMissed;
    int scPerfect, scGood, scBad, scMiss, scPrefab;
    int jcLines, jlRenderer, jlShowingPic, liApFc;
    FieldInfo* perfectRange;
    Il2CppClass *Renderer, *Material;
    const MethodInfo *srGetColor, *srSetColor, *getShared, *setMat, *matCtor, *matHas, *matGet, *matSet;
    const MethodInfo *matGetTex, *matSetTex;  // optional (stars)
    int listItems = -1, listSize = -1;
} C;

// ---- per level ----
static Handle g_sc, g_holder, g_basePrefab;
static int g_crit = 0, g_lastPerfect = 0;
static int g_resPerfect = -1;  // the Perfect count of the last level played in Critical mode (for its results)
struct Tinted {
    Handle prefab, crit, perf;  // a game prefab, its brighter gold copy and its orange copy
};
// material colour multipliers on top of the game's (already golden) Perfect effect. Above 1 = brighter.
static const Color kCritTint{1.15f, 1.12f, 1.30f, 1.15f};
static const Color kPerfTint{1.0f, 0.83f, 0.60f, 1.0f};  // a light orange-yellow
// the game's Perfect effect sprite colour; effect colour = sprite x tint, which the judge line copies
static const Color kFxSprite{1.0f, 0.93f, 0.63f, 1.0f};
// (scaled so its brightest channel is 1: same hue as the effect, as bright as a line can be)
static Color fxColour(Color k) {
    float r = kFxSprite.r * k.r, g = kFxSprite.g * k.g, b = kFxSprite.b * k.b;
    float m = r > g ? (r > b ? r : b) : (g > b ? g : b);
    return m > 0 ? Color{r / m, g / m, b / m, 1} : Color{1, 1, 1, 1};
}
// diagnostics, summarised in the log now and then
static struct {
    int held, pass2, pass2Perfect, holdCrit, holdPlain, tapsLogged;
    float lastLog;
} D;
static Tinted g_tint[4];
static int g_tintN = 0;
static Handle g_mats[16];
static int g_matN = 0;

bool critEnabled() { return g_cfg.critical && C.ok && !g_cfg.skin; }  // skins have no Critical / Perfect variants
int critCount() { return g_crit; }

static bool listOf(Obj* list, Obj*** out, int* size) {
    *size = 0;
    if (!list) return false;
    if (C.listItems < 0) {
        Il2CppClass* k = il2cpp_object_get_class(list);
        C.listItems = fieldOffset(k, "_items");
        C.listSize = fieldOffset(k, "_size");
        if (C.listItems < 0 || C.listSize < 0) return false;
    }
    Il2CppArray* items = fld<Il2CppArray*>(list, C.listItems);
    int n = fld<int>(list, C.listSize);
    if (!items || n < 0 || (uintptr_t)n > items->max_length) return false;
    *out = (Obj**)arrayData(items);
    *size = n;
    return true;
}

// List<T> or T[]
static int elements(Obj* coll, Obj*** out) {
    if (!coll) return 0;
    const char* n = il2cpp_class_get_name(il2cpp_object_get_class(coll));
    int len = (int)strlen(n);
    if (len > 2 && n[len - 2] == '[' && n[len - 1] == ']') {
        *out = (Obj**)arrayData((Il2CppArray*)coll);
        return (int)((Il2CppArray*)coll)->max_length;
    }
    int size;
    return listOf(coll, out, &size) ? size : 0;
}

static float perfectRange() {
    float r = 0.08f;
    if (C.perfectRange) il2cpp_field_static_get_value(C.perfectRange, &r);
    return r > 0.001f && r < 1.f ? r : 0.08f;
}

// ---- the orange effect ----
static void tintMaterial(Obj* mat, const char* prop, Color k) {
    Il2CppString* p = str(prop);
    if (!callm<bool>(C.matHas, mat, p)) return;
    Color c = callm<Color>(C.matGet, mat, p);
    callm<void>(C.matSet, mat, p, Color{c.r * k.r, c.g * k.g, c.b * k.b, c.a * k.a});
}

// A skin's colours on a renderer of the game's hit effect (its sparks): a tinted copy of its material, made once per
// material and colour (the copies are reused by every later spark)
struct SkinMat {
    Handle src, copy;
    uint32_t key;
};
static SkinMat g_skinMats[16];
static int g_skinMatN = 0;
void critTintRenderer(Obj* r, Color k) {
    if (!C.ok || !C.getShared || !C.setMat || !C.matCtor || !alive(r)) return;
    Obj* cur = callm<Obj*>(C.getShared, r);
    if (!cur) return;
    uint32_t key = ((uint32_t)(k.r * 255) << 24) | ((uint32_t)(k.g * 255) << 16) | ((uint32_t)(k.b * 255) << 8) |
                   (uint32_t)(k.a * 255);
    for (int i = 0; i < g_skinMatN; i++)  // a copy destroyed after all: forget it, a new one is made
        if (!alive((Obj*)g_skinMats[i].copy.get()) || !alive((Obj*)g_skinMats[i].src.get())) {
            g_skinMats[i].src.clear();
            g_skinMats[i].copy.clear();
            g_skinMats[i] = g_skinMats[--g_skinMatN];
            i--;
        }
    for (int i = 0; i < g_skinMatN; i++) {
        if (g_skinMats[i].copy.get() == cur) return;  // already ours
        if (g_skinMats[i].src.get() == cur && g_skinMats[i].key == key) {
            callm<void>(C.setMat, r, (Obj*)g_skinMats[i].copy.get());
            return;
        }
    }
    if (g_skinMatN >= 16) return;
    Obj* mat = il2cpp_object_new(C.Material);
    callm<void>(C.matCtor, mat, cur);
    // the sparks' gold is their own particle colour (the material is white, 0.28.4 log): k is the skin colour over
    // that gold, so the product is the skin colour
    tintMaterial(mat, "_Color", k);
    tintMaterial(mat, "_TintColor", k);
    // ours, referenced by no scene: without this a scene change unloads it and the next level's sparks draw the
    // missing-material magenta
    static const MethodInfo* hide = nullptr;
    if (!hide) hide = findMethod(findClass("UnityEngine", "Object"), "set_hideFlags", 1);
    if (hide) callm<void>(hide, mat, 32);  // HideFlags.DontUnloadUnusedAsset
    SkinMat& m = g_skinMats[g_skinMatN++];
    m.src.set(cur);
    m.copy.set(mat);
    m.key = key;
    callm<void>(C.setMat, r, mat);
    LOG("critical: skin colour %.2f %.2f %.2f on a %s material", k.r, k.g, k.b,
        il2cpp_class_get_name(il2cpp_object_get_class(r)));
}
// A copy of a prefab with its own tinted materials, parked under an inactive holder: it never runs itself,
// the clones the game makes of it do.
// star = the flying square bits (the particle system's texture) become four-pointed stars
static Obj* tintedCopy(Obj* prefab, Color k, const char* what, bool star) {
    Obj* holder = (Obj*)g_holder.get();
    if (!alive(holder)) return nullptr;
    Obj* copy = instantiate(prefab, holder);
    if (!alive(copy)) return nullptr;
    const char* kn = il2cpp_class_get_name(il2cpp_object_get_class(copy));
    Obj* go = strcmp(kn, "GameObject") == 0 ? copy : gameObject(copy);
    Il2CppArray* rs = componentsInChildren(go, C.Renderer);
    int n = rs ? (int)rs->max_length : 0, done = 0, stars = 0;
    for (int i = 0; i < n; i++) {
        Obj* r = (Obj*)arrayData(rs)[i];
        Obj* src = r ? callm<Obj*>(C.getShared, r) : nullptr;
        if (!src) continue;
        Obj* mat = il2cpp_object_new(C.Material);
        callm<void>(C.matCtor, mat, src);
        tintMaterial(mat, "_Color", k);
        tintMaterial(mat, "_TintColor", k);
        const char* rk = il2cpp_class_get_name(il2cpp_object_get_class(r));
        Il2CppString* tn = str("_MainTex");
        bool hasTex = callm<bool>(C.matHas, mat, tn);
        Obj* tex = hasTex && C.matGetTex ? callm<Obj*>(C.matGetTex, mat, tn) : nullptr;
        char texName[64] = "-";
        if (tex) objName(tex, texName, sizeof texName);
        bool particles = strcmp(rk, "ParticleSystemRenderer") == 0;
        if (star && particles && hasTex && C.matSetTex) {
            Obj* st = starTexture();
            if (st) {
                callm<void>(C.matSetTex, mat, tn, st);
                stars++;
            }
        }
        if (g_tintN <= 1)  // once per level is enough
            LOG("critical: %s copy, renderer %d is a %s, texture %s", what, i, rk, texName);
        callm<void>(C.setMat, r, mat);
        if (g_matN < 16) g_mats[g_matN++].set(mat);
        done++;
    }
    char pn[64] = "?";
    objName(prefab, pn, sizeof pn);
    LOG("critical: %s copy of %s, %d renderers tinted, %d with stars", what, pn, done, stars);
    return go;
}

// the game prefab behind one of ours (or itself)
static Obj* original(Obj* p) {
    for (int i = 0; i < g_tintN; i++)
        if (g_tint[i].perf.get() == p || g_tint[i].crit.get() == p) return (Obj*)g_tint[i].prefab.get();
    return p;
}
static Tinted* tinted(Obj* prefab) {
    prefab = original(prefab);
    if (!alive(prefab)) return nullptr;
    for (int i = 0; i < g_tintN; i++)
        if (g_tint[i].prefab.get() == prefab) return &g_tint[i];
    if (g_tintN >= 4) return nullptr;
    Tinted& t = g_tint[g_tintN++];
    t.prefab.set(prefab);
    t.crit.set(tintedCopy(prefab, kCritTint, "bright gold", true));
    t.perf.set(tintedCopy(prefab, kPerfTint, "orange", false));
    return &t;
}
// the effect for a Critical / a plain Perfect, falling back to the game's own
static Obj* effectFor(Obj* prefab, bool crit) {
    Tinted* t = tinted(prefab);
    Obj* o = t ? (Obj*)(crit ? t->crit.get() : t->perf.get()) : nullptr;
    return alive(o) ? o : original(prefab);
}
// the pair an object belongs to (a game prefab or one of our copies), without making one
static Tinted* familyOf(Obj* o) {
    for (int i = 0; i < g_tintN; i++)
        if (g_tint[i].prefab.get() == o || g_tint[i].crit.get() == o || g_tint[i].perf.get() == o) return &g_tint[i];
    return nullptr;
}

// ---- every Instantiate goes through Object.Internal_CloneSingle (an icall) ----
// While pass 2 runs, any clone of the Perfect effect (whatever ScoreControl hands over) is made from the orange
// copy. A few clones of each kind are also logged, with their colours now and a moment later.
typedef Obj* (*CloneFn)(Obj*);
static CloneFn o_clone;
static int g_fxMode = 0;  // 0 = leave alone, 1 = pass 1 (Critical), 2 = pass 2 (plain Perfect)
struct Watched {
    Handle clone;
    int kind;  // 0 = game prefab, 1 = gold copy, 2 = orange copy
    float born;
    bool done;
};
static Watched g_watch[6];
static int g_watchN = 0;

static void logColours(Obj* go, const char* when, int kind) {
    static const char* const kinds[] = {"game", "gold", "orange"};
    Il2CppArray* rs = alive(go) ? componentsInChildren(go, C.Renderer) : nullptr;
    int n = rs ? (int)rs->max_length : 0;
    for (int i = 0; i < n && i < 3; i++) {
        Obj* r = (Obj*)arrayData(rs)[i];
        if (!alive(r)) continue;
        Obj* mat = callm<Obj*>(C.getShared, r);
        Color mc{-1, -1, -1, -1};
        if (mat && callm<bool>(C.matHas, mat, str("_Color"))) mc = callm<Color>(C.matGet, mat, str("_Color"));
        const char* rk = il2cpp_class_get_name(il2cpp_object_get_class(r));
        Color sc{-1, -1, -1, -1};
        if (strcmp(rk, "SpriteRenderer") == 0) sc = callm<Color>(C.srGetColor, r);
        LOG("critical: %s %s clone, %s #%d: material %p _Color %.2f %.2f %.2f %.2f, sprite %.2f %.2f %.2f %.2f", when,
            kinds[kind], rk, i, (void*)mat, mc.r, mc.g, mc.b, mc.a, sc.r, sc.g, sc.b, sc.a);
    }
}

// Taps get their effect from ScoreControl.Perfect, which does not reliably pick up our perfectJudge swap, so
// the copy is chosen here, at the clone itself (holds already carry the right copy in their own `perfect`):
//   pass 1: the game's own Perfect prefab -> the gold copy (taps, drags, flicks are Criticals here);
//           our copies pass through untouched (a hold with a plain-Perfect head keeps its orange)
//   pass 2: only plain-Perfect taps run -> everything of the family becomes the orange copy
static Obj* cloneInner(Obj* src);
static uint64_t g_cloneOrigNs = 0;  // the game's own clone time inside this hook (not ours)
static Obj* origClone(Obj* src) {
    uint64_t t0 = perfNow();
    Obj* c = o_clone(src);
    g_cloneOrigNs += perfNow() - t0;
    return c;
}
static Obj* hk_clone(Obj* src) {
    uint64_t t0 = perfNow();
    g_cloneOrigNs = 0;
    Obj* c = cloneInner(src);
    uiFadeOnClone(src, c);  // the game's scene wipe goes above our canvases
    uint64_t total = perfNow() - t0;
    perfAdd(PF_CLONE, total > g_cloneOrigNs ? total - g_cloneOrigNs : 0);
    return c;
}
static Obj* cloneInner(Obj* src) {
    if (skinActive()) {  // a skin replaces the hit effects (Critical is off then)
        Obj* c = origClone(src);
        skinOnClone(src, c);
        return c;
    }
    Tinted* t = (g_fxMode && src && critEnabled()) ? familyOf(src) : nullptr;
    if (t) {
        Obj* want = nullptr;
        if (g_fxMode == 2) want = (Obj*)t->perf.get();
        else if (src == t->prefab.get()) want = (Obj*)t->crit.get();
        if (want && alive(want)) src = want;
    }
    Obj* c = origClone(src);
    if (t && c) {
        int kind = src == t->perf.get() ? 2 : src == t->crit.get() ? 1 : 0;
        int same = 0;
        for (int i = 0; i < g_watchN; i++)
            if (g_watch[i].kind == kind) same++;
        if (same < 2 && g_watchN < 6) {
            Watched& w = g_watch[g_watchN++];
            w.clone.set(c);
            w.kind = kind;
            w.born = timeSinceLevelLoad();
            w.done = false;
            LOG("critical: pass %d clones the %s effect", g_fxMode, kind == 2 ? "orange" : kind == 1 ? "gold" : "game's");
            logColours(c, "new", kind);
        }
    }
    return c;
}

static void watchTick() {
    float now = timeSinceLevelLoad();
    for (int i = 0; i < g_watchN; i++) {
        Watched& w = g_watch[i];
        if (w.done || now - w.born < 0.15f) continue;
        w.done = true;
        logColours((Obj*)w.clone.get(), "0.15 s old", w.kind);
    }
}

static void levelReset(Obj* sc) {
    g_sc.set(sc);
    g_basePrefab.set(fld<Obj*>(sc, C.scPrefab));
    g_crit = 0;
    g_lastPerfect = fld<int>(sc, C.scPerfect);
    for (int i = 0; i < g_tintN; i++) {
        g_tint[i].prefab.clear();
        g_tint[i].crit.clear();
        g_tint[i].perf.clear();
    }
    D = {};
    g_tintN = 0;
    for (int i = 0; i < g_watchN; i++) g_watch[i].clone.clear();
    g_watchN = 0;
    for (int i = 0; i < g_matN; i++) {  // last level's materials (not owned by any scene)
        Obj* m = (Obj*)g_mats[i].get();
        if (alive(m)) destroy(m);
        g_mats[i].clear();
    }
    g_matN = 0;
    Obj* h = newUI("PhiMeCriticalTemplates", nullptr);  // its RectTransform
    setActive(gameObject(h), false);
    g_holder.set(h);
    LOG("critical: level start, Critical window %.0f ms, Perfect %.0f ms", kCritWindow * 1000,
        perfectRange() * 1000);
}

// ---- the update ----
struct Held {
    Obj *cc, *note;
    bool ccFlag, noteFlag;
};
static Held g_held[64];
struct HoldSnap {
    Obj* hold;
};
static HoldSnap g_holds[2048];

static float absf(float v) { return v < 0 ? -v : v; }

static void paintLines(Obj* pc, Obj* sc, const Color* apFc = nullptr);

// Pass 2 runs the game's update over the held-back taps only: for the call, the click list's backing array is a
// small one holding just them (the other lists are emptied), and whatever the game removed from it (judged taps
// are destroyed and removed in the same update) is then removed from the real list too. 1.2.0 and older ran it
// over every tap of the chart: the whole note update twice on every frame with a plain Perfect.
static Handle g_p2Arr;
static int g_listVersion = -2;
static Il2CppArray* pass2Array(Il2CppArray* real) {
    if (!real) return nullptr;
    Il2CppArray* a = (Il2CppArray*)g_p2Arr.get();
    if (a) return a;
    static bool tried = false;
    if (tried) return nullptr;
    tried = true;
    Il2CppClass* k = il2cpp_object_get_class((Il2CppObject*)real);
    Il2CppClass* cc = findClass("", "ClickControl");
    if (!cc || strcmp(il2cpp_class_get_name(k), "ClickControl[]") != 0) {
        LOG("critical: click list holds %s, the second pass keeps using the whole list", il2cpp_class_get_name(k));
        return nullptr;
    }
    a = newArray(cc, 64);
    if (a) g_p2Arr.set(a);
    LOG("critical: second pass on the held-back taps only %s", a ? "on" : "OFF (no array)");
    return a;
}
static void removeFromList(Obj* list, Obj* item) {
    Obj** items;
    int n;
    if (!listOf(list, &items, &n)) return;
    for (int i = 0; i < n; i++) {
        if (items[i] != item) continue;
        for (int j = i; j + 1 < n; j++) items[j] = items[j + 1];
        items[n - 1] = nullptr;
        fld<int>(list, C.listSize) = n - 1;
        if (g_listVersion == -2) g_listVersion = fieldOffset(il2cpp_object_get_class(list), "_version");
        if (g_listVersion >= 0) fld<int>(list, g_listVersion)++;
        return;
    }
}

bool critRunNotes(Obj* num, void (*orig)(Obj*, const MethodInfo*), const MethodInfo* m, bool counting) {
    if (!C.ok) return false;  // Criticals are always counted; 区分大小 P only decides whether they are shown
    uint64_t tStart = perfNow(), tOrig = 0;
    bool show = critEnabled();
    Obj* pc = fld<Obj*>(num, C.numPC);
    Obj* sc = pc ? fld<Obj*>(pc, C.pcScore) : nullptr;
    if (!sc) return false;
    if (sc != g_sc.get()) levelReset(sc);
    // the game may hand ScoreControl its Perfect prefab after our first frame: pick it up once it is there
    if (!alive((Obj*)g_basePrefab.get())) {
        Obj* cur = fld<Obj*>(sc, C.scPrefab);
        if (alive(cur)) {
            g_basePrefab.set(original(cur));
            LOG("critical: Perfect effect prefab found (%.2f s into the level)", timeSinceLevelLoad());
        }
    }
    Obj* base = (Obj*)g_basePrefab.get();
    float now = fld<float>(pc, C.pcNow), pr = perfectRange();

    // taps that will be a plain Perfect in this update: hold them back for pass 2 (shown), or just count them (not
    // shown: no second pass at all, the effect is the game's own anyway)
    int held = 0, plainTaps = 0;
    Obj** items;
    int n;
    if (listOf(fld<Obj*>(num, C.numLists[0]), &items, &n))
        for (int i = 0; i < n && held < 64; i++) {
            Obj* cc = items[i];
            Obj* note = cc ? fld<Obj*>(cc, C.ccNote) : nullptr;
            if (!note) continue;
            bool a = fld<bool>(cc, C.ccJudged), b = fld<bool>(note, C.noteJudged);
            if (!a && !b) continue;
            float off = fld<float>(note, C.noteTime) - now, t = absf(off);
            if (D.tapsLogged < 8 && counting) {
                if (D.tapsLogged == 0 && C.ccScore >= 0) {  // why the perfectJudge swap alone was not enough
                    Obj* csc = fld<Obj*>(cc, C.ccScore);
                    Obj* pj = csc ? fld<Obj*>(csc, C.scPrefab) : nullptr;
                    char pn[64] = "?";
                    if (pj) objName(pj, pn, sizeof pn);
                    LOG("critical: tap's ScoreControl %s ours, its perfectJudge %p (%s), ours %p", csc == sc ? "is" : "is NOT",
                        (void*)pj, pn, (void*)fld<Obj*>(sc, C.scPrefab));
                }
                D.tapsLogged++;
                LOG("critical: tap at %+.0f ms -> %s", off * 1000,
                    t <= kCritWindow ? "Critical" : t < pr ? "Perfect" : "Good / Bad");
            }
            if (t <= kCritWindow || t >= pr) continue;  // Critical (pass 1) / Good, Bad (pass 1)
            if (!show) {
                plainTaps++;
                continue;
            }
            g_held[held++] = Held{cc, note, a, b};
            fld<bool>(cc, C.ccJudged) = false;
            fld<bool>(note, C.noteJudged) = false;
        }
    // holds: remember the unfinished ones; a head about to be judged gets the matching spark prefab now
    int holds = 0;
    if (listOf(fld<Obj*>(num, C.numLists[2]), &items, &n))
        for (int i = 0; i < n && holds < 2048; i++) {
            Obj* h = items[i];
            if (!h || fld<bool>(h, C.hJudgeOver)) continue;
            g_holds[holds++] = HoldSnap{h};
            if (fld<bool>(h, C.hJudged)) continue;
            Obj* note = fld<Obj*>(h, C.hNote);
            if (!note || !(fld<bool>(h, C.hIsJudged) || fld<bool>(note, C.noteJudged))) continue;
            float t = absf(fld<float>(note, C.noteTime) - now);
            if (t >= pr) continue;
            Obj* use = show ? effectFor(fld<Obj*>(h, C.hPrefab), t <= kCritWindow) : nullptr;
            if (alive(use)) fld<Obj*>(h, C.hPrefab) = use;
            if (counting) (t <= kCritWindow ? D.holdCrit : D.holdPlain)++;
        }

    int p0 = fld<int>(sc, C.scPerfect);
    if (p0 < g_lastPerfect && g_crit > p0) g_crit = p0;  // practice restart / seek
    // pass 1: everything else, with the bright gold Critical effect
    Obj* critFx = show ? effectFor(base, true) : nullptr;
    if (alive(critFx)) fld<Obj*>(sc, C.scPrefab) = critFx;
    g_fxMode = 1;
    uint64_t o0 = perfNow();
    orig(num, m);
    uint64_t o1 = perfNow();
    perfAdd(PF_GAME_NOTES, o1 - o0);
    tOrig += o1 - o0;
    g_fxMode = 0;
    int p1 = fld<int>(sc, C.scPerfect);
    int plainHolds = 0;
    for (int i = 0; i < holds; i++) {
        Obj* h = g_holds[i].hold;
        if (fld<bool>(h, C.hJudgeOver) && fld<bool>(h, C.hPerfect) && !fld<bool>(h, C.hMissed) &&
            absf(fld<float>(h, C.hJudgeTime)) > kCritWindow)
            plainHolds++;
    }
    // pass 2: only the held-back taps, with the orange effect
    if (held) {
        for (int i = 0; i < held; i++) {
            fld<bool>(g_held[i].cc, C.ccJudged) = g_held[i].ccFlag;
            fld<bool>(g_held[i].note, C.noteJudged) = g_held[i].noteFlag;
        }
        Obj* or_ = show ? effectFor(base, false) : nullptr;
        if (alive(or_)) fld<Obj*>(sc, C.scPrefab) = or_;
        int before = fld<int>(sc, C.scPerfect);
        Obj* lists[3];
        int sizes[3];
        for (int k = 0; k < 3; k++) {
            lists[k] = fld<Obj*>(num, C.numLists[k + 1]);
            Obj** dummy;
            sizes[k] = 0;
            if (listOf(lists[k], &dummy, &sizes[k])) fld<int>(lists[k], C.listSize) = 0;
            else lists[k] = nullptr;
        }
        // the click list: just the held-back taps
        Obj* click = fld<Obj*>(num, C.numLists[0]);
        Il2CppArray* realItems = click && C.listItems >= 0 ? fld<Il2CppArray*>(click, C.listItems) : nullptr;
        int realSize = click && C.listSize >= 0 ? fld<int>(click, C.listSize) : 0;
        Il2CppArray* tmp = pass2Array(realItems);
        if (tmp) {
            for (int i = 0; i < held; i++) arrayData(tmp)[i] = g_held[i].cc;
            fld<Il2CppArray*>(click, C.listItems) = tmp;
            fld<int>(click, C.listSize) = held;
        }
        g_fxMode = 2;
        uint64_t o2 = perfNow();
        orig(num, m);
        uint64_t o3 = perfNow();
        perfAdd(PF_PASS2, o3 - o2);
        tOrig += o3 - o2;
        g_fxMode = 0;
        if (tmp) {
            // what the game took out of the small list (judged, destroyed) leaves the real one too
            Il2CppArray* nowItems = fld<Il2CppArray*>(click, C.listItems);
            int left = fld<int>(click, C.listSize);
            Obj** rest = nowItems ? (Obj**)arrayData(nowItems) : nullptr;
            if (!rest || left < 0 || (uintptr_t)left > nowItems->max_length) left = 0;
            fld<Il2CppArray*>(click, C.listItems) = realItems;
            fld<int>(click, C.listSize) = realSize;
            for (int i = 0; i < held; i++) {
                bool still = false;
                for (int j = 0; j < left && !still; j++) still = rest[j] == g_held[i].cc;
                if (!still) removeFromList(click, g_held[i].cc);
            }
            for (int i = 0; i < 64; i++) arrayData(tmp)[i] = nullptr;  // no references kept
            if (nowItems != tmp) LOG("critical: the game replaced the click list during the second pass (handled)");
        }
        for (int k = 0; k < 3; k++)
            if (lists[k]) fld<int>(lists[k], C.listSize) = sizes[k];
        if (counting) {
            D.held += held;
            D.pass2++;
            D.pass2Perfect += fld<int>(sc, C.scPerfect) - before;
        }
    }
    if (alive(base)) fld<Obj*>(sc, C.scPrefab) = base;
    if (counting) {
        int d = p1 - p0 - plainHolds - plainTaps;  // (shown: the plain taps were scored in pass 2 instead)
        if (d > 0) g_crit += d;
    }
    int perfect = fld<int>(sc, C.scPerfect);
    if (g_crit > perfect) g_crit = perfect;
    g_lastPerfect = perfect;
    g_resPerfect = perfect;
    float rt = timeSinceLevelLoad();
    if (rt - D.lastLog >= 15 && perfect > 0) {
        D.lastLog = rt;
        LOG("critical: %d Critical / %d Perfect so far; taps held for the orange pass %d (pass ran %d times, "
            "scored %d), hold heads gold %d / orange %d",
            g_crit, perfect, D.held, D.pass2, D.pass2Perfect, D.holdCrit, D.holdPlain);
    }
    Color skinLine[2];
    if (show) paintLines(pc, sc);
    else if (skinLineColors(skinLine)) paintLines(pc, sc, skinLine);  // a skin's colorPerfect (AP) / colorGood (FC)
    watchTick();
    uint64_t total = perfNow() - tStart;
    perfAdd(PF_CRIT, total > tOrig ? total - tOrig : 0);
    return true;
}

// ---- judge line: AP+ gold, AP orange, FC blue (only while the game's AP/FC indicator is on) ----
static void paintLines(Obj* pc, Obj* sc, const Color* apFc) {
    if (fld<int>(sc, C.scBad) + fld<int>(sc, C.scMiss) > 0) return;  // the game's own (white) line
    Obj* li = C.pcLevelInfo >= 0 ? fld<Obj*>(pc, C.pcLevelInfo) : nullptr;
    if (!li || (C.liApFc >= 0 && !fld<bool>(li, C.liApFc))) return;
    Obj* jc = fld<Obj*>(pc, C.pcJudge);
    if (!jc) return;
    int perfect = fld<int>(sc, C.scPerfect), good = fld<int>(sc, C.scGood);
    // FC blue / AP = the plain Perfect effect's colour / AP+ = the Critical effect's colour
    Color c = apFc ? apFc[good > 0 ? 1 : 0]
                   : good > 0 ? Color{0.62f, 0.82f, 1.f, 1} : perfect > g_crit ? fxColour(kPerfTint) : fxColour(kCritTint);
    Obj** lines;
    int n = elements(fld<Obj*>(jc, C.jcLines), &lines);
    for (int i = 0; i < n; i++) {
        Obj* jl = lines[i];
        if (!jl || (C.jlShowingPic >= 0 && fld<bool>(jl, C.jlShowingPic))) continue;
        Obj* sr = fld<Obj*>(jl, C.jlRenderer);
        if (!sr) continue;
        Color cur = callm<Color>(C.srGetColor, sr);
        if (cur.r != c.r || cur.g != c.g || cur.b != c.b)  // only when the game changed it back
            callm<void>(C.srSetColor, sr, Color{c.r, c.g, c.b, cur.a});
    }
}

// ---- result screen: the Perfect count, merged or split into Criticals (gold) and plain Perfects (orange) ----
// Split from the start while 区分大小 P is shown; otherwise merged, and a tap on the number splits it (the two parts
// slide out of the number to both sides), another tap merges it back. The parts are copies of the game's Text;
// while they show, the game's own number is blanked.
static Handle g_loc, g_numStr, g_blank, g_part[2], g_hitRoot, g_hit;
static int g_locPerfect = -1;
static const MethodInfo *g_txtOverflow, *g_txtAlign;
static float g_u = 0;
static bool g_split = false, g_ready = false;
static Vec2 g_cen;
static float g_z = 0, g_gap = 0;
static float g_numW = 0, g_numH = 0;  // the game's number (Text units), measured while it is on screen
static bool g_placed = false;         // g_cen was found at least once for this result

// Where the game's number is drawn (world): its rect now, and the glyphs' place in it from the Text's alignment.
// Every frame, also while split (the game's text is blanked then: the measured size is used). False while the
// rect has no size yet: the result screen builds its layout over the first frames, before that the Text sits at
// the canvas origin (0.33.x placed the parts once, there: the middle of the screen).
static bool numberCentre(Obj* t) {
    float x0, x1, y0, y1;
    Rect r{0, 0, 0, 0};
    if (g_numW <= 0 || !worldBounds(rt(t), &x0, &x1, &y0, &y1) || !rectOf(rt(t), &r) || r.h <= 0 || y1 - y0 <= 0)
        return false;
    float lossy = (y1 - y0) / r.h, w = g_numW * lossy, h = g_numH * lossy;
    if (h <= 0 || h > y1 - y0) h = y1 - y0;
    int al = textAlignment(t), col = al % 3, row = al / 3;
    g_cen = Vec2{col == 0 ? x0 + w / 2 : col == 2 ? x1 - w / 2 : (x0 + x1) / 2,
                 row == 0 ? y1 - h / 2 : row == 2 ? y0 + h / 2 : (y0 + y1) / 2};
    g_z = worldPos(rt(t)).z;
    g_gap = textFontSize(t) * 0.7f * lossy;
    return true;
}

static float easeInOut(float x) { return x < 0.5f ? 4 * x * x * x : 1 - (-2 * x + 2) * (-2 * x + 2) * (-2 * x + 2) / 2; }
static void onPerfectTap(void*) {
    g_split = !g_split;
    LOG("critical: result Perfect count %s", g_split ? "split" : "merged");
}

static Obj* partOf(Obj* t, const char* s, int size, Color c) {
    Obj* p = instantiate(t, parentOf(rt(t)));
    if (!alive(p)) return nullptr;
    for (int i = childCount(rt(p)) - 1; i >= 0; i--) destroy(gameObject(childAt(rt(p), i)));
    Obj* an = animatorClass() ? getComponent(gameObject(p), animatorClass()) : nullptr;
    if (an) destroy(an);
    ignoreLayout(gameObject(p));
    // centred anchor + pivot: setWorldPos then puts the number's middle exactly there (the game's pivot is not)
    Rect r{0, 0, 0, 0};
    rectOf(rt(t), &r);
    setLayout(rt(p), Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f});
    setSize(rt(p), Vec2{r.w > 1 ? r.w : 200, r.h > 1 ? r.h : 80});
    if (g_txtAlign) callm<void>(g_txtAlign, p, MiddleCenter);
    if (g_txtOverflow) callm<void>(g_txtOverflow, p, 1);
    setRaycast(p, false);
    setTextSize(p, size);
    setText(p, s);
    setColor(p, Color{c.r, c.g, c.b, 0});
    return p;
}

static void critResultFrame(void*) {
    Obj* loc = (Obj*)g_loc.get();
    if (!loc) return;
    if (!alive(loc)) {
        g_loc.clear();
        return;
    }
    Obj* t = fld<Obj*>(loc, g_locPerfect);
    if (!alive(t) || strcmp(il2cpp_class_get_name(il2cpp_object_get_class(t)), "Text") != 0) return;
    Il2CppString* cur = textStr(t);
    if (!g_ready) {  // wait for the game's own Perfect number
        char buf[32] = "";
        if (cur) utf8(cur, buf, sizeof buf);
        long v = 0;
        int digits = 0;
        for (const char* q = buf; *q; q++) {
            if (*q < '0' || *q > '9' || ++digits > 9) return;
            v = v * 10 + (*q - '0');
        }
        if (!digits || v != g_resPerfect) return;
        int crit = g_crit > v ? (int)v : g_crit, size = textFontSize(t);
        if (size <= 0) size = 40;
        char a[16], b[16];
        snprintf(a, sizeof a, "%d", crit);
        snprintf(b, sizeof b, "%d", (int)v - crit);
        Obj* p0 = partOf(t, a, size, rgb(0xFFF08A));
        Obj* p1 = p0 ? partOf(t, b, (int)(size * 0.8f + 0.5f), rgb(0xFFCB7A)) : nullptr;
        if (!p1) {
            if (p0) destroy(gameObject(p0));
            g_loc.clear();
            LOG("critical: result split unavailable (no copies)");
            return;
        }
        g_part[0].set(p0);
        g_part[1].set(p1);
        g_numStr.set(cur);
        g_blank.set(str(""));
        Obj* root = overlayCanvas("PhiMePerfectTouch", 31001);
        Obj* hit = image(root, "hit", SPR_SOLID, Color{1, 1, 1, 0}, Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{0, 0}, 0, true);
        onClick(gameObject(hit), hit, onPerfectTap, nullptr);
        g_hitRoot.set(root);
        g_hit.set(hit);
        g_u = g_split ? 1.f : 0.f;
        g_numW = textPreferredWidth(t);  // the number is still shown: measure it before it may be blanked
        g_numH = textPreferredHeight(t);
        g_ready = true;
        LOG("critical: result %d Critical / %d Perfect (%s; tap the number to switch), number %.0fx%.0f", crit,
            (int)v - crit, g_split ? "split" : "merged", g_numW, g_numH);
    }
    Obj *p0 = (Obj*)g_part[0].get(), *p1 = (Obj*)g_part[1].get();
    if (!alive(p0) || !alive(p1)) return;
    if (g_numW <= 0 && cur == g_numStr.get()) {  // could not be measured yet: again while it is shown
        g_numW = textPreferredWidth(t);
        g_numH = textPreferredHeight(t);
    }
    if (numberCentre(t)) g_placed = true;
    if (!g_placed) {
        // not laid out yet: the game's own number stays, the parts wait (hidden). Once placed, a frame that can't
        // be measured keeps the last place (no flicker between the number and the parts).
        if (cur != g_numStr.get()) setTextStr(t, (Il2CppString*)g_numStr.get());
        Color c0 = getColor(p0), c1 = getColor(p1);
        setColor(p0, Color{c0.r, c0.g, c0.b, 0});
        setColor(p1, Color{c1.r, c1.g, c1.b, 0});
        static bool logged = false;
        if (!logged) {
            logged = true;
            LOG("critical: result number not laid out yet, the split waits");
        }
        return;
    }
    float x0, x1, y0, y1;
    Rect r{0, 0, 0, 0};
    // the touch area over the number
    Obj* hit = (Obj*)g_hit.get();
    float sx0, sx1, sy0, sy1;
    if (alive(hit) && screenBounds(rt(t), &sx0, &sx1, &sy0, &sy1)) {
        float h = sy1 - sy0;
        Vec2 a = screenToCanvas(Vec2{sx0 - h, sy0 - h * 0.5f}), b = screenToCanvas(Vec2{sx1 + h, sy1 + h * 0.5f});
        place(rt(hit), Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{(a.x + b.x) / 2, (a.y + b.y) / 2}, Vec2{b.x - a.x, b.y - a.y});
    }
    float target = g_split ? 1.f : 0.f;
    if (g_u != target) {
        float step = unscaledDt() / 0.4f;
        g_u = g_u < target ? (g_u + step > target ? target : g_u + step) : (g_u - step < target ? target : g_u - step);
    }
    float e = easeInOut(g_u);
    // the game's number shows only while merged; its colour (the game's own fade) sets the parts' alpha
    if (g_u > 0) {
        if (cur != g_blank.get()) setTextStr(t, (Il2CppString*)g_blank.get());
    } else if (cur != g_numStr.get()) {
        setTextStr(t, (Il2CppString*)g_numStr.get());
    }
    float ga = getColor(t).a, fa = e * 4 > 1 ? 1 : e * 4;
    float wc = 0, wp = 0;
    if (worldBounds(rt(p0), &x0, &x1, &y0, &y1) && rectOf(rt(p0), &r) && r.h > 0) {
        float lossy = (y1 - y0) / r.h;
        wc = textPreferredWidth(p0) * lossy;
        wp = textPreferredWidth(p1) * lossy;
    }
    float d0 = -(wp + g_gap) / 2 * e, d1 = (wc + g_gap) / 2 * e;
    setWorldPos(rt(p0), Vec3{g_cen.x + d0, g_cen.y, g_z});
    setWorldPos(rt(p1), Vec3{g_cen.x + d1, g_cen.y, g_z});
    Color c0 = getColor(p0), c1 = getColor(p1);
    setColor(p0, Color{c0.r, c0.g, c0.b, ga * fa});
    setColor(p1, Color{c1.r, c1.g, c1.b, ga * fa});
}

// Criticals of the level whose result shows `perfect` Perfects (counted whether or not they are shown), -1 = unknown
int critResultCount(int perfect) {
    if (!C.ok || g_resPerfect < 0 || g_resPerfect != perfect) return -1;
    return g_crit > perfect ? perfect : g_crit;
}

void critResultStart(Obj* loc) {
    g_loc.clear();
    g_numStr.clear();
    g_part[0].clear();
    g_part[1].clear();
    g_hitRoot.clear();
    g_hit.clear();
    g_ready = false;
    g_numW = g_numH = 0;
    g_placed = false;
    g_split = critEnabled();  // 区分大小 P shown: split from the start
    // only right after a level that counted Criticals (critRunNotes ran and nothing reset it since)
    if (!C.ok || g_resPerfect < 0 || g_locPerfect < 0) return;
    g_loc.set(loc);
    static bool registered = false;
    if (!registered) {
        registered = true;
        onEveryFrame(critResultFrame);
    }
}
bool criticalInstall() {
    Il2CppClass* NUM = findClass("", "NoteUpdateManager");
    Il2CppClass* PC = findClass("", "ProgressControl");
    Il2CppClass* SC = findClass("", "ScoreControl");
    Il2CppClass* CC = findClass("", "ClickControl");
    Il2CppClass* HC = findClass("", "HoldControl");
    Il2CppClass* Note = findClass("", "ChartNote");
    Il2CppClass* JC = findClass("", "JudgeControl");
    Il2CppClass* JL = findClass("", "JudgeLineControl");
    Il2CppClass* LI = findClass("", "LevelInformation");
    Il2CppClass* SR = findClass("UnityEngine", "SpriteRenderer");
    C.Renderer = findClass("UnityEngine", "Renderer");
    C.Material = findClass("UnityEngine", "Material");
    if (!NUM || !PC || !SC || !CC || !HC || !Note || !JC || !JL || !LI || !SR || !C.Renderer || !C.Material)
        return false;
    C.numPC = fieldOffset(NUM, "progressControl");
    static const char* const lists[] = {"clickControls", "dragControls", "holdControls", "flickControls"};
    for (int i = 0; i < 4; i++) C.numLists[i] = fieldOffset(NUM, lists[i]);
    C.pcNow = fieldOffset(PC, "nowTime");
    C.pcScore = fieldOffset(PC, "scoreControl");
    C.pcJudge = fieldOffset(PC, "judgeControl");
    C.pcLevelInfo = fieldOffset(PC, "levelInformation");
    C.ccNote = fieldOffset(CC, "noteInfor");
    C.ccJudged = fieldOffset(CC, "isJudged");
    C.ccScore = fieldOffset(CC, "scoreControl");
    C.noteTime = fieldOffset(Note, "realTime");
    C.noteJudged = fieldOffset(Note, "isJudged");
    C.hNote = fieldOffset(HC, "noteInfor");
    C.hIsJudged = fieldOffset(HC, "isJudged");
    C.hJudged = fieldOffset(HC, "judged");
    C.hJudgeOver = fieldOffset(HC, "judgeOver");
    C.hPerfect = fieldOffset(HC, "isPerfect");
    C.hJudgeTime = fieldOffset(HC, "_judgeTime");
    C.hPrefab = fieldOffset(HC, "perfect");
    C.hMissed = fieldOffset(HC, "missed");
    C.scPerfect = fieldOffset(SC, "perfect");
    C.scGood = fieldOffset(SC, "good");
    C.scBad = fieldOffset(SC, "bad");
    C.scMiss = fieldOffset(SC, "miss");
    C.scPrefab = fieldOffset(SC, "perfectJudge");
    C.jcLines = fieldOffset(JC, "judgeLineControls");
    C.jlRenderer = fieldOffset(JL, "_judgeLineSpriteRenderer");
    C.jlShowingPic = fieldOffset(JL, "isShowingPic");
    C.liApFc = fieldOffset(LI, "ApFcIsOn");
    C.perfectRange = il2cpp_class_get_field_from_name(JC, "PerfectTimeRange");
    C.srGetColor = findMethod(SR, "get_color", 0);
    C.srSetColor = findMethod(SR, "set_color", 1);
    C.getShared = findMethod(C.Renderer, "get_sharedMaterial", 0);
    C.setMat = findMethod(C.Renderer, "set_material", 1);  // set_sharedMaterial is stripped in 4.0.x
    static const char* const matArg[] = {"Material"};
    C.matCtor = findMethodSig(C.Material, ".ctor", 1, matArg);
    C.matHas = findMethod(C.Material, "HasProperty", 1, IL2CPP_TYPE_STRING);
    C.matGet = findMethod(C.Material, "GetColor", 1, IL2CPP_TYPE_STRING);
    C.matSet = findMethod(C.Material, "SetColor", 2, IL2CPP_TYPE_STRING);
    C.matGetTex = findMethod(C.Material, "GetTexture", 1, IL2CPP_TYPE_STRING);
    C.matSetTex = findMethod(C.Material, "SetTexture", 2, IL2CPP_TYPE_STRING);
    const int must[] = {C.numPC, C.numLists[0], C.numLists[1], C.numLists[2], C.numLists[3], C.pcNow, C.pcScore,
                        C.pcJudge, C.ccNote, C.ccJudged, C.noteTime, C.noteJudged, C.hNote, C.hIsJudged, C.hJudged,
                        C.hJudgeOver, C.hPerfect, C.hJudgeTime, C.hPrefab, C.hMissed, C.scPerfect, C.scGood,
                        C.scBad, C.scMiss, C.scPrefab, C.jcLines, C.jlRenderer};
    for (int v : must)
        if (v < 0) return false;
    const void* ms[] = {C.srGetColor, C.srSetColor, C.getShared, C.setMat, C.matCtor, C.matHas, C.matGet, C.matSet};
    for (const void* p : ms)
        if (!p) return false;
    // optional: the result screen's split Perfect count
    Il2CppClass* LOC = findClass("", "LevelOverControl");
    g_locPerfect = LOC ? fieldOffset(LOC, "perfectText") : -1;
    Il2CppClass* Txt = findClass("UnityEngine.UI", "Text");
    g_txtOverflow = Txt ? findMethod(Txt, "set_horizontalOverflow", 1) : nullptr;
    g_txtAlign = Txt ? findMethod(Txt, "set_alignment", 1) : nullptr;
    // optional: without it the plain-Perfect taps rely on perfectJudge alone
    Il2CppClass* Object = findClass("UnityEngine", "Object");
    o_clone = (CloneFn)hookIcall(Object ? findMethod(Object, "Internal_CloneSingle", 1) : nullptr,
                                 "UnityEngine.Object::Internal_CloneSingle(UnityEngine.Object)", (void*)hk_clone);
    C.ok = true;
    return true;
}
