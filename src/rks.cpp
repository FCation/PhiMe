// RKS with 3 decimals, and a count-up on the result screen.
//
// The top-right player bar (PlayerBarControl: avatar, challenge rank, RKS) writes GameInformation.RankingScore
// into its `rank` Text once, in Start, with 2 decimals; the account page (StatsFrameController.rks) the same. Both
// are rewritten with 3 decimals right after their Start.
// On the result screen the bar shows the RKS from before this play (the last one we showed) and we watch the game's
// RankingScore: when it moved by at least 0.001, the number warms to gold and counts up to the new value while a
// thin gold line draws itself under it (right to left), a small dark capsule with "+0.012" drops in below, the
// number pulses once as it lands and cools back to white; the line and the capsule stay. The new value is then the
// one remembered for the next result.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

typedef void (*VoidFn)(Obj*, const MethodInfo*);
static VoidFn orig_PBC_Start, orig_SFC_Start;
static struct {
    Il2CppClass *GI, *LOC;
    const MethodInfo *get, *overflow;
    int pbcRank, sfcRks;
} K;
static float g_known = -1;  // the RKS last shown (outside a result screen, or after a result's count-up)

static bool rksNow(float* out) {
    FieldInfo* f = K.GI ? il2cpp_class_get_field_from_name(K.GI, "_main") : nullptr;
    Obj* gi = nullptr;
    if (f) il2cpp_field_static_get_value(f, &gi);
    if (!gi || !K.get) return false;
    *out = callm<float>(K.get, gi);
    return *out >= 0 && *out < 100;
}
static int thousandths(float v) { return (int)(v * 1000 + 0.5f); }
// The game writes its 2-decimal RKS again after Start (1.3.3 and older wrote ours once: the bar was back to 2
// decimals outside a count-up). Every text we write is kept with the string we put there; keepFrame writes it again
// whenever the game has replaced it.
struct Kept {
    Handle t, s;
};
static Kept g_kept[4];
static void showRks(Obj* t, float v) {
    char b[24];
    snprintf(b, sizeof b, "%d.%03d", thousandths(v) / 1000, thousandths(v) % 1000);
    Il2CppString* s = str(b);
    setTextStr(t, s);
    int slot = -1;
    for (int i = 0; i < 4 && slot < 0; i++)
        if (g_kept[i].t.get() == t) slot = i;
    for (int i = 0; i < 4 && slot < 0; i++)
        if (!alive((Obj*)g_kept[i].t.get())) slot = i;
    if (slot < 0) slot = 0;
    if (g_kept[slot].t.get() != t) g_kept[slot].t.set(t);
    g_kept[slot].s.set(s);
}
static void wide(Obj* t) {
    if (K.overflow) callm<void>(K.overflow, t, 1);  // Overflow: one more digit must not be cut off
}

// ---- the result screen ----
static Handle g_text, g_line, g_pill, g_pillText;
static float g_from = 0, g_to = 0, g_watch = -1, g_t = -1, g_lineW = 0, g_lineH = 2;
static Color g_white{1, 1, 1, 1};  // the number's own color
static Vec2 g_pillPos;
static const float kWait = 0.9f, kCount = 1.4f, kPulse = 0.35f, kWarm = 0.3f, kCool = 0.6f, kPillDelay = 0.15f,
                   kPillIn = 0.45f, kEnd = kCount + 0.2f + kCool;
static const Color kGold{1.f, 0.84f, 0.5f, 1.f};
static const Color kAmber{0.86f, 0.6f, 0.16f, 1.f};  // the line sits on the bar's white box: needs contrast

static void stopAnim() {
    g_text.clear();
    g_watch = g_t = -1;
}

static Color mix(Color a, Color b, float u) {
    return Color{a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u, a.a + (b.a - a.a) * u};
}
static float easeOut(float u) {
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    return 1 - (1 - u) * (1 - u) * (1 - u);
}

