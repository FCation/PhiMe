// Result screen accuracy: 4 decimals, and a [分数 | ACC] switch that puts the accuracy in the big score slot.
//
// The game fills LevelOverControl's Texts from its Start coroutine (SetResultInfo: scoreText "D7", percentText
// "{0:00.00}%"; SetNewbestInNormalPlaying: best = "NEW BEST    {old:D7}   +{gain:D7}" / "BEST       {old:D7}" /
// "BEST       {old:D7}   {diff}" and, when worse, best.color = (0.8, 0.8, 0.8, 0)). We never change what the game
// computes; every frame the Texts are set to our strings when they differ (the game may write late), and the game's
// own strings are kept so switching back shows them again.
// The ACC best line compares with the record from before this play: GameInformation.GetSongRecord, read in
// LevelOverControl.Start (the coroutine updates the record later), or the course record in course mode.
//
// The swap is animated: once the result panel stopped moving, the game's score and percent Texts are made
// transparent and two copies of the big score Text (one showing the score, one the accuracy) take their places,
// sitting exactly where the game drew them. Switching moves each copy to the other slot (position and size,
// ease-in-out), the "Accuracy"/"Score" caption and the BEST line fade out and back in with the new text.
// The switch's touch area lives on our own overlay canvas over the switch's screen rect: the result canvas'
// raycasts land below what it draws.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

static struct {
    Il2CppClass* GI;
    const MethodInfo *getRec, *lriScore, *lriPct, *overflow, *vOverflow, *align;
    int giLsi, lsiId, lsiLv, recS, recA;
    int locLri, locBest, locScore, locPct, locRatings;
    Il2CppClass* Text;
    bool ok;
} K;

static Handle g_loc, g_toggle, g_toggleGroup, g_thumb, g_segLabel[2], g_hitRoot, g_hit;
static Handle g_caption, g_captionGame, g_captionOurs;
static Handle g_clone[2];  // 0 shows the score, 1 the accuracy
static Color g_slotColor[2];
static bool g_clonesOn = false, g_cloneFailed = false;
static Vec2 g_lastPos{0, 0};
static int g_still = 0;
static float g_toggleA = 0, g_u = 0;  // g_u: 0 = score big .. 1 = accuracy big (animated)
static float g_segX = 0, g_captionA = 1;
static Handle g_ours[3], g_game[3];  // 0 score slot, 1 percent slot, 2 best line: our strings / the game's
static int g_oldS = 0;
static long g_oldK = 0;  // old best accuracy in 1/10000 %
static float g_oldA = 0;
static bool g_haveOld = false, g_noNewBest = false, g_built = false, g_ready = false, g_hitLogged = false;
static Color g_bestColor{1, 1, 1, 1};  // the best line's color before the game greys it
static int g_score = 0;
static long g_accK = 0;
static float g_t0 = 0, g_maxPw = 0;
static const float kSwap = 0.55f;

// 98.76543 -> 987654: cut (not rounded) to 4 decimals, never 100.0000 below 100 (the values are floats)
static long accK(double a) {
    long k = (long)(a * 10000 + 0.05);
    if (a < 100 && k >= 1000000) k = 999999;
    return k < 0 ? 0 : k;
}
static void fmtK(char* b, int cap, long k) { snprintf(b, cap, "%ld.%04ld%%", k / 10000, k % 10000); }

static bool hasPct(const char* s) {
    for (; *s; s++)
        if (*s == '%') return true;
    return false;
}

static bool startsWith(Il2CppString* s, const char* p) {
    char b[24];
    if (!s || utf8(s, b, sizeof b) <= 0) return false;
    size_t n = strlen(p);
    return strlen(b) >= n && memcmp(b, p, n) == 0;
}

static float easeInOut(float u) {
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    if (u < 0.5f) return 4 * u * u * u;
    float v = -2 * u + 2;
    return 1 - v * v * v / 2;
}
static float lerp(float a, float b, float e) { return a + (b - a) * e; }

