// In-level HUD extras, in the game's own score font:
//   - an accuracy line under the score: live (judged notes only) or the best still reachable
//     (100% minus what is already lost); optionally swapped with the score (accuracy big, score small)
//   - Perfect / Good / Bad+Miss counts under the combo label
//
// ScoreControl.Update (4.0.x) rewrites the score / combo strings every frame from its counters:
//   accuracy = (perfect + 0.65 * good) / levelInformation.numOfNotes
// so we run right after it and only touch our own Texts (plus the score Text when swapped).
// Our Texts are children of the game's Texts: they follow its slide-in / layout and die with the level.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

typedef void (*VoidFn)(Obj*, const MethodInfo*);
static VoidFn orig_SC_Update;

static struct {
    int levelInfo, perfect, good, bad, miss, scoreText, comboLabel, liNotes, scoreValue;
} S;

static Handle g_sc;                 // the ScoreControl our Texts were built for
static Handle g_acc, g_judge;       // our Texts (null when that line is off)
static Handle g_accStr, g_judgeStr, g_scoreStr; // cached strings, rebuilt only when the shown value changes
static int g_accKey = -1;           // shown accuracy in hundredths of a percent
static int g_judgeKey[5] = {-1, -1, -1, -1, -1};
static int g_scoreKey = -1;         // shown score with Criticals
static float g_lineRatio = 0;       // the score font's line height / font size

// best-effort line height of a Text's font, measured on the score (always a non-empty string)
static float lineRatio(Obj* scoreText) {
    int fs = textFontSize(scoreText);
    float h = textPreferredHeight(scoreText);
    float r = fs > 0 ? h / fs : 0;
    return r >= 0.8f && r <= 2.f ? r : 1.15f;
}

// A Text right under the host Text's line of glyphs, same horizontal alignment, `size` big.
static Obj* lineUnder(Obj* host, int size, const char* what) {
    Obj* hrt = rt(host);
    Rect r;
    if (!rectOf(hrt, &r)) return nullptr;
    int a = textAlignment(host), row = a / 3, col = a % 3;
    float gh = textFontSize(host) * g_lineRatio;
    // top of the glyph line, measured from the bottom of the host rect
    float top = row == 0 ? r.h : row == 1 ? (r.h + gh) / 2 : gh;
    float ax = col * 0.5f;
    float w = r.w > 400 ? r.w : 400;
    Obj* t = text(hrt, "", size, rgb(0xFFFFFF), col /* Upper + same column */, false, Vec2{ax, 0}, Vec2{ax, 1},
                  Vec2{0, top - gh}, Vec2{w, size * 1.5f});
    setTextFont(t, textFont(host));
    LOG("hud: %s under a %d px text (align %d, rect %.0fx%.0f, line %.2f), size %d", what, textFontSize(host), a, r.w,
        r.h, g_lineRatio, size);
    return t;
}

static void build(Obj* sc) {
    g_sc.set(sc);
    g_acc.clear();
    g_judge.clear();
    g_accKey = -1;
    g_scoreKey = -1;
    for (int& k : g_judgeKey) k = -1;
    Obj* score = fld<Obj*>(sc, S.scoreText);
    Obj* label = fld<Obj*>(sc, S.comboLabel);
    g_lineRatio = lineRatio(score);
    if (g_cfg.accShow && alive(score)) {
        int fs = textFontSize(score);
        int size = (int)(fs * 0.42f + 0.5f);
        if (size < 12) size = 12;
        g_acc.set(lineUnder(score, size, g_cfg.accSwap ? "score (swapped)" : "accuracy"));
    }
    if (g_cfg.judgeShow && alive(label)) {
        int size = textFontSize(label);
        if (size < 12) size = 12;
        g_judge.set(lineUnder(label, size, "judgements"));
    }
}

static int alphaOf(Obj* t) {
    if (!alive(t)) return 255;
    float a = getColor(t).a;
    return a <= 0 ? 0 : a >= 1 ? 255 : (int)(a * 255 + 0.5f);
}