// the gold line under the digits and the capsule below it, both right-aligned with the digits' right end
static void makeAccents(Obj* t, int delta) {
    Rect r{0, 0, 120, 40};
    rectOf(rt(t), &r);
    int fs = textFontSize(t);
    if (fs <= 0) fs = (int)(r.h * 0.7f);
    float pw = textPreferredWidth(t);
    if (pw <= 0 || pw > r.w * 3) pw = r.w;
    int col = textAlignment(t) % 3;
    float right = col == 0 ? r.x + pw : col == 1 ? r.x + r.w * 0.5f + pw * 0.5f : r.x + r.w;
    // local coordinates are relative to the Text's pivot: anchor both at that pivot
    RectInfo ri;
    Vec2 piv = rectInfo(rt(t), &ri) ? ri.pivot : Vec2{0.5f, 0.5f};
    float lineY = r.y + r.h * 0.12f;
    float lh = fs * 0.06f < 2 ? 2 : fs * 0.06f;
    g_lineW = pw;
    g_lineH = lh;
    Obj* line = image(rt(t), "PhiMeRksLine", SPR_ROUND, kAmber, Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{0, lh}, lh * 0.5f);
    if (line) {
        place(rt(line), piv, Vec2{1, 0.5f}, Vec2{right, lineY}, Vec2{0, lh});
        g_line.set(line);
    }
    int bs = (int)(fs * 0.95f + 0.5f);
    char b[24];
    snprintf(b, sizeof b, "+%d.%03d", delta / 1000, delta % 1000);
    float tw = textWidth(b, bs, true);
    Vec2 size{tw + bs * 1.2f, bs * 1.55f};
    // below the bar (clear of its white box), pushed out toward the screen's right edge
    float pillRight = right + fs * 0.6f;
    Vec2 s = screenSize(), e0, e1;
    if (screenToLocal(rt(t), Vec2{s.x - s.x * 0.012f, s.y * 0.5f}, &e0) &&
        screenToLocal(rt(t), Vec2{s.x - s.x * 0.012f - 100, s.y * 0.5f}, &e1) && e0.x > e1.x)
        pillRight = e0.x;  // the screen's right edge, minus a small margin
    if (pillRight < right) pillRight = right;
    g_pillPos = Vec2{pillRight, r.y - fs * 0.3f};
    Obj* pill = image(rt(t), "PhiMeRksGain", SPR_ROUND, Color{0.05f, 0.05f, 0.07f, 0}, Vec2{0.5f, 0.5f}, Vec2{0, 0},
                      size, size.y * 0.5f);
    if (pill) {
        place(rt(pill), piv, Vec2{1, 1}, g_pillPos, size);
        Obj* pt = text(rt(pill), b, bs, Color{kGold.r, kGold.g, kGold.b, 0}, MiddleCenter, true, Vec2{0.5f, 0.5f},
                       Vec2{0.5f, 0.5f}, Vec2{0, 1}, Vec2{size.x + bs, size.y});
        g_pill.set(pill);
        g_pillText.set(pt);
    }
}

static void rksFrame(void*) {
    Obj* t = (Obj*)g_text.get();
    if (!t) return;
    if (!alive(t)) {
        stopAnim();
        return;
    }
    float dt = unscaledDt();
    if (g_watch >= 0) {  // waiting for the game's new RankingScore (and for the screen to settle)
        g_watch += dt;
        float now;
        if (!rksNow(&now)) return;
        int d = thousandths(now) - thousandths(g_from);
        if (d >= 1 && g_watch >= kWait) {
            g_to = now;
            g_watch = -1;
            g_t = 0;
            g_white = getColor(t);
            makeAccents(t, d);
            LOG("rks: %.4f -> %.4f (+%d.%03d), counting up", g_from, g_to, d / 1000, d % 1000);
        } else if (g_watch > 12) {
            showRks(t, now);
            g_known = now;
            stopAnim();
        }
        return;
    }
    if (g_t < 0) return;
    g_t += dt;
    float e = easeOut(g_t / kCount);
    showRks(t, g_from + (g_to - g_from) * e);
    float p = (g_t - kCount) / kPulse;  // a single pulse when the count lands
    setScale(rt(t), p > 0 && p < 1 ? 1 + 0.12f * 4 * p * (1 - p) : 1.f);
    // warm to gold while counting, cool back once it landed
    float warm = g_t < kCount ? easeOut(g_t / kWarm) : 1 - easeOut((g_t - kCount - 0.2f) / kCool);
    if (warm < 0) warm = 0;
    setColor(t, mix(g_white, Color{kAmber.r, kAmber.g, kAmber.b, g_white.a}, warm));
    Obj* line = (Obj*)g_line.get();
    if (alive(line)) {  // draws itself with the count, then settles a little dimmer
        setSize(rt(line), Vec2{g_lineW * e, g_lineH});
        float dim = g_t > kCount ? easeOut((g_t - kCount) / kCool) : 0;
        setColor(line, Color{kAmber.r, kAmber.g, kAmber.b, 1 - 0.3f * dim});
    }
    Obj* pill = (Obj*)g_pill.get();
    Obj* pt = (Obj*)g_pillText.get();
    if (alive(pill)) {  // drops in from just above while it fades in
        float a = easeOut((g_t - kPillDelay) / kPillIn);
        setColor(pill, Color{0.05f, 0.05f, 0.07f, 0.6f * a});
        if (alive(pt)) setColor(pt, Color{kGold.r, kGold.g, kGold.b, a});
        setPos(rt(pill), Vec2{g_pillPos.x, g_pillPos.y + 12 * (1 - a)});
    }
    if (g_t >= kEnd) {
        setScale(rt(t), 1);
        setColor(t, g_white);
        showRks(t, g_to);
        g_known = g_to;
        g_text.clear();
        g_t = -1;  // the line and the capsule stay
    }
}