// the old record, before the result coroutine updates it
static void readOld(Obj* loc) {
    g_haveOld = false;
    g_oldS = 0;
    g_oldK = 0;
    g_oldA = 0;
    FieldInfo* f = K.GI ? il2cpp_class_get_field_from_name(K.GI, "_main") : nullptr;
    Obj* gi = nullptr;
    if (f) il2cpp_field_static_get_value(f, &gi);
    Obj* lsi = gi && K.giLsi >= 0 ? fld<Obj*>(gi, K.giLsi) : nullptr;
    if (!lsi) return;
    Il2CppString* id = fld<Il2CppString*>(lsi, K.lsiId);
    Il2CppString* lv = fld<Il2CppString*>(lsi, K.lsiLv);
    if (!id || !lv) return;
    if (courseLevelActive()) {
        char a[160], b[16];
        utf8(id, a, sizeof a);
        utf8(lv, b, sizeof b);
        int s = 0;
        float acc = 0;
        bool c = false;
        courseRecGet(a, b, &s, &acc, &c);  // zeros when there is none
        g_oldS = s;
        g_oldA = acc;
        g_oldK = accK(acc);
        g_haveOld = true;
        g_noNewBest = autoplayLevelActive() || g_cfg.practice;  // course.cpp shows no NEW BEST then either
        return;
    }
    bool okCall = false;
    void* args[2] = {id, lv};
    Obj* rec = invokeObj(K.getRec, gi, args, &okCall);
    if (!okCall) return;
    if (rec) {
        g_oldS = fld<int>(rec, K.recS);
        g_oldA = fld<float>(rec, K.recA);
        g_oldK = accK(g_oldA);
    }
    g_haveOld = true;
}

bool resAccOldBest(float* acc) {
    if (!g_haveOld) return false;
    *acc = g_oldA;
    return true;
}

static void bestLine(char* b, int cap) {
    char o[24], d[24];
    fmtK(o, sizeof o, g_oldK);
    long diff = g_accK - g_oldK;
    if (diff > 0 && !g_noNewBest) {
        fmtK(d, sizeof d, diff);
        snprintf(b, cap, "NEW BEST    %s   +%s", o, d);
    } else if (diff >= 0) {
        snprintf(b, cap, "BEST       %s", o);
    } else {
        fmtK(d, sizeof d, -diff);
        snprintf(b, cap, "BEST       %s   -%s", o, d);
    }
}

static Obj* slot(Obj* loc, int i) {
    int off = i == 0 ? K.locScore : i == 1 ? K.locPct : K.locBest;
    Obj* t = off >= 0 ? fld<Obj*>(loc, off) : nullptr;
    return alive(t) ? t : nullptr;
}

// keep the game's string of a slot (anything that is not ours), then show what the mode wants
static void enforce(Obj* t, int i, Il2CppString* want) {
    Il2CppString* now = textStr(t);
    if (now && now != g_ours[i].get()) g_game[i].set(now);
    if (!want) want = (Il2CppString*)g_game[i].get();
    if (want && now != want) setTextStr(t, want);
}

// ---------------- the switch ----------------
static void onToggle(void*) {
    g_cfg.resultAccBig = !g_cfg.resultAccBig;
    prefSetIntRaw("PhiMeMod.resultAccBig", g_cfg.resultAccBig ? 1 : 0);  // past the autoplay sandbox
    prefSave();
    LOG("resacc: big number is now the %s", g_cfg.resultAccBig ? "accuracy" : "score");
}

// the thumb slides under the labels, the labels change color with it
static void paintToggle(float e) {
    Obj* th = (Obj*)g_thumb.get();
    if (alive(th)) setPos(rt(th), Vec2{lerp(-g_segX, g_segX, e), 0});
    Color ink = rgb(0x141413), ivory = rgb(0xFAF9F5, 0.75f);
    for (int i = 0; i < 2; i++) {
        float on = i == 0 ? 1 - e : e;
        Obj* l = (Obj*)g_segLabel[i].get();
        if (alive(l))
            setColor(l, Color{lerp(ivory.r, ink.r, on), lerp(ivory.g, ink.g, on), lerp(ivory.b, ink.b, on),
                              lerp(ivory.a, ink.a, on)});
    }
}

// [分数 | ACC] under the rating letter, on the height of the BEST line: a child of the best Text, sized from its
// font, so it scales with the game's own layout (phone / iPad alike). Drawn only: taps go to the overlay.
static Obj* toggleHost(Obj* loc) {
    Obj* b = slot(loc, 2);
    return b ? b : slot(loc, 0);
}