// Critical mode: the game's score + 1 per Critical, in the game's 7-digit format
static Il2CppString* critScore(Obj* sc) {
    float v = fld<float>(sc, S.scoreValue);
    int key = (int)(v + 0.5f) + critCount();
    if (key != g_scoreKey || !g_scoreStr.h) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%07d", key);
        g_scoreStr.set(str(buf));
        g_scoreKey = key;
    }
    return (Il2CppString*)g_scoreStr.get();
}

static void updateAcc(Obj* sc, Obj* acc, Il2CppString* scoreStr) {
    int p = fld<int>(sc, S.perfect), g = fld<int>(sc, S.good), b = fld<int>(sc, S.bad), m = fld<int>(sc, S.miss);
    int judged = p + g + b + m;
    double got = p + 0.65 * g, pct = 100;
    if (g_cfg.accMode == 1) {
        Obj* li = fld<Obj*>(sc, S.levelInfo);
        int notes = li ? fld<int>(li, S.liNotes) : 0;
        if (notes > 0) pct = 100.0 * (notes - (judged - got)) / notes;
    } else if (judged > 0) {
        pct = 100.0 * got / judged;
    }
    // floor to 0.01 %, so a single Good never shows as 100.00 %
    int key = (int)(pct * 100 + 1e-6);
    if (key < 0) key = 0;
    if (key > 10000) key = 10000;
    if (key != g_accKey || !g_accStr.h) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d.%02d%%", key / 100, key % 100);
        g_accStr.set(str(buf));
        g_accKey = key;
    }
    Il2CppString* accStr = (Il2CppString*)g_accStr.get();
    Obj* score = fld<Obj*>(sc, S.scoreText);
    if (g_cfg.accSwap && alive(score)) {
        // the game wrote this frame's score string (it skips frames before the level starts): move it down
        Il2CppString* cur = textStr(score);
        if (scoreStr) setTextStr(acc, scoreStr);  // Critical mode: our score
        else if (cur && cur != accStr) setTextStr(acc, cur);
        setTextStr(score, accStr);
    } else {
        setTextStr(acc, accStr);
    }
    if (alive(score)) {  // follow the score's colour / fades
        Color c = getColor(score);
        setColor(acc, c);
    }
}

static void updateJudge(Obj* sc, Obj* judge, bool crit) {
    int p = fld<int>(sc, S.perfect), g = fld<int>(sc, S.good);
    int bm = fld<int>(sc, S.bad) + fld<int>(sc, S.miss);
    int cp = crit ? critCount() : -1;
    // rich-text colours replace the Text colour, so the game's fade (alpha) goes into the tags
    int a = alphaOf(fld<Obj*>(sc, S.comboLabel));
    if (p == g_judgeKey[0] && g == g_judgeKey[1] && bm == g_judgeKey[2] && a == g_judgeKey[3] && cp == g_judgeKey[4] &&
        g_judgeStr.h)
        return;
    g_judgeKey[0] = p;
    g_judgeKey[1] = g;
    g_judgeKey[2] = bm;
    g_judgeKey[3] = a;
    g_judgeKey[4] = cp;
    char buf[224];
    if (crit)  // Critical (gold) / Perfect (orange-yellow, close to it) / Good / Bad + Miss
        snprintf(buf, sizeof(buf),
                 "<color=#FFF08A%02X>%d</color>  <color=#FFCB7A%02X>%d</color>  <color=#B4E1FF%02X>%d</color>  "
                 "<color=#A0A0A0%02X>%d</color>",
                 a, cp, a, p - cp, a, g, a, bm);
    else {
        // a skin's colorPerfect / colorGood, else the game's Perfect yellow and Good blue
        char ph[8] = "FFECA0", gh[8] = "B4E1FF";
        skinJudgeHex(ph, gh);
        snprintf(buf, sizeof(buf), "<color=#%s%02X>%d</color>  <color=#%s%02X>%d</color>  <color=#A0A0A0%02X>%d</color>", ph,
                 a, p, gh, a, g, a, bm);
    }
    g_judgeStr.set(str(buf));
    setTextStr(judge, (Il2CppString*)g_judgeStr.get());
}

