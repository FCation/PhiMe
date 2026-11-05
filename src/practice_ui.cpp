// Section editor (选段练习): an editing-app style timeline at the bottom of the screen.
//   two fingers: zoom around the pinch centre     one finger on an end point: drag it
//   one finger elsewhere on the timeline: pan     while dragging an end point the backdrop turns clear and
//   the chart jumps to that time so you can see where you are.
// Plus a speed slider (0.05x - 2x, step 0.05), a note flow slider (0.5x - 20x, logarithmic, with a mark where
// flow x speed = 1 that the knob snaps to), AB-loop and keep-pitch switches, 取消 / 开始练习.
#include "practice.h"

using namespace il2cpp;
using namespace unity;

static const int C_CLAY = 0xD97757, C_IVORY = 0xFAF9F5, C_LINE = 0xE8E6DC, C_TEXT = 0x141413, C_MUTED = 0x87867F,
                 C_TRACK = 0xEDEBE3, C_TRACK_OFF = 0xE3E1D7;

static const float kSheetH = 540, kMargin = 24, kTimelineH = 96, kMinSpan = 1.0f;
static const int kTicks = 9;

static Handle g_root, g_dim, g_sel, g_hA, g_hB, g_readout, g_speedFill, g_speedKnob, g_speedText;
static Handle g_flowFill, g_flowKnob, g_flowText, g_flowMark;
static Handle g_tick[kTicks], g_tickLbl[kTicks];
static Handle g_loopTrack, g_loopKnob, g_pitchTrack, g_pitchKnob;
static float g_S = 1700, g_TW = 1620, g_SW = 800;  // sheet width, timeline width, speed slider width
static float g_vs = 0, g_ve = 1;                    // visible window (seconds)
static bool g_open = false;

enum Gesture { G_NONE, G_DRAG_A, G_DRAG_B, G_PAN, G_PINCH, G_SPEED, G_FLOW };
static Gesture g_gesture = G_NONE;
static float g_panX0, g_panVs0, g_pinchD0, g_pinchSpan0, g_pinchAnchorT, g_pinchAnchorX;
static float g_previewedT = -1;

bool editorIsOpen() { return g_open; }