static void buildToggle(Obj* host, bool hostIsBest) {
    int fs = textFontSize(host);
    if (fs <= 0) fs = hostIsBest ? 26 : 80;
    float h = hostIsBest ? fs * 1.9f : fs * 0.5f;
    float segW = h * 1.9f, w = segW * 2 + 8;
    g_segX = segW * 0.5f + 1;
    Obj* bg = image(rt(host), "PhiMeAccSwitch", SPR_ROUND, Color{0.04f, 0.04f, 0.06f, 0.6f}, Vec2{0.5f, 0.5f},
                    Vec2{0, 0}, Vec2{w, h}, h * 0.5f, false);
    if (!bg) return;
    Obj* th = image(rt(bg), "thumb", SPR_ROUND, rgb(0xFAF9F5), Vec2{0.5f, 0.5f}, Vec2{-g_segX, 0},
                    Vec2{segW - 2, h - 8}, (h - 8) * 0.5f);
    g_thumb.set(th);
    const char* names[2] = {T("分数", "Score"), "ACC"};
    for (int i = 0; i < 2; i++) {
        float x = (i == 0 ? -1 : 1) * g_segX;
        Obj* l = text(rt(bg), names[i], (int)(h * 0.48f + 0.5f), rgb(0xFAF9F5), MiddleCenter, true, Vec2{0.5f, 0.5f},
                      Vec2{0.5f, 0.5f}, Vec2{x, 0}, Vec2{segW, h});
        setRaycast(l, false);
        g_segLabel[i].set(l);
    }
    g_toggleGroup.set(addCanvasGroup(gameObject(bg)));
    setGroupAlpha((Obj*)g_toggleGroup.get(), 0);  // shown once the result panel stopped moving
    setBlocksRaycasts((Obj*)g_toggleGroup.get(), false);
    g_toggle.set(bg);
    paintToggle(easeInOut(g_u));
    // the touch area: our own overlay canvas, placed over the switch every frame
    Obj* root = overlayCanvas("PhiMeAccSwitchTouch", 31000);
    Obj* hit = image(root, "hit", SPR_SOLID, Color{1, 1, 1, 0}, Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{0, 0}, 0, true);
    onClick(gameObject(hit), hit, onToggle, nullptr);
    g_hitRoot.set(root);
    g_hit.set(hit);
}

static Obj* shownRating(Obj* loc) {
    Il2CppArray* a = K.locRatings >= 0 ? fld<Il2CppArray*>(loc, K.locRatings) : nullptr;
    if (!a) return nullptr;
    for (uintptr_t i = 0; i < a->max_length; i++) {
        Obj* go = (Obj*)arrayData(a)[i];
        if (go && alive(go) && activeSelf(go)) return go;
    }
    return nullptr;
}

// the overlay touch area over the switch's screen rect, generously padded
static void placeHit() {
    Obj* bg = (Obj*)g_toggle.get();
    Obj* hit = (Obj*)g_hit.get();
    if (!alive(bg) || !alive(hit)) return;
    float x0, x1, y0, y1;
    if (g_toggleA <= 0 || !screenBounds(rt(bg), &x0, &x1, &y0, &y1)) {
        setSize(rt(hit), Vec2{0, 0});
        return;
    }
    float h = y1 - y0, padX = h * 0.8f, padY = h * 0.75f;
    Vec2 a = screenToCanvas(Vec2{x0 - padX, y0 - padY}), b = screenToCanvas(Vec2{x1 + padX, y1 + padY});
    place(rt(hit), Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{(a.x + b.x) / 2, (a.y + b.y) / 2},
          Vec2{b.x - a.x, b.y - a.y});
    if (!g_hitLogged) {
        g_hitLogged = true;
        Vec2 s = screenSize();
        LOG("resacc: switch on screen x %.0f..%.0f y %.0f..%.0f (screen %.0fx%.0f), touch area %.0fx%.0f units", x0, x1,
            y0, y1, s.x, s.y, b.x - a.x, b.y - a.y);
    }
}