// ---- 隐藏分数和连击: every UI Graphic field named *score* / *combo* on ScoreControl and LevelUICont (the combo number,
// the COMBO / AUTOPLAY label, the score) is disabled each frame; our lines are their children and go with them ----
struct Hidden {
    Il2CppClass* k;
    int off[10];
    int n;
    bool scanned;
};
static Hidden g_hideSC, g_hideUI;
static Il2CppClass* g_LevelUI = nullptr;
static Handle g_hideFor, g_levelUI;  // the ScoreControl the LevelUICont was looked up for
static bool hasWord(const char* s) {  // "score" or "combo", any case
    size_t n = strlen(s);
    for (size_t i = 0; i + 5 <= n; i++) {
        char w[6];
        for (int j = 0; j < 5; j++) w[j] = (s[i + j] >= 'A' && s[i + j] <= 'Z') ? char(s[i + j] + 32) : s[i + j];
        w[5] = 0;
        if (!strcmp(w, "score") || !strcmp(w, "combo")) return true;
    }
    return false;
}
static bool isGraphic(Il2CppClass* k) {
    for (; k; k = il2cpp_class_get_parent(k))
        if (!strcmp(il2cpp_class_get_name(k), "Graphic")) return true;
    return false;
}
static void scanHidden(Hidden& h, Il2CppClass* k) {
    h.k = k;
    h.n = 0;
    h.scanned = true;
    char names[240] = "";
    void* it = nullptr;
    while (FieldInfo* f = il2cpp_class_get_fields(k, &it)) {
        if (h.n >= 10) break;
        if (il2cpp_field_get_flags(f) & 0x10) continue;  // static
        const Il2CppType* t = il2cpp_field_get_type(f);
        if (il2cpp_type_get_type(t) != IL2CPP_TYPE_CLASS || !isGraphic(il2cpp_class_from_type(t))) continue;
        const char* name = il2cpp_field_get_name(f);
        if (!hasWord(name)) continue;
        h.off[h.n++] = (int)il2cpp_field_get_offset(f);
        size_t l = strlen(names);
        snprintf(names + l, sizeof names - l, "%s%s", l ? ", " : "", name);
    }
    LOG("hud: hide score/combo on %s: %s", il2cpp_class_get_name(k), h.n ? names : "nothing found");
}
static void hideIn(Obj* o, Hidden& h) {
    if (!o) return;
    if (!h.scanned) scanHidden(h, il2cpp_object_get_class(o));
    for (int i = 0; i < h.n; i++) {
        Obj* g = fld<Obj*>(o, h.off[i]);
        if (alive(g)) setEnabled(g, false);
    }
}
static void hideScoreCorner(Obj* sc) {
    hideIn(sc, g_hideSC);
    if (sc != g_hideFor.get()) {  // a new level: LevelUICont looked up once
        g_hideFor.set(sc);
        g_levelUI.clear();
        if (!g_LevelUI) g_LevelUI = findClass("", "LevelUICont");
        Obj* ui = g_LevelUI ? findObjectOfType(g_LevelUI) : nullptr;
        if (ui) g_levelUI.set(ui);
    }
    Obj* ui = (Obj*)g_levelUI.get();
    if (alive(ui)) hideIn(ui, g_hideUI);
}

