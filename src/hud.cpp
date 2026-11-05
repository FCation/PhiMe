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

static void hk_SC_Update(Obj* self, const MethodInfo* m) {
    orig_SC_Update(self, m);
    PerfScope ps(PF_HUD);
    configLoad();
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