static void placeToggle(Obj* loc, Obj* host, bool hostIsBest) {
    Obj* bg = (Obj*)g_toggle.get();
    Rect r{0, 0, 0, 0}, tr{0, 0, 0, 0};
    RectInfo hi;
    if (!alive(bg) || !rectOf(rt(host), &r) || !rectOf(rt(bg), &tr) || !rectInfo(rt(host), &hi)) return;
    // the host text's glyphs: right end (the wider of its strings) and vertical middle
    float pw = textPreferredWidth(host);
    if (pw > g_maxPw) g_maxPw = pw;
    pw = g_maxPw < r.w ? g_maxPw : r.w;
    float ph = textPreferredHeight(host);
    if (ph <= 0 || ph > r.h) ph = r.h;
    int al = textAlignment(host), col = al % 3, row = al / 3;
    float right = col == 0 ? r.x + pw : col == 1 ? r.x + r.w * 0.5f + pw * 0.5f : r.x + r.w;
    float cy = row == 0 ? r.y + r.h - ph * 0.5f : row == 1 ? r.y + r.h * 0.5f : r.y + ph * 0.5f;
    if (!hostIsBest) cy = r.y - tr.h * 0.5f - 6;  // no best line: just under the big number
    float gap = tr.h * 0.5f;
    float x = right + gap + tr.w * 0.5f;
    Obj* rg = shownRating(loc);
    float x0, x1, y0, y1;
    Vec2 c;
    if (rg && screenBounds(goTransform(rg), &x0, &x1, &y0, &y1) &&
        screenToLocal(rt(host), Vec2{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f}, &c) && c.x > x)
        x = c.x;  // centred under the letter, unless that would cover the line's end
    Vec2 p{x, cy};
    // settle first: the panel slides in, the letter pops; fade in once it held still for a few frames
    float d = (p.x - g_lastPos.x) * (p.x - g_lastPos.x) + (p.y - g_lastPos.y) * (p.y - g_lastPos.y);
    g_still = d < 0.25f ? g_still + 1 : 0;
    g_lastPos = p;
    place(rt(bg), hi.pivot, Vec2{0.5f, 0.5f}, p, Vec2{tr.w, tr.h});
    Obj* g = (Obj*)g_toggleGroup.get();
    if (alive(g) && (g_still >= 8 || timeSinceLevelLoad() - g_t0 > 4)) {
        g_toggleA += unscaledDt() / 0.3f;
        if (g_toggleA > 1) g_toggleA = 1;
        setGroupAlpha(g, g_toggleA);
    }
    placeHit();
}

// ---------------- the two numbers ----------------
struct Geo {
    float minX, maxX, minY, maxY, lossy, fs, z;
    int col, row;
};
static bool geoOf(Obj* t, Geo* g) {
    Rect r{0, 0, 0, 0};
    if (!worldBounds(rt(t), &g->minX, &g->maxX, &g->minY, &g->maxY) || !rectOf(rt(t), &r) || r.h <= 0) return false;
    g->lossy = (g->maxY - g->minY) / r.h;
    g->fs = (float)textFontSize(t);
    int al = textAlignment(t);
    g->col = al % 3;
    g->row = al / 3;
    g->z = worldPos(rt(t)).z;
    return g->lossy > 0 && g->fs > 0;
}

// the copy as big / where the game would draw that string in slot g: scale, centre (world)
static void target(Obj* c, const Geo& g, float rootLossy, float fsC, float* s, float* cx, float* cy) {
    *s = g.fs * g.lossy / (fsC * rootLossy);
    float w = textPreferredWidth(c) * rootLossy * *s, h = textPreferredHeight(c) * rootLossy * *s;
    *cx = g.col == 0 ? g.minX + w / 2 : g.col == 2 ? g.maxX - w / 2 : (g.minX + g.maxX) / 2;
    *cy = g.row == 0 ? g.maxY - h / 2 : g.row == 2 ? g.minY + h / 2 : (g.minY + g.maxY) / 2;
}

static Obj* makeClone(Obj* from, Obj* root, Il2CppString* s) {
    Obj* c = instantiate(from, root);
    if (!alive(c)) return nullptr;
    Obj* go = gameObject(c);
    for (int i = childCount(rt(c)) - 1; i >= 0; i--) destroy(gameObject(childAt(rt(c), i)));  // labels of ours
    Obj* an = animatorClass() ? getComponent(go, animatorClass()) : nullptr;
    if (an) destroy(an);
    ignoreLayout(go);
    setLayout(rt(c), Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f});
    setSize(rt(c), Vec2{100, 100});
    if (K.align) callm<void>(K.align, c, MiddleCenter);
    if (K.overflow) callm<void>(K.overflow, c, 1);
    if (K.vOverflow) callm<void>(K.vOverflow, c, 1);
    setRaycast(c, false);
    setTextStr(c, s);
    setLastSibling(rt(c));
    return c;
}