// ---- 判定偏移条 + EARLY / LATE, under the combo (and under the judgement counts when they are on) ----
// A bar over the game's windows (Bad .. Good .. Perfect .. Critical, early on the left), in the judgement colours (a
// skin's colorPerfect / colorGood when one is on), and a pointer that springs to each hit's offset: a damped spring
// (fast, a hint of overshoot, so the move reads as a curve), with a 2 ms dead band so near-identical hits leave it
// still. Taps and hold heads only (drags / flicks have no timing). The hits come from critical.cpp, which sees every
// tap the update is about to judge.
static Handle g_offSc, g_offRoot, g_offGroup, g_offPtr, g_offText, g_offSeg[4], g_offTick;
static float g_offW = 0, g_offBarH = 3, g_offBarY = 0;
static float g_win[4] = {-1, -1, -1, -1};  // the windows the bar is laid out for
static bool g_winCrit = false, g_winSkin = false;
static float g_px = 0, g_pv = 0, g_pTarget = 0;  // pointer: shown offset, velocity, target (seconds; + = early)
static bool g_pMoving = false;
static Color g_pCol{1, 1, 1, 1};
static float g_pColT = 9;               // time since the pointer took a judgement's colour
static float g_txtT = 9, g_groupA = -1;  // EARLY / LATE age; the group alpha last set
static Obj* g_recent[64];                // taps / holds already reported (a flagged tap can stay a frame)
static int g_recentI = 0;
static float g_hits[16];
static int g_hitN = 0;

void hudOffsetHit(Obj* note, float offset) {
    if (!g_cfg.offsetBar && !g_cfg.earlyLate) return;
    for (Obj* r : g_recent)
        if (r == note) return;
    g_recent[g_recentI++ & 63] = note;
    if (g_hitN < 16) g_hits[g_hitN++] = offset;
}

static int gradeOf(float off) {  // 0 Critical, 1 Perfect, 2 Good, 3 Bad, -1 outside (a miss: not shown)
    float t = off < 0 ? -off : off;
    return t <= g_win[0] ? 0 : t < g_win[1] ? 1 : t < g_win[2] ? 2 : t < g_win[3] ? 3 : -1;
}
static Color gradeColor(int g) {
    Color sk[2];
    bool skin = skinLineColors(sk);
    if (g == 3) return rgb(0xE8826E);
    if (g == 2) return skin ? sk[1] : rgb(0xB4E1FF);
    if (skin) return sk[0];
    if (critEnabled()) return rgb(g == 0 ? 0xFFF08A : 0xFFCB7A);
    return rgb(0xFFECA0);
}
static float easeOut3(float u) {
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    return 1 - (1 - u) * (1 - u) * (1 - u);
}