// every frame: a kept RKS text the game rewrote gets 3 decimals again (the one being animated keeps its own value)
static void keepFrame(void*) {
    for (Kept& k : g_kept) {
        Obj* t = (Obj*)k.t.get();
        if (!t) continue;
        if (!alive(t)) {
            k.t.clear();
            k.s.clear();
            continue;
        }
        Il2CppString* mine = (Il2CppString*)k.s.get();
        if (!mine || textStr(t) == mine) continue;
        float now;
        if (t == g_text.get()) setTextStr(t, mine);  // the result's count-up (or its wait): the value we showed
        else if (rksNow(&now)) showRks(t, now);
        static int logged = 0;
        if (logged++ < 2) LOG("rks: the game rewrote the RKS text, back to 3 decimals");
    }
}
static void keepOn() {
    static bool registered = false;
    if (!registered) {
        registered = true;
        onEveryFrame(keepFrame);
    }
}

static void hk_PBC_Start(Obj* self, const MethodInfo* m) {
    orig_PBC_Start(self, m);
    Obj* t = K.pbcRank >= 0 ? fld<Obj*>(self, K.pbcRank) : nullptr;
    float now;
    if (!alive(t) || !rksNow(&now)) return;
    wide(t);
    keepOn();
    bool result = K.LOC && findObjectOfType(K.LOC);
    if (!result || g_known < 0) {
        showRks(t, now);
        g_known = now;
        return;
    }
    // the result screen: from the RKS before this play, to whatever the game makes of it
    stopAnim();
    g_line.clear();
    g_pill.clear();
    g_pillText.clear();
    g_text.set(t);
    g_from = g_known;
    showRks(t, g_from);
    g_watch = 0;
    onEveryFrame(rksFrame);
    PM_ONCE("rks: player bar on the result screen, watching for a change");
}

static void hk_SFC_Start(Obj* self, const MethodInfo* m) {
    orig_SFC_Start(self, m);
    Obj* t = K.sfcRks >= 0 ? fld<Obj*>(self, K.sfcRks) : nullptr;
    float now;
    if (!alive(t) || !rksNow(&now)) return;
    wide(t);
    keepOn();
    showRks(t, now);
}

bool rksInstall() {
    K.GI = findClass("", "GameInformation");
    K.LOC = findClass("", "LevelOverControl");
    Il2CppClass* PBC = findClass("", "PlayerBarControl");
    Il2CppClass* SFC = findClass("", "StatsFrameController");
    Il2CppClass* Txt = findClass("UnityEngine.UI", "Text");
    K.get = K.GI ? findMethod(K.GI, "get_RankingScore", 0) : nullptr;
    K.overflow = Txt ? findMethod(Txt, "set_horizontalOverflow", 1) : nullptr;
    K.pbcRank = PBC ? fieldOffset(PBC, "rank") : -1;
    K.sfcRks = SFC ? fieldOffset(SFC, "rks") : -1;
    const MethodInfo* pbcStart = PBC ? findMethod(PBC, "Start", 0) : nullptr;
    const MethodInfo* sfcStart = SFC ? findMethod(SFC, "Start", 0) : nullptr;
    if (K.get && pbcStart && K.pbcRank >= 0) orig_PBC_Start = (VoidFn)hookMethod(pbcStart, (void*)hk_PBC_Start);
    if (K.get && sfcStart && K.sfcRks >= 0) orig_SFC_Start = (VoidFn)hookMethod(sfcStart, (void*)hk_SFC_Start);
    LOG("rks: 3 decimals in the player bar %s, account page %s", orig_PBC_Start ? "on" : "off",
        orig_SFC_Start ? "on" : "off");
    return orig_PBC_Start != nullptr;
}