static void clonesOn(Obj* score, Obj* pct) {
    g_clonesOn = true;
    Obj* root = canvasTransform(rt(score));
    Il2CppString* sc = (Il2CppString*)g_game[0].get();
    if (!sc) sc = (Il2CppString*)g_ours[1].get();
    Obj* a = root ? makeClone(score, root, sc) : nullptr;
    Obj* b = a ? makeClone(score, root, (Il2CppString*)g_ours[0].get()) : nullptr;
    if (!a || !b) {
        if (a) destroy(gameObject(a));
        g_cloneFailed = true;
        LOG("resacc: number copies unavailable, switching without the move");
        return;
    }
    g_clone[0].set(a);
    g_clone[1].set(b);
    setColor(a, getColor(score));
    setColor(b, getColor(pct));
    g_slotColor[0] = getColor(score);
    g_slotColor[1] = getColor(pct);
    LOG("resacc: number copies on (score font %d, percent font %d)", textFontSize(score), textFontSize(pct));
}

// copies placed for animation progress e; the game's own two Texts kept transparent
static bool clonesTick(Obj* score, Obj* pct, float e) {
    Obj* a = (Obj*)g_clone[0].get();
    Obj* b = (Obj*)g_clone[1].get();
    if (!alive(a) || !alive(b)) return false;
    Obj* root = canvasTransform(rt(score));
    Rect rr{0, 0, 0, 0};
    float rx0, rx1, ry0, ry1;
    if (!root || !rectOf(root, &rr) || rr.h <= 0 || !worldBounds(root, &rx0, &rx1, &ry0, &ry1)) return false;
    float rootLossy = (ry1 - ry0) / rr.h;
    Geo gs, gp;
    if (!geoOf(score, &gs) || !geoOf(pct, &gp)) return false;
    float fsC = (float)textFontSize(a);
    if (fsC <= 0) return false;
    Obj* cl[2] = {a, b};
    const Geo* from[2] = {&gs, &gp};
    const Geo* to[2] = {&gp, &gs};
    for (int i = 0; i < 2; i++) {
        float s0, x0, y0, s1, x1, y1;
        target(cl[i], *from[i], rootLossy, fsC, &s0, &x0, &y0);
        target(cl[i], *to[i], rootLossy, fsC, &s1, &x1, &y1);
        setScale(rt(cl[i]), lerp(s0, s1, e));
        setWorldPos(rt(cl[i]), Vec3{lerp(x0, x1, e), lerp(y0, y1, e), from[i]->z});
        // color follows the slot it is in (the two may differ)
        Color c0 = g_slotColor[i], c1 = g_slotColor[1 - i];
        setColor(cl[i], Color{lerp(c0.r, c1.r, e), lerp(c0.g, c1.g, e), lerp(c0.b, c1.b, e), lerp(c0.a, c1.a, e)});
    }
    Obj* orig[2] = {score, pct};
    for (int i = 0; i < 2; i++) {
        Color c = getColor(orig[i]);
        if (c.a != 0) setColor(orig[i], Color{c.r, c.g, c.b, 0});
    }
    return true;
}

// the "Accuracy" caption under the percent: says "Score" while the score sits there
static void findCaption(Obj* pct) {
    Obj* parent = parentOf(rt(pct));
    Il2CppArray* ts = alive(parent) && K.Text ? componentsInChildren(gameObject(parent), K.Text) : nullptr;
    for (uintptr_t i = 0; ts && i < ts->max_length; i++) {
        Obj* t = (Obj*)arrayData(ts)[i];
        char b[32];
        if (t && t != pct && utf8(textStr(t), b, sizeof b) > 0 && strcmp(b, "Accuracy") == 0) {
            g_caption.set(t);
            g_captionGame.set(textStr(t));
            g_captionA = getColor(t).a;
            LOG("resacc: Accuracy caption found");
            return;
        }
    }
    LOG("resacc: no Accuracy caption next to the percent");
}

// out, text swapped at the middle, back in
static float fadeOf(float u) { return u < 0.5f ? 1 - 2 * u : 2 * u - 1; }

static void captionTick(float u) {
    Obj* t = (Obj*)g_caption.get();
    if (!t || !alive(t)) return;
    if (!g_captionOurs.h) g_captionOurs.set(str("Score"));
    Il2CppString* want = (Il2CppString*)(u >= 0.5f ? g_captionOurs.get() : g_captionGame.get());
    if (want && textStr(t) != want) setTextStr(t, want);
    Color c = getColor(t);
    float a = g_captionA * easeInOut(fadeOf(u));
    if (c.a != a) setColor(t, Color{c.r, c.g, c.b, a});
}