// The bar is one texture, made again when the windows or colours change: the zones' colours blend into each other
// (smoothstep across each boundary, over a third of the narrower zone beside it) and both ends are round (a capsule
// mask, anti-aliased). Its pixel size keeps the bar's aspect, so the round ends stay round.
static struct {
    bool looked, ok;
    Il2CppClass *C32, *Img;
    Il2CppClass* Tex2D;
    const MethodInfo *ctor, *setPx, *apply, *wrap, *spCreate, *imgSprite;
} GB;
static Handle g_barTex, g_barSprite;
static void gbLook() {
    if (GB.looked) return;
    GB.looked = true;
    GB.Tex2D = findClass("UnityEngine", "Texture2D");
    GB.C32 = findClass("UnityEngine", "Color32");
    GB.Img = findClass("UnityEngine.UI", "Image");
    Il2CppClass* Tex = findClass("UnityEngine", "Texture");
    Il2CppClass* Sp = findClass("UnityEngine", "Sprite");
    GB.ctor = GB.Tex2D ? findMethod(GB.Tex2D, ".ctor", 2, IL2CPP_TYPE_I4) : nullptr;
    GB.setPx = GB.Tex2D ? findMethod(GB.Tex2D, "SetPixels32", 1) : nullptr;
    GB.apply = GB.Tex2D ? findMethod(GB.Tex2D, "Apply", 0) : nullptr;
    GB.wrap = Tex ? findMethod(Tex, "set_wrapMode", 1) : nullptr;
    GB.spCreate = Sp ? findMethod(Sp, "Create", 7) : nullptr;
    GB.imgSprite = GB.Img ? findMethod(GB.Img, "set_sprite", 1) : nullptr;
    GB.ok = GB.Tex2D && GB.C32 && GB.ctor && GB.setPx && GB.apply && GB.spCreate && GB.imgSprite;
    LOG("hud: timing bar gradient %s", GB.ok ? "on" : "OFF (texture methods missing, plain colours)");
}
static float smooth01(float u) {
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    return u * u * (3 - 2 * u);
}
static void offLayout() {
    Obj* bar = (Obj*)g_offSeg[0].get();
    if (!alive(bar)) return;
    bool critShown = critEnabled() && !skinActive();
    // zones from the centre out: their outer edges and colours
    float edge[4];
    Color col[4];
    int nz = 0;
    if (critShown) edge[nz] = g_win[0], col[nz++] = gradeColor(0);
    edge[nz] = g_win[1], col[nz++] = gradeColor(1);
    edge[nz] = g_win[2], col[nz++] = gradeColor(2);
    edge[nz] = g_win[3], col[nz++] = gradeColor(3);
    gbLook();
    if (!GB.ok) {  // no textures: the bar in the Perfect colour at least
        Color c = col[critShown ? 1 : 0];
        setColor(bar, Color{c.r, c.g, c.b, 0.85f});
        return;
    }
    const int H = 16;
    int W = (int)(H * g_offW / g_offBarH + 0.5f);
    if (W < 64) W = 64;
    if (W > 2048) W = 2048;
    Il2CppArray* px = newArray(GB.C32, (uintptr_t)(W * H));
    if (!px) return;
    uint8_t* p = (uint8_t*)arrayData(px);
    float r = H * 0.5f, b = g_win[3];
    for (int x = 0; x < W; x++) {
        // the colour at this offset
        float xc = (x + 0.5f) / W * 2 - 1, t = (xc < 0 ? -xc : xc) * b;
        int z = 0;
        while (z < nz - 1 && t >= edge[z]) z++;
        Color c = col[z];
        for (int k = 0; k < nz - 1; k++) {  // near a boundary: blend the two zones
            float B = edge[k], wIn = B - (k ? edge[k - 1] : 0), wOut = edge[k + 1] - B;
            float hw = (wIn < wOut ? wIn : wOut) * 0.33f;
            if (hw <= 0 || t < B - hw || t > B + hw) continue;
            float u = smooth01((t - (B - hw)) / (2 * hw));
            Color a = col[k], o = col[k + 1];
            c = Color{a.r + (o.r - a.r) * u, a.g + (o.g - a.g) * u, a.b + (o.b - a.b) * u, 1};
        }
        // the round ends: distance outside the capsule's straight part
        float fx = x + 0.5f, dx = fx < r ? r - fx : fx > W - r ? fx - (W - r) : 0;
        for (int y = 0; y < H; y++) {
            float dy = (y + 0.5f) - r, d = __builtin_sqrtf(dx * dx + dy * dy);
            float a = r - d + 0.5f;
            a = a < 0 ? 0 : a > 1 ? 1 : a;
            uint8_t* q = p + (y * W + x) * 4;
            q[0] = (uint8_t)(c.r * 255 + 0.5f);
            q[1] = (uint8_t)(c.g * 255 + 0.5f);
            q[2] = (uint8_t)(c.b * 255 + 0.5f);
            q[3] = (uint8_t)(a * 0.88f * 255 + 0.5f);
        }
    }
    Obj* oldTex = (Obj*)g_barTex.get();
    Obj* oldSp = (Obj*)g_barSprite.get();
    Obj* tex = il2cpp_object_new(GB.Tex2D);
    callm<void>(GB.ctor, tex, W, H);
    if (GB.wrap) callm<void>(GB.wrap, tex, 1);  // clamp: no bleeding between the two ends
    callm<void>(GB.setPx, tex, px);
    callm<void>(GB.apply, tex);
    Obj* sp = callm<Obj*>(GB.spCreate, tex, Rect{0, 0, (float)W, (float)H}, Vec2{0.5f, 0.5f}, 100.f, 0u, 0, Vec4{0, 0, 0, 0});
    callm<void>(GB.imgSprite, bar, sp);
    setColor(bar, Color{1, 1, 1, 1});
    g_barTex.set(tex);
    g_barSprite.set(sp);
    if (alive(oldSp)) destroy(oldSp);  // the last layout's (ours, referenced by nothing else now)
    if (alive(oldTex)) destroy(oldTex);
}