// ---------- geometry (canvas units, origin at screen centre) ----------
static float canvasW() {
    Vec2 s = screenSize();
    return s.y > 0 ? s.x * canvasH() / s.y : 1920;
}
static float sheetTop() { return -canvasH() / 2 + kMargin + kSheetH; }
static float tlLeft() { return -g_S / 2 + 40; }
static float tlY() { return sheetTop() - 140; }
static float spLeft() { return -g_S / 2 + 230; }
static float spY() { return sheetTop() - 270; }
static float flY() { return sheetTop() - 370; }
static Vec2 toCanvas(Vec2 px) { return screenToCanvas(px); }
static float xAt(float t) { return (t - g_vs) / (g_ve - g_vs) * g_TW; }  // timeline-local x
static float timeAt(float localX) { return g_vs + localX / g_TW * (g_ve - g_vs); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void fmtTime(char* out, int cap, float t) {
    if (t < 0) t = 0;
    int cs = (int)(t * 100 + 0.5f);
    snprintf(out, cap, "%02d:%02d.%02d", cs / 6000, (cs / 100) % 60, cs % 100);
}

// ---------- painting ----------
static void paintSwitch(Handle& track, Handle& knob, bool on) {
    Obj* tr = (Obj*)track.get();
    Obj* kn = (Obj*)knob.get();
    if (!alive(tr) || !alive(kn)) return;
    setColor(tr, rgb(on ? C_CLAY : C_TRACK_OFF));
    setPos(rt(kn), Vec2{on ? 15.f : -15.f, 0});
}

// ---------- flow slider: 0.5x .. 20x on a log scale (20x is 40 times 0.5x) ----------
static const float kFlowMin = 0.5f, kFlowMax = 20.f;
static float expS(float x) {  // e^x for the slider's range (|x| < 10): x = n ln2 + r, series for e^r
    const float ln2 = 0.69314718f;
    int n = (int)(x / ln2 + (x >= 0 ? 0.5f : -0.5f));
    float r = x - n * ln2, term = 1, sum = 1;
    for (int i = 1; i < 9; i++) {
        term *= r / i;
        sum += term;
    }
    for (; n > 0; n--) sum *= 2;
    for (; n < 0; n++) sum *= 0.5f;
    return sum;
}
static float logS(float v) {  // ln v, Newton on expS
    if (v <= 0) return -10;
    float y = 0;
    for (float t = v; t > 2; t *= 0.5f) y += 0.69314718f;
    for (float t = v; t < 0.5f; t *= 2) y -= 0.69314718f;
    for (int i = 0; i < 6; i++) {
        float e = expS(y);
        y += 2 * (v - e) / (v + e);
    }
    return y;
}
static float flowF(float v) { return clampf(logS(v / kFlowMin) / logS(kFlowMax / kFlowMin), 0, 1); }
static float flowAt(float f) { return kFlowMin * expS(clampf(f, 0, 1) * logS(kFlowMax / kFlowMin)); }
static float flowCancel() { return clampf(1.f / g_prac.speed, kFlowMin, kFlowMax); }  // flow x speed = 1
static float flowRound(float v) {  // 0.05 steps below 2x, 0.1 below 5x, 0.5 above
    float step = v < 2 ? 0.05f : v < 5 ? 0.1f : 0.5f;
    return clampf((float)(int)(v / step + 0.5f) * step, kFlowMin, kFlowMax);
}
static bool flowIsCancel() {
    float d = g_prac.flow - flowCancel();
    return d < 0.006f && d > -0.006f;
}

static void paintFlow() {
    float f = flowF(g_prac.flow);
    Obj* fill = (Obj*)g_flowFill.get();
    Obj* knob = (Obj*)g_flowKnob.get();
    if (alive(fill)) {
        setPos(rt(fill), Vec2{f * g_SW / 2, 0});
        setSize(rt(fill), Vec2{f * g_SW, 8});
    }
    if (alive(knob)) setPos(rt(knob), Vec2{f * g_SW, 0});
    Obj* mark = (Obj*)g_flowMark.get();
    if (alive(mark)) setPos(rt(mark), Vec2{flowF(flowCancel()) * g_SW, 0});
    char buf[32];
    if (g_prac.flow < 10) snprintf(buf, sizeof(buf), "%.2fx", g_prac.flow);
    else snprintf(buf, sizeof(buf), "%.1fx", g_prac.flow);
    Obj* t = (Obj*)g_flowText.get();
    if (alive(t)) {
        setText(t, buf);
        setColor(t, rgb(flowIsCancel() ? C_CLAY : C_TEXT));  // orange: notes move as fast as in the real song
    }
}

static void paintSpeed() {
    paintFlow();  // the 抵消 mark follows the speed
    float f = (g_prac.speed - 0.05f) / 1.95f;
    Obj* fill = (Obj*)g_speedFill.get();
    Obj* knob = (Obj*)g_speedKnob.get();
    if (alive(fill)) {
        setPos(rt(fill), Vec2{f * g_SW / 2, 0});
        setSize(rt(fill), Vec2{f * g_SW, 8});
    }
    if (alive(knob)) setPos(rt(knob), Vec2{f * g_SW, 0});
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2fx", g_prac.speed);
    Obj* t = (Obj*)g_speedText.get();
    if (alive(t)) setText(t, buf);
}

// Selection + handles + readout: cheap, done on every drag frame.
static float g_shownA = -1, g_shownB = -1;
static void paintSelection() {
    float xa = xAt(g_prac.a), xb = xAt(g_prac.b);
    Obj* sel = (Obj*)g_sel.get();
    if (alive(sel)) {
        float l = clampf(xa, 0, g_TW), r = clampf(xb, 0, g_TW);
        setActive(gameObject(sel), r > l);
        setPos(rt(sel), Vec2{(l + r) / 2, 0});
        setSize(rt(sel), Vec2{r - l, kTimelineH});
    }
    Obj* ha = (Obj*)g_hA.get();
    Obj* hb = (Obj*)g_hB.get();
    if (alive(ha)) {
        setActive(gameObject(ha), xa >= -1 && xa <= g_TW + 1);
        setPos(rt(ha), Vec2{xa, 0});
    }
    if (alive(hb)) {
        setActive(gameObject(hb), xb >= -1 && xb <= g_TW + 1);
        setPos(rt(hb), Vec2{xb, 0});
    }
    if (g_prac.a == g_shownA && g_prac.b == g_shownB) return;  // text only when the values change
    g_shownA = g_prac.a;
    g_shownB = g_prac.b;
    char a[16], b[16], buf[96];
    fmtTime(a, sizeof(a), g_prac.a);
    fmtTime(b, sizeof(b), g_prac.b);
    snprintf(buf, sizeof(buf), T("A  %s      B  %s      时长 %.2fs", "A  %s      B  %s      Length %.2fs"), a, b, g_prac.b - g_prac.a);
    Obj* ro = (Obj*)g_readout.get();
    if (alive(ro)) setText(ro, buf);
}

// Tick marks + labels: only when the visible window changes (zoom / pan).
static void paintTicks() {
    // ticks at a "nice" step for the current zoom
    static const float steps[] = {0.1f, 0.25f, 0.5f, 1, 2, 5, 10, 15, 30, 60, 120};
    float span = g_ve - g_vs, step = steps[10];
    for (float s : steps)
        if (span / s <= 7) {
            step = s;
            break;
        }
    float first = (float)(int)(g_vs / step) * step;
    if (first < g_vs) first += step;
    for (int i = 0; i < kTicks; i++) {
        float t = first + i * step;
        bool vis = t <= g_ve;
        Obj* tk = (Obj*)g_tick[i].get();
        Obj* lb = (Obj*)g_tickLbl[i].get();
        if (alive(tk)) {
            setActive(gameObject(tk), vis);
            if (vis) setPos(rt(tk), Vec2{xAt(t), -kTimelineH / 2 - 8});
        }
        if (alive(lb)) {
            setActive(gameObject(lb), vis);
            if (vis) {
                char buf[24];
                int m = (int)(t / 60);
                float s = t - m * 60;
                if (step < 1) snprintf(buf, sizeof(buf), "%d:%04.1f", m, s);
                else snprintf(buf, sizeof(buf), "%d:%02d", m, (int)(s + 0.5f));
                setText(lb, buf);
                setPos(rt(lb), Vec2{xAt(t), -kTimelineH / 2 - 30});
            }
        }
    }
}

static void paintTimeline() {
    paintTicks();
    paintSelection();
}

// ---------- see-through while dragging an end point ----------
// 0 = normal, 1 = dragging: backdrop fades out completely, the sheet fades out, and only the timeline stays
// faintly visible. Eased every frame so it fades instead of popping.
static Handle g_sheetGroup, g_timelineGroup;
static float g_fade = 0, g_fadeTarget = 0;
static void setDimClear(bool clear) { g_fadeTarget = clear ? 1.f : 0.f; }
static void fadeTick(float dt) {
    if (g_fade == g_fadeTarget) return;
    float k = dt * 9;
    if (k > 1) k = 1;
    g_fade += (g_fadeTarget - g_fade) * k;
    if (g_fade - g_fadeTarget < 0.004f && g_fadeTarget - g_fade < 0.004f) g_fade = g_fadeTarget;
    Obj* d = (Obj*)g_dim.get();
    if (alive(d)) setColor(d, Color{0.08f, 0.07f, 0.06f, 0.42f * (1 - g_fade)});
    setGroupAlpha((Obj*)g_sheetGroup.get(), 1 - g_fade);
    setGroupAlpha((Obj*)g_timelineGroup.get(), 1 - 0.68f * g_fade);  // ~1/3 left: just enough to aim
}

// ---------- tap targets (read from the touch list; no EventSystem needed) ----------
struct Tap {
    Vec2 c, s;  // centre / size in canvas units
    NativeCallback cb;
    void* user;
    Handle vis;
};
static Tap g_taps[12];
static int g_tapN = 0, g_tapDown = -1;

static void addTap(Obj* vis, Vec2 centre, Vec2 size, NativeCallback cb, void* user) {
    if (g_tapN >= 12) return;
    Tap& t = g_taps[g_tapN++];
    t.c = centre;
    t.s = size;
    t.cb = cb;
    t.user = user;
    t.vis.set(vis);
}
static int hitTap(Vec2 p) {
    for (int i = 0; i < g_tapN; i++) {
        Vec2 d{p.x - g_taps[i].c.x, p.y - g_taps[i].c.y};
        if (d.x > -g_taps[i].s.x / 2 - 8 && d.x < g_taps[i].s.x / 2 + 8 && d.y > -g_taps[i].s.y / 2 - 8 &&
            d.y < g_taps[i].s.y / 2 + 8)
            return i;
    }
    return -1;
}
static void pressVis(int i, bool down) {
    Obj* v = (Obj*)g_taps[i].vis.get();
    if (alive(v)) setScale(rt(v), down ? 0.95f : 1.f);
}
// canvas centre of something placed in the sheet with anchor (0,1) or (1,1)
static Vec2 sheetToCanvas(float anchorX, Vec2 pos) {
    return Vec2{(anchorX < 0.5f ? -g_S / 2 : g_S / 2) + pos.x, sheetTop() + pos.y};
}

// ---------- buttons ----------
static void onSpeedStep(void* user) {
    float d = user ? 0.05f : -0.05f;
    g_prac.speed = clampf((float)(int)((g_prac.speed + d) * 20 + 0.5f) / 20, 0.05f, 2.0f);
    paintSpeed();
    practiceSaveSettings();
}
// −/+ walk through round values, with the 抵消 value in between
static void onFlowStep(void* user) {
    static const float nice[] = {0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1, 1.2f, 1.5f, 2, 2.5f, 3, 4, 5, 6, 8, 10, 12, 15, 20};
    float v = g_prac.flow, c = flowCancel(), best = user ? kFlowMax : kFlowMin;
    for (float n : nice) {
        if (user && n > v + 0.006f && n < best) best = n;
        if (!user && n < v - 0.006f && n > best) best = n;
    }
    if (user && c > v + 0.006f && c < best) best = c;
    if (!user && c < v - 0.006f && c > best) best = c;
    g_prac.flow = best;
    paintFlow();
    practiceSaveSettings();
}
static void onLoop(void*) {
    g_prac.loop = !g_prac.loop;
    paintSwitch(g_loopTrack, g_loopKnob, g_prac.loop);
    practiceSaveSettings();
}
static void onPitch(void*) {
    g_prac.keepPitch = !g_prac.keepPitch;
    paintSwitch(g_pitchTrack, g_pitchKnob, g_prac.keepPitch);
    practiceSaveSettings();
}
static void onStart(void*) {
    practiceSaveSettings();
    practiceStart();
}
static void onCancel(void*) { practiceCancelEdit(); }

// ---------- building ----------
static Obj* pillButton(Obj* parent, Vec2 anchor, Vec2 pos, Vec2 size, const char* label, bool primary,
                       NativeCallback cb, void* user) {
    Obj* border = image(parent, "btn", SPR_ROUND, rgb(primary ? C_CLAY : C_LINE), anchor, pos, size, 14, true);
    if (!primary) image(rt(border), "fill", SPR_ROUND, rgb(C_IVORY), Vec2{0.5f, 0.5f}, Vec2{0, 0},
                        Vec2{size.x - 4, size.y - 4}, 12);
    text(rt(border), label, 23, rgb(primary ? 0xFFFFFF : C_TEXT), MiddleCenter, primary, Vec2{0.5f, 0.5f},
         Vec2{0.5f, 0.5f}, Vec2{0, 1}, size);
    addTap(border, sheetToCanvas(anchor.x, pos), size, cb, user);
    return border;
}

static void switchRow(Obj* sheet, float x, float y, const char* label, Handle& track, Handle& knob, bool on,
                      NativeCallback cb) {
    Obj* hit = image(sheet, "switch", SPR_SOLID, Color{1, 1, 1, 0}, Vec2{0, 1}, Vec2{x + 110, y}, Vec2{220, 60}, 0,
                     true);
    text(rt(hit), label, 23, rgb(C_TEXT), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{0, 0},
         Vec2{150, 40});
    Obj* tr = image(rt(hit), "track", SPR_ROUND, rgb(C_TRACK_OFF), Vec2{1, 0.5f}, Vec2{-34, 0}, Vec2{68, 38}, 19);
    Obj* kn = image(rt(tr), "knob", SPR_CIRCLE, rgb(0xFFFFFF), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{30, 30});
    shadow(kn, Color{0, 0, 0, 0.12f}, Vec2{0, -1});
    track.set(tr);
    knob.set(kn);
    paintSwitch(track, knob, on);
    addTap(tr, sheetToCanvas(0, Vec2{x + 110, y}), Vec2{220, 60}, cb, nullptr);
}

void editorOpen() {
    editorClose();
    g_tapN = 0;
    g_tapDown = -1;
    float W = canvasW();
    g_S = clampf(W - 48, 900, 1800);
    g_TW = g_S - 80;
    g_SW = g_S - 230 - 300;
    g_vs = 0;
    g_ve = g_prac.songLen;

    Obj* root = overlayCanvas("PhiMeSectionEditor", 31000);
    g_root.set(root);
    Obj* dim = image(root, "dim", SPR_SOLID, Color{0.08f, 0.07f, 0.06f, 0.42f}, Vec2{0.5f, 0.5f}, Vec2{0, 0},
                     Vec2{100, 100}, 0, true);
    stretch(rt(dim), -20);
    g_dim.set(dim);

    Obj* border = image(root, "sheetBorder", SPR_ROUND, rgb(C_LINE), Vec2{0.5f, 0}, Vec2{0, kMargin + kSheetH / 2},
                        Vec2{g_S + 4, kSheetH + 4}, 26, true);
    Obj* sheetImg = image(rt(border), "sheet", SPR_ROUND, rgb(C_IVORY), Vec2{0.5f, 0.5f}, Vec2{0, 0},
                          Vec2{g_S, kSheetH}, 24, true);
    Obj* sheet = rt(sheetImg);
    g_sheetGroup.set(addCanvasGroup(gameObject(border)));
    g_fade = g_fadeTarget = 0;
    g_shownA = g_shownB = -1;

    // Clawd peeking over the sheet
    image(sheet, "clawd", SPR_CLAWD, rgb(0xFFFFFF), Vec2{1, 1}, Vec2{-150, 34 + 2}, Vec2{120, 68});

    // header
    text(sheet, T("选段练习", "Section practice"), 28, rgb(C_TEXT), MiddleLeft, true, Vec2{0, 1}, Vec2{0, 0.5f}, Vec2{40, -46},
         Vec2{300, 44});
    Obj* ro = text(sheet, "", 21, rgb(C_MUTED), MiddleRight, false, Vec2{1, 1}, Vec2{1, 0.5f}, Vec2{-40, -46},
                   Vec2{900, 36});
    g_readout.set(ro);

    // timeline
    Obj* tl = newUI("timeline", sheet);
    place(tl, Vec2{0.5f, 1}, Vec2{0.5f, 0.5f}, Vec2{0, -140}, Vec2{g_TW, kTimelineH});
    Obj* tlGroup = addCanvasGroup(gameObject(tl));  // fades on its own, not with the sheet
    setIgnoreParentGroups(tlGroup, true);
    g_timelineGroup.set(tlGroup);
    Obj* track = image(tl, "track", SPR_ROUND, rgb(C_TRACK), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{g_TW, kTimelineH},
                       14, true);
    (void)track;
    Obj* sel = image(tl, "selection", SPR_SOLID, rgb(C_CLAY, 0.22f), Vec2{0, 0.5f}, Vec2{0, 0},
                     Vec2{10, kTimelineH});
    g_sel.set(sel);
    for (int i = 0; i < kTicks; i++) {
        g_tick[i].set(image(tl, "tick", SPR_SOLID, rgb(0xC9C6BA), Vec2{0, 0.5f}, Vec2{0, 0}, Vec2{2, 12}));
        g_tickLbl[i].set(text(tl, "", 18, rgb(C_MUTED), MiddleCenter, false, Vec2{0, 0.5f}, Vec2{0.5f, 0.5f},
                              Vec2{0, 0}, Vec2{120, 26}));
    }
    for (int h = 0; h < 2; h++) {
        Obj* hd = image(tl, h ? "B" : "A", SPR_ROUND, rgb(C_CLAY), Vec2{0, 0.5f}, Vec2{0, 0},
                        Vec2{14, kTimelineH + 16}, 7);
        image(rt(hd), "grip", SPR_ROUND, rgb(0xFFFFFF, 0.85f), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{4, 28}, 2);
        Obj* tag = image(rt(hd), "tag", SPR_ROUND, rgb(C_CLAY), Vec2{0.5f, 1}, Vec2{0, 18}, Vec2{34, 30}, 10);
        text(rt(tag), h ? "B" : "A", 19, rgb(0xFFFFFF), MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f},
             Vec2{0, 1}, Vec2{34, 30});
        (h ? g_hB : g_hA).set(hd);
    }

    // speed
    text(sheet, T("速度", "Speed"), 23, rgb(C_TEXT), MiddleLeft, false, Vec2{0, 1}, Vec2{0, 0.5f}, Vec2{40, -270},
         Vec2{100, 40});
    pillButton(sheet, Vec2{0, 1}, Vec2{180, -270}, Vec2{64, 52}, "-", false, onSpeedStep, nullptr);
    Obj* sp = newUI("speed", sheet);
    place(sp, Vec2{0, 1}, Vec2{0, 0.5f}, Vec2{230, -270}, Vec2{g_SW, 40});
    image(sp, "rail", SPR_ROUND, rgb(C_TRACK_OFF), Vec2{0, 0.5f}, Vec2{g_SW / 2, 0}, Vec2{g_SW, 8}, 4, true);
    g_speedFill.set(image(sp, "fill", SPR_ROUND, rgb(C_CLAY), Vec2{0, 0.5f}, Vec2{0, 0}, Vec2{10, 8}, 4));
    Obj* knob = image(sp, "knob", SPR_CIRCLE, rgb(0xFFFFFF), Vec2{0, 0.5f}, Vec2{0, 0}, Vec2{36, 36});
    shadow(knob, Color{0, 0, 0, 0.18f}, Vec2{0, -1.5f});
    g_speedKnob.set(knob);
    pillButton(sheet, Vec2{1, 1}, Vec2{-250, -270}, Vec2{64, 52}, "+", false, onSpeedStep, (void*)1);
    g_speedText.set(text(sheet, "", 24, rgb(C_TEXT), MiddleRight, true, Vec2{1, 1}, Vec2{1, 0.5f}, Vec2{-40, -270},
                         Vec2{150, 40}));

    // note flow: log scale with marked values, and the 抵消 mark (flow x speed = 1)
    text(sheet, T("流速", "Flow"), 23, rgb(C_TEXT), MiddleLeft, false, Vec2{0, 1}, Vec2{0, 0.5f}, Vec2{40, -370},
         Vec2{100, 40});
    pillButton(sheet, Vec2{0, 1}, Vec2{180, -370}, Vec2{64, 52}, "-", false, onFlowStep, nullptr);
    Obj* fl = newUI("flow", sheet);
    place(fl, Vec2{0, 1}, Vec2{0, 0.5f}, Vec2{230, -370}, Vec2{g_SW, 40});
    image(fl, "rail", SPR_ROUND, rgb(C_TRACK_OFF), Vec2{0, 0.5f}, Vec2{g_SW / 2, 0}, Vec2{g_SW, 8}, 4, true);
    g_flowFill.set(image(fl, "fill", SPR_ROUND, rgb(C_CLAY), Vec2{0, 0.5f}, Vec2{0, 0}, Vec2{10, 8}, 4));
    static const float marks[] = {0.5f, 1, 2, 5, 10, 20};
    static const char* const markText[] = {"0.5x", "1x", "2x", "5x", "10x", "20x"};
    for (int i = 0; i < 6; i++) {
        float x = flowF(marks[i]) * g_SW;
        image(fl, "tick", SPR_SOLID, rgb(0xC9C6BA), Vec2{0, 0.5f}, Vec2{x, -14}, Vec2{2, 10});
        text(fl, markText[i], 17, rgb(C_MUTED), MiddleCenter, false, Vec2{0, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{x, -32},
             Vec2{70, 24});
    }
    // the 抵消 mark: a dark pin through the rail with its label above (under the knob when they meet)
    Obj* mark = newUI("cancelMark", fl);
    place(mark, Vec2{0, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{4, 4});
    image(mark, "pin", SPR_ROUND, rgb(C_TEXT, 0.7f), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{5, 26}, 2);
    text(mark, T("抵消", "1:1"), 16, rgb(C_TEXT, 0.7f), MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f},
         Vec2{0, 28}, Vec2{70, 22});
    g_flowMark.set(mark);
    Obj* fknob = image(fl, "knob", SPR_CIRCLE, rgb(0xFFFFFF), Vec2{0, 0.5f}, Vec2{0, 0}, Vec2{36, 36});
    shadow(fknob, Color{0, 0, 0, 0.18f}, Vec2{0, -1.5f});
    g_flowKnob.set(fknob);
    pillButton(sheet, Vec2{1, 1}, Vec2{-250, -370}, Vec2{64, 52}, "+", false, onFlowStep, (void*)1);
    g_flowText.set(text(sheet, "", 24, rgb(C_TEXT), MiddleRight, true, Vec2{1, 1}, Vec2{1, 0.5f}, Vec2{-40, -370},
                        Vec2{150, 40}));

    // switches + actions
    switchRow(sheet, 40, -480, T("AB 循环", "A-B loop"), g_loopTrack, g_loopKnob, g_prac.loop, onLoop);
    switchRow(sheet, 300, -480, T("变速不变调", "Keep pitch"), g_pitchTrack, g_pitchKnob, g_prac.keepPitch, onPitch);
    pillButton(sheet, Vec2{1, 1}, Vec2{-150, -480}, Vec2{200, 60}, T("开始练习", "Start"), true, onStart, nullptr);
    pillButton(sheet, Vec2{1, 1}, Vec2{-340, -480}, Vec2{150, 60}, T("取消", "Cancel"), false, onCancel, nullptr);

    g_open = true;
    g_gesture = G_NONE;
    paintTimeline();
    paintSpeed();
}

void editorClose() {
    Obj* root = (Obj*)g_root.get();
    uiFadeAway(root);  // fades out, then destroyed
    g_root.clear();
    g_open = false;
    g_gesture = G_NONE;
}

// ---------- gestures ----------
static void clampView() {
    float span = clampf(g_ve - g_vs, kMinSpan, g_prac.songLen);
    if (g_vs < 0) g_vs = 0;
    if (g_vs + span > g_prac.songLen) g_vs = g_prac.songLen - span;
    if (g_vs < 0) g_vs = 0;
    g_ve = g_vs + span;
}

static void endGesture() {
    if (g_gesture == G_DRAG_A || g_gesture == G_DRAG_B) {
        setDimClear(false);
        practicePreview(g_gesture == G_DRAG_A ? g_prac.a : g_prac.b);
    }
    if (g_gesture == G_SPEED || g_gesture == G_FLOW) practiceSaveSettings();
    g_gesture = G_NONE;
}

void editorTick(float dt) {
    if (!g_open) return;
    if (!alive((Obj*)g_root.get())) {
        g_open = false;
        return;
    }
    fadeTick(dt);
    TouchInfo t[4];
    int n = touches(t, 4);
    bool changed = false, selChanged = false;

    // buttons first: a press that starts on a button belongs to that button until the finger lifts
    if (n == 1) {
        Vec2 p = toCanvas(t[0].pos);
        if (t[0].phase == 0) {
            int h = hitTap(p);
            if (h >= 0) {
                g_tapDown = h;
                pressVis(h, true);
                g_gesture = G_NONE;
                return;
            }
        } else if (g_tapDown >= 0) {
            if (t[0].phase == 3 || t[0].phase == 4) {
                int h = g_tapDown;
                g_tapDown = -1;
                pressVis(h, false);
                if (t[0].phase == 3 && hitTap(p) == h && g_taps[h].cb) g_taps[h].cb(g_taps[h].user);  // may close us
            }
            return;
        }
    } else if (n == 0 && g_tapDown >= 0) {
        pressVis(g_tapDown, false);
        g_tapDown = -1;
    }

    if (n >= 2) {
        Vec2 p0 = toCanvas(t[0].pos), p1 = toCanvas(t[1].pos);
        float dx = p1.x - p0.x, dy = p1.y - p0.y;
        float d = __builtin_sqrtf(dx * dx + dy * dy);
        float midLocal = (p0.x + p1.x) / 2 - tlLeft();
        if (g_gesture != G_PINCH) {
            if (g_gesture == G_DRAG_A || g_gesture == G_DRAG_B) setDimClear(false);
            g_gesture = G_PINCH;
            g_pinchD0 = d > 1 ? d : 1;
            g_pinchSpan0 = g_ve - g_vs;
            g_pinchAnchorX = clampf(midLocal, 0, g_TW);
            g_pinchAnchorT = timeAt(g_pinchAnchorX);
        } else if (d > 1) {
            float span = clampf(g_pinchSpan0 * g_pinchD0 / d, kMinSpan, g_prac.songLen);
            g_vs = g_pinchAnchorT - g_pinchAnchorX / g_TW * span;
            g_ve = g_vs + span;
            clampView();
            changed = true;
        }
    } else if (n == 1) {
        Vec2 p = toCanvas(t[0].pos);
        float lx = p.x - tlLeft();
        if (t[0].phase == 0) {  // finger down: decide what this touch does
            g_gesture = G_NONE;
            bool inTimeline = p.y > tlY() - kTimelineH / 2 - 20 && p.y < tlY() + kTimelineH / 2 + 40 && lx > -40 &&
                              lx < g_TW + 40;
            bool inSpeed = p.y > spY() - 34 && p.y < spY() + 34 && p.x > spLeft() - 30 && p.x < spLeft() + g_SW + 30;
            bool inFlow = p.y > flY() - 44 && p.y < flY() + 40 && p.x > spLeft() - 30 && p.x < spLeft() + g_SW + 30;
            if (inTimeline) {
                float da = lx - xAt(g_prac.a), db = lx - xAt(g_prac.b);
                da = da < 0 ? -da : da;
                db = db < 0 ? -db : db;
                if (da < 56 && da <= db) g_gesture = G_DRAG_A;
                else if (db < 56) g_gesture = G_DRAG_B;
                else {
                    g_gesture = G_PAN;
                    g_panX0 = lx;
                    g_panVs0 = g_vs;
                }
                if (g_gesture == G_DRAG_A || g_gesture == G_DRAG_B) {
                    setDimClear(true);
                    g_previewedT = -1;
                }
            } else if (inSpeed) {
                g_gesture = G_SPEED;
            } else if (inFlow) {
                g_gesture = G_FLOW;
            }
        }
        // (a finger left over from a pinch keeps G_PINCH and does nothing until it lifts)
        if (t[0].phase == 3 || t[0].phase == 4) {
            endGesture();
        } else if (g_gesture == G_DRAG_A || g_gesture == G_DRAG_B) {
            float tt = (float)(int)(clampf(timeAt(clampf(lx, 0, g_TW)), 0, g_prac.songLen) * 100 + 0.5f) / 100;
            if (g_gesture == G_DRAG_A) g_prac.a = clampf(tt, 0, g_prac.b - 0.5f);
            else g_prac.b = clampf(tt, g_prac.a + 0.5f, g_prac.songLen - 0.2f);
            // The chart follows the handle every frame it moves (the preview only rebuilds the next few
            // seconds of notes, so this stays cheap).
            float ht = g_gesture == G_DRAG_A ? g_prac.a : g_prac.b;
            if (ht != g_previewedT) {
                practicePreview(ht);
                g_previewedT = ht;
            }
            selChanged = true;
        } else if (g_gesture == G_PAN) {
            float span = g_ve - g_vs;
            g_vs = g_panVs0 - (lx - g_panX0) / g_TW * span;
            g_ve = g_vs + span;
            clampView();
            changed = true;
        } else if (g_gesture == G_SPEED) {
            float f = clampf((p.x - spLeft()) / g_SW, 0, 1);
            float sp = (float)(int)((0.05f + f * 1.95f) * 20 + 0.5f) / 20;
            if (sp != g_prac.speed) {
                g_prac.speed = clampf(sp, 0.05f, 2.0f);
                paintSpeed();
            }
        } else if (g_gesture == G_FLOW) {
            float x = clampf(p.x - spLeft(), 0, g_SW);
            float c = flowCancel(), cx = flowF(c) * g_SW;
            // snaps onto the 抵消 mark within a finger's width of it
            float v = (x - cx < 22 && cx - x < 22) ? c : flowRound(flowAt(x / g_SW));
            if (v != g_prac.flow) {
                g_prac.flow = v;
                paintFlow();
            }
        }
    } else if (g_gesture != G_NONE) {
        endGesture();
    }
    if (changed) paintTimeline();  // the view moved: ticks + selection, this frame
    else if (selChanged) paintSelection();
}