static void bestTick(Obj* best, float u) {
    bool big = u >= 0.5f;
    float fade = easeInOut(fadeOf(u));
    Il2CppString* game = (Il2CppString*)g_game[2].get();
    Il2CppString* now = textStr(best);
    if (now && now != g_ours[2].get()) game = now, g_game[2].set(now);
    // only a normal best line is replaced (not challenge texts or empty), and only when the old record is known
    bool line = game && (startsWith(game, "NEW BEST") || startsWith(game, "BEST"));
    if (!line || !g_haveOld) return;
    if (!g_ours[2].h) {
        char b[96];
        bestLine(b, sizeof b);
        g_ours[2].set(str(b));
    }
    Il2CppString* want = big ? (Il2CppString*)g_ours[2].get() : game;
    if (now != want) setTextStr(best, want);
    // the game's color for its line: greyed and see-through when worse than the record; faded while switching
    bool worse = big ? g_accK < g_oldK : g_score < g_oldS;
    Color base = worse ? Color{0.8f, 0.8f, 0.8f, 0} : g_bestColor;
    Color c = getColor(best), w{base.r, base.g, base.b, base.a * fade};
    if (c.r != w.r || c.g != w.g || c.b != w.b || c.a != w.a) setColor(best, w);
}

static void resFrame(void*) {
    Obj* loc = (Obj*)g_loc.get();
    if (!loc) return;
    if (!alive(loc)) {
        g_loc.clear();
        g_toggle.clear();
        return;
    }
    if (!g_ready) {
        Obj* lri = K.locLri >= 0 ? fld<Obj*>(loc, K.locLri) : nullptr;
        Obj* pt = slot(loc, 1);
        if (!lri || !pt) return;
        // the game's own texts first (its percent line ends with '%'), 3 s at most
        char b[24];
        Il2CppString* ps = textStr(pt);
        bool filled = ps && utf8(ps, b, sizeof b) > 0 && hasPct(b);
        if (!filled && timeSinceLevelLoad() - g_t0 < 3) return;
        Obj* st = slot(loc, 0);
        if (K.overflow) {  // "99.1234%" is wider than the 7 digits the score slot was laid out for
            if (st) callm<void>(K.overflow, st, 1);
            callm<void>(K.overflow, pt, 1);
        }
        g_score = callm<int>(K.lriScore, lri);
        g_accK = accK(callm<float>(K.lriPct, lri));
        char a[24], s[16];
        fmtK(a, sizeof a, g_accK);
        snprintf(s, sizeof s, "%07d", g_score);
        g_ours[0].set(str(a));  // the accuracy
        g_ours[1].set(str(s));  // the score
        g_ready = true;
        findCaption(pt);
        LOG("resacc: %07d %s, old best %07d %ld (%s)", g_score, a, g_oldS, g_oldK, g_haveOld ? "read" : "unknown");
    }
    // the switch's progress, eased both ways (a tap mid-way turns it around)
    float dt = unscaledDt();
    float tgt = g_cfg.resultAccBig ? 1.f : 0.f;
    if (g_u < tgt) g_u = g_u + dt / kSwap > tgt ? tgt : g_u + dt / kSwap;
    else if (g_u > tgt) g_u = g_u - dt / kSwap < tgt ? tgt : g_u - dt / kSwap;
    float e = easeInOut(g_u);

    Obj* score = slot(loc, 0);
    Obj* pct = slot(loc, 1);
    Obj* best = slot(loc, 2);
    Il2CppString* accStr = (Il2CppString*)g_ours[0].get();
    bool moving = g_clonesOn && !g_cloneFailed;
    // the game's Texts: the score stays the score and the percent shows 4 decimals while our copies move;
    // without copies they swap strings at the middle of the switch
    bool big = !moving && g_u >= 0.5f;
    if (score) enforce(score, 0, big ? accStr : nullptr);
    if (pct) {
        Il2CppString* want = big ? (Il2CppString*)g_ours[1].get() : accStr;
        Il2CppString* now = textStr(pct);
        if (now && now != g_ours[1].get() && now != accStr) g_game[1].set(now);
        if (want && now != want) setTextStr(pct, want);
    }
    if (best) bestTick(best, g_u);
    captionTick(g_u);
    Obj* host = toggleHost(loc);
    if (host) {
        bool isBest = host == best;
        if (!g_built) {
            g_built = true;
            buildToggle(host, isBest);
        }
        placeToggle(loc, host, isBest);
        paintToggle(e);
    }
    // the copies take over once everything holds still (they then sit exactly where the game's Texts were)
    if (score && pct && !g_clonesOn && g_toggleA > 0) {
        clonesOn(score, pct);
        moving = !g_cloneFailed;  // placed this very frame: never drawn at the canvas centre
    }
    if (moving && score && pct && !clonesTick(score, pct, e)) {
        // lost them (or the geometry): give the game's Texts back
        g_cloneFailed = true;
        for (int i = 0; i < 2; i++) {
            Obj* c = (Obj*)g_clone[i].get();
            if (alive(c)) destroy(gameObject(c));
        }
        Obj* orig[2] = {score, pct};
        for (int i = 0; i < 2; i++) setColor(orig[i], g_slotColor[i]);
        LOG("resacc: number copies dropped");
    }
}