static void offBuild(Obj* sc) {
    g_offSc.set(sc);
    g_offRoot.clear();
    g_offPtr.clear();
    g_offText.clear();
    g_offTick.clear();
    for (Handle& h : g_offSeg) h.clear();
    for (Obj*& r : g_recent) r = nullptr;
    g_hitN = 0;
    g_px = g_pv = g_pTarget = 0;
    g_pMoving = true;
    g_pColT = g_txtT = 9;
    g_groupA = -1;
    g_win[0] = -1;
    Obj* label = fld<Obj*>(sc, S.comboLabel);
    Rect r;
    if (!alive(label) || !rectOf(rt(label), &r)) return;
    int fs = textFontSize(label);
    if (fs < 12) fs = 24;
    Obj* score = fld<Obj*>(sc, S.scoreText);
    float ratio = alive(score) ? lineRatio(score) : 1.15f;
    int a = textAlignment(label), row = a / 3, col = a % 3;
    float gh = fs * ratio, top = row == 0 ? r.h : row == 1 ? (r.h + gh) / 2 : gh;
    // under the label's glyphs, and under the judgement counts (a line fs tall, box 1.5 fs) when they show
    float y = top - gh - (g_cfg.judgeShow && !g_cfg.hideScore ? fs * 1.5f : 0) - fs * 0.15f;
    float ax = col * 0.5f;
    g_offW = fs * 10.f;
    g_offBarH = fs * 0.16f < 3 ? 3 : fs * 0.16f;
    Obj* root = newUI("PhiMeOffset", rt(label));
    place(root, Vec2{ax, 0}, Vec2{ax, 1}, Vec2{0, y}, Vec2{g_offW, fs * 2.6f});
    g_offRoot.set(root);
    g_offGroup.set(addCanvasGroup(gameObject(root)));
    setScale(root, g_cfg.offsetSize / 100.f);  // 偏移条大小 (from its top: it grows downward, away from the combo)
    float textY = -fs * 0.5f;
    if (g_cfg.offsetBar) {
        g_offBarY = -fs * 0.55f;
        // the bar: one image, its gradient sprite made by offLayout
        Obj* s = image(root, "bar", SPR_SOLID, Color{1, 1, 1, 0}, Vec2{0.5f, 1}, Vec2{0, g_offBarY}, Vec2{g_offW, g_offBarH});
        place(rt(s), Vec2{0.5f, 1}, Vec2{0.5f, 0.5f}, Vec2{0, g_offBarY}, Vec2{g_offW, g_offBarH});
        g_offSeg[0].set(s);
        float tw = fs * 0.1f < 2 ? 2 : fs * 0.1f;
        Obj* tick = image(root, "centre", SPR_ROUND, Color{1, 1, 1, 0.75f}, Vec2{0.5f, 1}, Vec2{0, g_offBarY},
                          Vec2{tw, g_offBarH * 2.4f}, tw * 0.5f);
        place(rt(tick), Vec2{0.5f, 1}, Vec2{0.5f, 0.5f}, Vec2{0, g_offBarY}, Vec2{tw, g_offBarH * 2.4f});
        g_offTick.set(tick);
        Vec2 ps{fs * 0.22f, fs * 0.95f};
        Obj* p = image(root, "pointer", SPR_ROUND, rgb(0xFFFFFF), Vec2{0.5f, 1}, Vec2{0, g_offBarY}, ps, ps.x * 0.5f);
        place(rt(p), Vec2{0.5f, 1}, Vec2{0.5f, 0.5f}, Vec2{0, g_offBarY}, ps);
        g_offPtr.set(p);
        textY = g_offBarY - fs * 1.05f;
    }
    if (g_cfg.earlyLate) {
        int ts = (int)(fs * 0.8f + 0.5f);
        Obj* t = text(root, "", ts, Color{1, 1, 1, 0}, MiddleCenter, true, Vec2{0.5f, 1}, Vec2{0.5f, 0.5f}, Vec2{0, textY},
                      Vec2{fs * 6.f, ts * 1.5f});
        setTextFont(t, textFont(label));
        g_offText.set(t);
    }
    LOG("hud: timing bar %s (size %d%%), EARLY / LATE %s (small P %s), under a %d px combo label (align %d)",
        g_cfg.offsetBar ? "on" : "off", g_cfg.offsetSize, g_cfg.earlyLate ? "on" : "off", g_cfg.earlyLatePerfect ? "too" : "no",
        fs, a);
}