bool resAccOwnsBest() { return g_u >= 0.5f && g_ours[2].h && g_loc.get(); }

void resAccStart(Obj* loc) {
    g_loc.clear();
    g_toggle.clear();
    for (int i = 0; i < 3; i++) g_ours[i].clear(), g_game[i].clear();
    for (int i = 0; i < 2; i++) g_segLabel[i].clear(), g_clone[i].clear();
    g_thumb.clear();
    g_hit.clear();
    g_hitRoot.clear();
    g_built = g_ready = g_clonesOn = g_cloneFailed = g_hitLogged = false;
    g_noNewBest = false;
    g_maxPw = 0;
    g_toggleGroup.clear();
    g_caption.clear();
    g_captionGame.clear();
    g_captionA = 1;
    g_lastPos = Vec2{0, 0};
    g_still = 0;
    g_toggleA = 0;
    g_u = g_cfg.resultAccBig ? 1.f : 0.f;  // no animation for the starting side
    g_haveOld = false;
    if (!K.ok || gameChallengeLevel()) return;
    readOld(loc);
    Obj* best = slot(loc, 2);
    if (best) g_bestColor = getColor(best);  // the prefab's color, before the coroutine greys it
    g_t0 = timeSinceLevelLoad();
    g_loc.set(loc);
    onEveryFrame(resFrame);
}

bool resAccInstall() {
    K.GI = findClass("", "GameInformation");
    Il2CppClass* LOC = findClass("", "LevelOverControl");
    Il2CppClass* LRI = findClass("", "LevelResultInfo");
    Il2CppClass* LSI = findClass("", "LevelStartInfo");
    Il2CppClass* Rec = findClass("", "LevelRecord");
    if (!K.GI || !LOC || !LRI || !LSI || !Rec) return false;
    K.getRec = findMethod(K.GI, "GetSongRecord", 2);
    K.lriScore = findMethod(LRI, "get_IntScore", 0);
    K.lriPct = findMethod(LRI, "get_Percent", 0);
    Il2CppClass* Txt = findClass("UnityEngine.UI", "Text");
    K.overflow = Txt ? findMethod(Txt, "set_horizontalOverflow", 1) : nullptr;
    K.vOverflow = Txt ? findMethod(Txt, "set_verticalOverflow", 1) : nullptr;
    K.align = Txt ? findMethod(Txt, "set_alignment", 1) : nullptr;
    K.giLsi = fieldOffset(K.GI, "levelStartInfo");
    K.lsiId = fieldOffset(LSI, "songsId");
    K.lsiLv = fieldOffset(LSI, "songsLevel");
    K.recS = fieldOffset(Rec, "s");
    K.recA = fieldOffset(Rec, "a");
    K.locLri = fieldOffset(LOC, "levelResultInfo");
    K.locBest = fieldOffset(LOC, "best");
    K.locScore = fieldOffset(LOC, "scoreText");
    K.locPct = fieldOffset(LOC, "percentText");
    K.locRatings = fieldOffset(LOC, "ratings");
    K.Text = Txt;
    K.ok = K.getRec && K.lriScore && K.lriPct && K.giLsi >= 0 && K.lsiId >= 0 && K.lsiLv >= 0 && K.recS >= 0 &&
           K.recA >= 0 && K.locLri >= 0 && K.locScore >= 0 && K.locPct >= 0;
    LOG("resacc: result accuracy %s", K.ok ? "on" : "off (fields missing)");
    return K.ok;
}