static void offTick(Obj* sc) {
    if (!g_cfg.offsetBar && !g_cfg.earlyLate) return;
    if (sc != g_offSc.get()) offBuild(sc);
    Obj* root = (Obj*)g_offRoot.get();
    if (!alive(root)) {
        g_hitN = 0;
        return;
    }
    float dt = unscaledDt();
    if (dt > 0.05f) dt = 0.05f;
    if (dt < 0) dt = 0;
    // windows (course mode changes them every frame by a hair) and colours (critical / skin switched)
    float w[4];
    critWindows(&w[0], &w[1], &w[2], &w[3]);
    bool crit = critEnabled(), skin = skinActive();
    bool relayout = crit != g_winCrit || skin != g_winSkin;
    // (2 ms: course mode moves the windows with the frame time; the texture is not remade for every hair)
    for (int i = 0; i < 4; i++) relayout = relayout || w[i] - g_win[i] > 0.002f || g_win[i] - w[i] > 0.002f;
    if (relayout) {
        for (int i = 0; i < 4; i++) g_win[i] = w[i];
        g_winCrit = crit;
        g_winSkin = skin;
        offLayout();
    }
    // this frame's hits: the pointer goes to their mean, the text shows the worst of them
    if (g_hitN) {
        float sum = 0, textOff = 0;
        int n = 0, worst = -1;
        for (int i = 0; i < g_hitN; i++) {
            int g = gradeOf(g_hits[i]);
            if (g < 0) continue;
            sum += g_hits[i];
            n++;
            if (g > worst) worst = g, textOff = g_hits[i];
        }
        g_hitN = 0;
        if (n) {
            float mean = sum / n, d = mean - g_pTarget;
            if (d > 0.002f || d < -0.002f) {  // dead band: a near-identical hit does not nudge the pointer
                g_pTarget = mean;
                g_pMoving = true;
            }
            g_pCol = gradeColor(gradeOf(mean));
            g_pColT = 0;
            bool say = worst >= 2 || (worst == 1 && g_cfg.earlyLatePerfect);
            Obj* t = (Obj*)g_offText.get();
            if (say && alive(t)) {
                bool early = textOff > 0;
                setText(t, early ? "EARLY" : "LATE");
                g_txtT = 0;
                Color c = rgb(early ? 0x8FD3FF : 0xFF9E7A);
                setColor(t, c);
            }
        }
    }
    // the pointer: a damped spring toward the target (two half steps on a long frame keep it stable)
    Obj* p = (Obj*)g_offPtr.get();
    if (alive(p)) {
        if (g_pMoving) {
            const float w0 = 34.f, zeta = 0.68f;
            int steps = dt > 0.017f ? 2 : 1;
            float h = dt / steps;
            for (int s = 0; s < steps; s++) {
                float acc = w0 * w0 * (g_pTarget - g_px) - 2 * zeta * w0 * g_pv;
                g_pv += acc * h;
                g_px += g_pv * h;
            }
            float e = g_pTarget - g_px;
            if (e < 0.00005f && e > -0.00005f && g_pv < 0.002f && g_pv > -0.002f) {
                g_px = g_pTarget;
                g_pv = 0;
                g_pMoving = false;
            }
            float lim = g_win[3] > 0 ? g_win[3] : 0.22f, x = g_px > lim ? lim : g_px < -lim ? -lim : g_px;
            // early on the left
            setPos(rt(p), Vec2{-x / lim * (g_offW / 2), g_offBarY});
        }
        if (g_pColT < 0.6f) {  // the hit's colour, back to white
            g_pColT += dt;
            float u = easeOut3(g_pColT / 0.6f);
            setColor(p, Color{g_pCol.r + (1 - g_pCol.r) * u, g_pCol.g + (1 - g_pCol.g) * u, g_pCol.b + (1 - g_pCol.b) * u, 1});
        }
    }
    // EARLY / LATE: pops in (scale 1.25 -> 1), stays, fades out
    Obj* t = (Obj*)g_offText.get();
    if (alive(t) && g_txtT < 0.8f) {
        g_txtT += dt;
        float s = 1 + 0.25f * (1 - easeOut3(g_txtT / 0.12f));
        setScale(rt(t), s);
        float a = g_txtT < 0.45f ? 1.f : 1 - (g_txtT - 0.45f) / 0.3f;
        Color c = getColor(t);
        setColor(t, Color{c.r, c.g, c.b, a < 0 ? 0 : a});
    }
    // with the HUD: the combo label's fade (the game fades the corner in at the start)
    Obj* label = fld<Obj*>(sc, S.comboLabel);
    float ga = alive(label) ? getColor(label).a : 1;
    Obj* grp = (Obj*)g_offGroup.get();
    if (alive(grp) && ga != g_groupA) {
        setGroupAlpha(grp, ga);
        g_groupA = ga;
    }
}

static void hk_SC_Update(Obj* self, const MethodInfo* m) {
    orig_SC_Update(self, m);
    PerfScope ps(PF_HUD);
    configLoad();
    offTick(self);
    if (g_cfg.hideScore) {
        hideScoreCorner(self);
        return;  // no accuracy line / counts to keep up either
    }
    bool crit = critEnabled();
    if (!g_cfg.accShow && !g_cfg.judgeShow && !crit) return;
    if (self != g_sc.get()) build(self);  // a new level (or a retry)
    Obj* acc = (Obj*)g_acc.get();
    Obj* judge = (Obj*)g_judge.get();
    Il2CppString* scoreStr = crit ? critScore(self) : nullptr;
    bool swapped = g_cfg.accShow && g_cfg.accSwap && alive(acc);
    if (scoreStr && !swapped) setTextStr(fld<Obj*>(self, S.scoreText), scoreStr);
    if (g_cfg.accShow && alive(acc)) updateAcc(self, acc, scoreStr);
    if (g_cfg.judgeShow && alive(judge)) updateJudge(self, judge, crit);
}

bool hudInstall() {
    Il2CppClass* Score = findClass("", "ScoreControl");
    Il2CppClass* LI = findClass("", "LevelInformation");
    if (!Score || !LI) return false;
    S.levelInfo = fieldOffset(Score, "levelInformation");
    S.perfect = fieldOffset(Score, "perfect");
    S.good = fieldOffset(Score, "good");
    S.bad = fieldOffset(Score, "bad");
    S.miss = fieldOffset(Score, "miss");
    S.scoreText = fieldOffset(Score, "score");
    S.comboLabel = fieldOffset(Score, "comboText");
    S.liNotes = fieldOffset(LI, "numOfNotes");
    S.scoreValue = fieldOffset(Score, "_score");
    const int must[] = {S.levelInfo, S.perfect, S.good, S.bad, S.miss, S.scoreText, S.comboLabel, S.liNotes,
                        S.scoreValue};
    for (int v : must)
        if (v < 0) return false;
    orig_SC_Update = (VoidFn)hookMethod(findMethod(Score, "Update", 0), (void*)hk_SC_Update);
    return orig_SC_Update != nullptr;
}
