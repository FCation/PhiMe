// 上隐条 (top hide bar): a nearly opaque black band over the top of the chart, to read dense patterns from the
// lower part of the screen only. It covers the chart, not the HUD (score, combo, judgements are drawn after it), and
// its lower edge fades out softly.
//   - The band is a world SpriteRenderer (a vertical alpha gradient) in front of the notes: the note sprites' sorting
//     layer and object layer, an order just below the HUD canvas when that canvas is sorted with the world, else very
//     high (an overlay HUD is drawn after every camera anyway).
//   - When a judge line that still has notes to judge (within the next 2.5 s, or just missed) sits under the band,
//     the band fades away, and comes back once no such line is under it (lines with no notes are only decoration).
// Height: g_cfg.maskPct (0..50 % of the screen, from the top).
#include "mod.h"
#include "practice.h"

using namespace il2cpp;
using namespace unity;

static struct {
    bool ok;
    Il2CppClass *Cam, *SR, *Canvas, *Texture2D, *Color32, *Sprite, *GO;
    const MethodInfo *camMain, *w2s, *orthoSize, *aspect, *isOrtho;
    const MethodInfo *texCtor, *setPixels, *texApply, *texWrap, *spriteCreate, *srSprite, *srColor;
    const MethodInfo *rLayer, *rGetLayer, *right, *rOrder, *goLayer, *goGetLayer, *cvMode, *cvOrder, *cvLayer, *hide, *trScale, *inverse,
        *transformPoint;
    int numLists[4], scCombo;
    int pcJc, pcNow, pcScore, jcLines, jlAbove, jlBelow, noteTime, noteJudged, scScoreText, ccSR;
    int numLists0, jcNum;
    int listItems = -1, listSize = -1;
} M;

static Handle g_pc, g_band, g_sprite;
static float g_alpha = 1, g_shown = -1;
static int g_lines = 0;
static float* g_times[64];  // per judge line: its notes' times, sorted
static int g_timesN[64];
static Obj* g_lineTr[64];  // each line's transform (a note's parent)
static const float kMinRun = 1.5f;  // a stretch of a line's notes shorter than this never clears the band
static bool g_built = false;
static float clearUntil = 0;  // the band stays clear until then (level time)
static float g_pctLive = 25;  // the band height in % (smooth while the preview handle is dragged)
static bool g_armed = false, g_preview = false;  // preview: armed from the panel, running in this level
static void previewFrame(Obj* pc);
static const float kOpacity = 0.93f, kFeather = 0.07f;  // band alpha; soft edge, as a fraction of the screen height

static int listView(Obj* list, Obj*** out) {
    if (!list) return 0;
    if (M.listItems < 0) {
        Il2CppClass* k = il2cpp_object_get_class(list);
        M.listItems = fieldOffset(k, "_items");
        M.listSize = fieldOffset(k, "_size");
        if (M.listItems < 0 || M.listSize < 0) return 0;
    }
    Il2CppArray* items = fld<Il2CppArray*>(list, M.listItems);
    int n = fld<int>(list, M.listSize);
    if (!items || n <= 0 || (uintptr_t)n > items->max_length) return 0;
    *out = (Obj**)arrayData(items);
    return n;
}

static void freeTimes() {
    for (int i = 0; i < g_lines; i++) {
        free(g_times[i]);
        g_times[i] = nullptr;
        g_timesN[i] = 0;
    }
    g_lines = 0;
}

// From a note at time t on the line with transform `line`: how long its line's notes keep coming (gaps up to the
// 1.5 s debounce). -1: line unknown.
static float runAhead(Obj* line, float t) {
    for (int l = 0; l < g_lines; l++) {
        if (!line || g_lineTr[l] != line || !g_times[l]) continue;
        const float* ts = g_times[l];
        int n = g_timesN[l], i = 0;
        while (i < n && ts[i] < t - 0.01f) i++;
        float end = t;
        for (; i < n && ts[i] - end <= 1.5f; i++)
            if (ts[i] > end) end = ts[i];
        return end - t;
    }
    return -1;
}

static int cmpf(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : x > y ? 1 : 0;
}
extern "C" void qsort(void* base, size_t n, size_t size, int (*cmp)(const void*, const void*));

// each line's note times, once the chart is there
static bool collectTimes(Obj* jc) {
    Obj** lines = nullptr;
    int nl = listView(fld<Obj*>(jc, M.jcLines), &lines);
    if (nl <= 0) return false;
    freeTimes();
    int total = 0;
    for (int l = 0; l < nl && l < 64; l++) {
        g_times[l] = nullptr;
        g_timesN[l] = 0;
        g_lineTr[l] = nullptr;
        if (!alive(lines[l])) continue;
        g_lineTr[l] = goTransform(gameObject(lines[l]));
        Obj **a = nullptr, **b = nullptr;
        int na = listView(fld<Obj*>(lines[l], M.jlAbove), &a), nb = listView(fld<Obj*>(lines[l], M.jlBelow), &b);
        if (na + nb <= 0) continue;
        float* t = (float*)malloc(sizeof(float) * (na + nb));
        if (!t) continue;
        int n = 0;
        for (int i = 0; i < na; i++)
            if (a[i]) t[n++] = fld<float>(a[i], M.noteTime);
        for (int i = 0; i < nb; i++)
            if (b[i]) t[n++] = fld<float>(b[i], M.noteTime);
        qsort(t, n, sizeof(float), cmpf);
        g_times[l] = t;
        g_timesN[l] = n;
        total += n;
    }
    g_lines = nl < 64 ? nl : 64;
    if (total == 0) {  // the chart is not loaded yet (the lines are there, empty): again next frame (0.31.0 kept this)
        freeTimes();
        return false;
    }
    LOG("mask: %d judge lines, %d notes", g_lines, total);
    return true;
}

// the ChartNote field of a note component (ClickControl / DragControl / FlickControl / HoldControl.noteInfor)
static Il2CppClass* g_infoK[4];
static int g_infoOff[4], g_infoN = 0;
static int infoOffset(Obj* c) {
    Il2CppClass* k = il2cpp_object_get_class(c);
    for (int i = 0; i < g_infoN; i++)
        if (g_infoK[i] == k) return g_infoOff[i];
    int off = fieldOffset(k, "noteInfor");
    if (g_infoN < 4) g_infoK[g_infoN] = k, g_infoOff[g_infoN++] = off;
    return off;
}

// the band: a vertical alpha ramp, opaque on top, soft at the bottom (128 rows, the lower ones the feather)
static Obj* bandSprite(float featherShare) {
    Obj* s = (Obj*)g_sprite.get();
    if (alive(s)) return s;
    const int W = 4, H = 128;
    Obj* tex = il2cpp_object_new(M.Texture2D);
    callm<void>(M.texCtor, tex, W, H);
    if (M.texWrap) callm<void>(M.texWrap, tex, 1);
    Il2CppArray* px = newArray(M.Color32, (uintptr_t)(W * H));
    uint8_t* p = (uint8_t*)arrayData(px);
    int soft = (int)(H * featherShare);
    for (int y = 0; y < H; y++) {  // row 0 = bottom
        float a = y >= soft ? 1.f : (float)y / (soft > 0 ? soft : 1);
        a = a * a * (3 - 2 * a);  // smoothstep
        for (int x = 0; x < W; x++) {
            uint8_t* c = p + (y * W + x) * 4;
            c[0] = c[1] = c[2] = 0;
            c[3] = (uint8_t)(a * 255 + 0.5f);
        }
    }
    callm<void>(M.setPixels, tex, px);
    callm<void>(M.texApply, tex);
    if (M.hide) callm<void>(M.hide, tex, 32);
    s = callm<Obj*>(M.spriteCreate, tex, Rect{0, 0, (float)W, (float)H}, Vec2{0.5f, 1.f}, 100.f, 0u, 0, Vec4{0, 0, 0, 0});
    if (M.hide && s) callm<void>(M.hide, s, 32);
    g_sprite.set(s);
    return s;
}

// world corners of the screen at the chart's depth (orthographic camera)
static bool screenWorld(Obj* cam, float* cx, float* cy, float* w, float* h) {
    if (!alive(cam) || !callm<bool>(M.isOrtho, cam)) return false;
    float size = callm<float>(M.orthoSize, cam), asp = callm<float>(M.aspect, cam);
    Vec3 p = worldPos(goTransform(gameObject(cam)));
    *cx = p.x;
    *cy = p.y;
    *h = size * 2;
    *w = *h * asp;
    return *h > 0;
}

static void build(Obj* pc) {
    g_built = true;
    Obj* cam = callm<Obj*>(M.camMain);
    float cx, cy, w, h;
    if (!screenWorld(cam, &cx, &cy, &w, &h)) {
        LOG("mask: no orthographic main camera, band off");
        return;
    }
    // a note's sorting layer and object layer (the camera draws those), and the HUD canvas' place
    Obj* jc = fld<Obj*>(pc, M.pcJc);
    Obj* num = alive(jc) && M.jcNum >= 0 ? fld<Obj*>(jc, M.jcNum) : nullptr;
    Obj** notes = nullptr;
    int nn = alive(num) ? listView(fld<Obj*>(num, M.numLists0), &notes) : 0;
    int layer = 0, objLayer = 0, order = 32767;  // above every note
    for (int i = 0; i < nn; i++) {
        Obj* sr = alive(notes[i]) && M.ccSR >= 0 ? fld<Obj*>(notes[i], M.ccSR) : nullptr;
        if (!alive(sr)) continue;
        layer = M.rGetLayer ? callm<int>(M.rGetLayer, sr) : 0;  // (0.32.4 called the setter here)
        if (M.goGetLayer) objLayer = callm<int>(M.goGetLayer, gameObject(sr));
        break;
    }
    Obj* sc = fld<Obj*>(pc, M.pcScore);
    Obj* st = alive(sc) && M.scScoreText >= 0 ? fld<Obj*>(sc, M.scScoreText) : nullptr;
    Obj* ct = alive(st) ? canvasTransform(rt(st)) : nullptr;
    Obj* cv = ct ? getComponent(gameObject(ct), M.Canvas) : nullptr;
    int mode = cv ? callm<int>(M.cvMode, cv) : -1;
    if (cv && mode != 0) {  // a HUD sorted with the world: just under it
        int cl = callm<int>(M.cvLayer, cv), co = callm<int>(M.cvOrder, cv);
        layer = cl;
        order = co - 1;
    }
    Obj* rtb = newUI("PhiMeHideBar", nullptr);
    Obj* go = gameObject(rtb);
    if (M.goLayer) callm<void>(M.goLayer, go, objLayer);
    Obj* sr = addComponent(go, M.SR);
    // the soft edge's share of the sprite: kFeather of the screen below a band of maskPct
    float pct = g_pctLive / 100.f;
    g_sprite.clear();
    callm<void>(M.srSprite, sr, bandSprite(kFeather / (pct + kFeather)));
    callm<void>(M.rLayer, sr, layer);
    callm<void>(M.rOrder, sr, order);
    callm<void>(M.srColor, sr, Color{0, 0, 0, kOpacity});
    g_band.set(sr);
    g_alpha = 1;
    g_shown = -1;
    LOG("mask: band %d%% of the screen, sorting layer %d order %d, object layer %d (HUD canvas mode %d)", g_cfg.maskPct,
        layer, order, objLayer, mode);
}

// band placement and fade, every frame
static void maskFrame(void*) {
    Obj* pc = (Obj*)g_pc.get();
    if (!pc) return;
    if (!alive(pc)) {
        g_pc.clear();
        g_band.clear();
        freeTimes();
        return;
    }
    if (!g_built) build(pc);
    if (g_preview) previewFrame(pc);
    Obj* sr = (Obj*)g_band.get();
    if (!alive(sr)) return;
    Obj* jc = fld<Obj*>(pc, M.pcJc);
    if (!g_lines && alive(jc)) collectTimes(jc);
    Obj* cam = callm<Obj*>(M.camMain);
    float cx, cy, w, h;
    if (!screenWorld(cam, &cx, &cy, &w, &h)) return;
    float pct = g_pctLive / 100.f;
    // the band: from the top of the screen down to pct, its soft edge hanging below (the sprite is 1.28 units tall:
    // 128 px at 100 per unit, its lower rows the soft edge)
    float bandH = pct * h, total = bandH + kFeather * h;
    Obj* tr = goTransform(gameObject(sr));
    setWorldPos(tr, Vec3{cx, cy + h / 2, 0});
    if (M.trScale) callm<void>(M.trScale, tr, Vec3{(w * 1.1f) / 0.04f, total / 1.28f, 1});
    // A note about to be judged (within 0.6 s, or a hold still being held) whose hit point sits under the band: fade
    // out. The hit point is the note projected onto its judge line (its parent): x along the line, y = 0 in the line's
    // space; so a slanted or vertical line counts only where its notes land (0.31.1 used whole lines and 2.5 s:
    // lines that had passed through the band faded it for nothing).
    bool covered = false;
    float now = fld<float>(pc, M.pcNow), top = cy + h / 2, edge = top - bandH;
    Obj* num = alive(jc) && M.jcNum >= 0 ? fld<Obj*>(jc, M.jcNum) : nullptr;
    for (int k = 0; k < 4 && alive(num) && !covered; k++) {
        Obj** e = nullptr;
        int n = M.numLists[k] >= 0 ? listView(fld<Obj*>(num, M.numLists[k]), &e) : 0;
        for (int i = 0; i < n && !covered; i++) {
            Obj* c = e[i];
            if (!c) continue;
            int off = infoOffset(c);
            Obj* note = off >= 0 ? fld<Obj*>(c, off) : nullptr;
            if (!note) continue;
            float dt = fld<float>(note, M.noteTime) - now;
            // Strict (the user: rather covered too much than cleared for nothing). No judge line geometry any more
            // (0.32.4/0.32.5 got hit points like y = 95 and cleared the band for notes far below it): a note due within
            // 0.2 s is next to its hit point, so it alone decides: in the band right now -> clear.
            if (dt < -0.05f || dt > 0.2f) continue;
            if (!alive(c)) continue;
            Obj* ntr = goTransform(gameObject(c));
            Vec3 at = worldPos(ntr);
            bool noteIn = at.y > edge + h * 0.02f && at.y < top + h * 0.05f && at.x > cx - w / 2 && at.x < cx + w / 2;
            if (!noteIn) continue;
            // a short show (a line passing through the band with a note or two) does not clear it: the note's line must
            // keep having notes for 1.5 s from here (line unknown: stay covered)
            float tn = fld<float>(note, M.noteTime), run = runAhead(parentOf(ntr), tn);
            static int logged = 0;
            if (run < kMinRun) {
                static float lastKept = -1;
                if (logged < 16 && tn != lastKept) {
                    lastKept = tn;
                    logged++;
                    LOG("mask: kept at %.2f s: a note in the band, its line's notes last %.2f s", now, run);
                }
                continue;
            }
            covered = true;
            if (logged < 16) {
                logged++;
                LOG("mask: cleared at %.2f s by a note at (%.2f, %.2f) due in %.2f s, its line's notes last %.2f s; band "
                    "y %.2f..%.2f", now, at.x, at.y, dt, run, edge, top);
            }
        }
    }    // once cleared, stay clear 1.5 s after the last covered frame: no going black between notes
    float tnow = timeSinceLevelLoad();
    if (covered) clearUntil = tnow + 1.5f;
    bool clear = covered || tnow < clearUntil;
    float target = pct <= 0 ? 0.f : (clear && !g_preview) ? 0.f : 1.f;  // the preview always shows the band
    // clearing is quick (the note is only 0.2 s away), coming back is a slow fade
    float step = unscaledDt() / (target < g_alpha ? 0.08f : 0.25f);
    g_alpha = g_alpha < target ? (g_alpha + step > target ? target : g_alpha + step)
                               : (g_alpha - step < target ? target : g_alpha - step);
    if (g_alpha != g_shown) {
        callm<void>(M.srColor, sr, Color{0, 0, 0, kOpacity * g_alpha});
        g_shown = g_alpha;
    }
}

// ======================= the adjusting preview =======================
// Armed from the panel (预览调整); the next level runs as a practice loop from 1:22 to the end with autoplay (practice.cpp
// / autoplay.cpp ask maskPreviewPending / maskPreviewActive), the game's HUD hidden, a handle on the band's lower edge
// (drag: smooth; release: whole %) and 确认 at the bottom: saves the height and opens the game's pause menu to leave.
static Handle g_ui, g_handle, g_handleText, g_hudGroup;
static bool g_dragging = false, g_done = false;
static int g_touchId = -1;
static const Vec2 kHandle{220, 52}, kOk{220, 64};

bool maskPreviewPending() { return g_armed; }
bool maskPreviewLevel() { return g_preview && alive((Obj*)g_pc.get()); }  // this level is (or was) the preview
bool maskPreviewActive() { return g_preview && !g_done && alive((Obj*)g_pc.get()); }
void maskPreviewArm() {
    g_armed = true;
    LOG("mask: preview armed for the next level");
}

static void paintHandle() {
    Obj* t = (Obj*)g_handleText.get();
    if (!alive(t)) return;
    char b[48];
    snprintf(b, sizeof b, T("拖动调整  %d%%", "Drag  %d%%"), (int)(g_pctLive + 0.5f));
    setText(t, b);
}
static void onOk(void*) {
    if (g_done) return;
    g_done = true;
    g_cfg.maskPct = (int)(g_pctLive + 0.5f);
    g_cfg.maskOn = g_cfg.maskPct > 0;
    configSave();
    Obj* ui = (Obj*)g_ui.get();
    if (alive(ui)) setActive(gameObject(ui), false);
    Obj* g = (Obj*)g_hudGroup.get();
    if (alive(g)) setGroupAlpha(g, 1);
    LOG("mask: preview saved %d%%", g_cfg.maskPct);
    // back to where the preview was started from (the pause menu's own 返回); else that menu, to leave by hand
    if (!practiceLeaveLevel()) practicePauseToLeave();
}
static void buildPreviewUi(Obj* pc) {
    Obj* root = overlayCanvas("PhiMeMaskPreview", 30600);
    g_ui.set(root);
    Obj* hb = image(root, "handle", SPR_ROUND, rgb(0xE8E6DC, 0.95f), Vec2{0.5f, 0.5f}, Vec2{0, 0}, kHandle, 26);
    Obj* hl = text(rt(hb), "", 22, rgb(0xD97757), MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, 1}, kHandle);
    g_handle.set(hb);
    g_handleText.set(hl);
    paintHandle();
    Obj* ok = image(root, "ok", SPR_ROUND, rgb(0xD97757), Vec2{0, 0}, Vec2{48 + kOk.x / 2, 40 + kOk.y / 2}, kOk, 20, true);
    text(rt(ok), T("确认", "Done"), 26, rgb(0xFFFFFF), MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, 1}, kOk);
    onClick(gameObject(ok), ok, onOk, nullptr);
    // the game's HUD (score, combo, pause button, song name...): the canvas the combo text is on, faded out
    Obj* sc = M.pcScore >= 0 ? fld<Obj*>(pc, M.pcScore) : nullptr;
    Obj* combo = alive(sc) && M.scCombo >= 0 ? fld<Obj*>(sc, M.scCombo) : nullptr;
    Obj* ct = alive(combo) ? canvasTransform(rt(combo)) : nullptr;
    if (ct) {
        Obj* g = addCanvasGroup(gameObject(ct));
        setGroupAlpha(g, 0);
        g_hudGroup.set(g);
    }
    LOG("mask: preview page up (HUD %s)", ct ? "hidden" : "NOT found");
}
static void previewFrame(Obj* pc) {
    if (g_done) return;
    if (!g_ui.h) buildPreviewUi(pc);
    Obj* g = (Obj*)g_hudGroup.get();
    if (alive(g)) setGroupAlpha(g, 0);  // the game may fade its HUD in: keep it out
    float H = canvasH();
    Obj* hb = (Obj*)g_handle.get();
    float edgeY = H / 2 - g_pctLive / 100.f * H;  // the band's lower edge (canvas units, centre origin)
    if (alive(hb)) setPos(rt(hb), Vec2{0, edgeY - kHandle.y / 2 - 6});
    TouchInfo tt[4];
    int n = touches(tt, 4);
    // 确认, bottom left: read from the touches too (a level has no EventSystem, so its onClick never fires)
    Vec2 ss = screenSize();
    float W = ss.y > 0 ? H * ss.x / ss.y : H * 16 / 9;
    Vec2 okc{-W / 2 + 48 + kOk.x / 2, -H / 2 + 40 + kOk.y / 2};
    static bool okDown = false;
    for (int i = 0; i < n; i++) {
        Vec2 p = screenToCanvas(tt[i].pos);
        bool onOkBtn = __builtin_fabsf(p.x - okc.x) < kOk.x / 2 + 16 && __builtin_fabsf(p.y - okc.y) < kOk.y / 2 + 16;
        if (tt[i].phase == 0 && onOkBtn) okDown = true;
        else if ((tt[i].phase == 3 || tt[i].phase == 4) && okDown && !g_dragging) {
            okDown = false;
            if (onOkBtn && tt[i].phase == 3) {
                onOk(nullptr);
                return;
            }
        }
    }
    for (int i = 0; i < n; i++) {
        Vec2 p = screenToCanvas(tt[i].pos);
        if (tt[i].phase == 0 && !g_dragging && __builtin_fabsf(p.y - (edgeY - kHandle.y / 2)) < 70 &&
            __builtin_fabsf(p.x) < kHandle.x) {
            g_dragging = true;
            g_touchId = tt[i].id;
        } else if (g_dragging && tt[i].id == g_touchId) {
            float pct = (H / 2 - (p.y + kHandle.y / 2 + 6)) / H * 100.f;
            g_pctLive = pct < 0 ? 0 : pct > 50 ? 50 : pct;
            if (tt[i].phase == 3 || tt[i].phase == 4) {  // released: snap to a whole %
                g_dragging = false;
                g_pctLive = (float)(int)(g_pctLive + 0.5f);
            }
            paintHandle();
        }
    }
}
// Song select -> Rrhar'il AT, started right away (the play button's own start method). Every step is checked and
// logged; false = do it by hand (the panel says how)
bool maskPreviewStart() {
    Il2CppClass* SS = findClass("", "SongSelector");
    Il2CppClass* Item = findClass("", "SongSelectorItem");
    Obj* ss = SS ? findObjectOfType(SS) : nullptr;
    if (!alive(ss) || !Item) {
        LOG("mask: preview start: no song select");
        return false;
    }
    const MethodInfo* items = findMethod(SS, "get_SongItems", 0);
    int fSong = fieldOffset(SS, "nowSong"), fLevel = fieldOffset(SS, "nowLevel"), fValue = fieldOffset(SS, "nowSongSelectorValue");
    int fInfo = fieldOffset(Item, "songInfo");
    Obj* list = items ? callm<Obj*>(items, ss) : nullptr;
    Obj** e = nullptr;
    int n = list ? listView(list, &e) : 0;
    int idx = -1;
    for (int i = 0; i < n && idx < 0 && fInfo >= 0; i++) {
        Obj* info = e[i] ? fld<Obj*>(e[i], fInfo) : nullptr;
        if (!info) continue;
        static int fId = -2;
        if (fId == -2) fId = fieldOffset(il2cpp_object_get_class(info), "songsId");
        char id[96] = "";
        if (fId >= 0) utf8(fld<Il2CppString*>(info, fId), id, sizeof id);
        if (!memcmp(id, "Rrharil.", 8)) idx = i;
    }
    if (idx < 0 || fSong < 0 || fLevel < 0) {
        LOG("mask: preview start: Rrhar'il not in this song list (%d songs)", n);
        return false;
    }
    fld<int>(ss, fSong) = idx;
    fld<int>(ss, fLevel) = 3;  // AT
    if (fValue >= 0 && il2cpp_type_get_type(il2cpp_field_get_type(il2cpp_class_get_field_from_name(SS, "nowSongSelectorValue"))) == IL2CPP_TYPE_R4)
        fld<float>(ss, fValue) = (float)idx;  // the list's scroll position follows it
    const MethodInfo* setText = findMethod(SS, "SetText", 0);
    if (setText) invokeSafe(setText, ss);
    const MethodInfo* m = findMethod(SS, "NormalStart", 0);
    const char* how = "NormalStart()";
    bool ok = false;
    if (m) ok = invokeSafe(m, ss);
    if (!ok && (m = findMethod(SS, "GameStart", 0))) {
        how = "GameStart()";
        ok = invokeSafe(m, ss);
    }
    if (!ok && (m = findMethod(SS, "GameStart", 1, IL2CPP_TYPE_I4))) {
        how = "GameStart(index)";
        int a = idx;
        void* args[] = {&a};
        ok = invokeSafe(m, ss, args);
    }
    LOG("mask: preview start: Rrhar'il is song %d, AT selected, %s %s", idx, how, ok ? "called" : "FAILED");
    return ok;
}
void maskLevelStart(Obj* pc) {
    g_pc.clear();
    g_band.clear();
    g_built = false;
    clearUntil = 0;
    freeTimes();
    g_ui.clear();
    g_handle.clear();
    g_handleText.clear();
    g_hudGroup.clear();
    g_dragging = false;
    g_done = false;
    g_preview = g_armed;  // armed from the panel: this level is the adjusting preview
    g_armed = false;
    g_pctLive = (float)g_cfg.maskPct;
    if (!M.ok || (!g_preview && (!g_cfg.maskOn || g_cfg.maskPct <= 0))) return;
    g_pc.set(pc);
    static bool registered = false;
    if (!registered) {
        registered = true;
        onEveryFrame(maskFrame);
    }
}

// ProgressControl.OnApplicationPause: the game pauses the level when the app goes to the background; not during the
// adjusting preview (it just keeps looping)
typedef void (*AppPauseFn)(Obj*, bool, const MethodInfo*);
static AppPauseFn orig_appPause;
static void hk_appPause(Obj* self, bool paused, const MethodInfo* m) {
    if (maskPreviewActive()) {
        LOG("mask: preview: app %s, no pause menu", paused ? "in the background" : "back");
        return;
    }
    orig_appPause(self, paused, m);
}

bool maskInstall() {
    M.Cam = findClass("UnityEngine", "Camera");
    M.SR = findClass("UnityEngine", "SpriteRenderer");
    M.Canvas = findClass("UnityEngine", "Canvas");
    M.Texture2D = findClass("UnityEngine", "Texture2D");
    M.Color32 = findClass("UnityEngine", "Color32");
    M.Sprite = findClass("UnityEngine", "Sprite");
    M.GO = findClass("UnityEngine", "GameObject");
    Il2CppClass* Renderer = findClass("UnityEngine", "Renderer");
    Il2CppClass* Tex = findClass("UnityEngine", "Texture");
    Il2CppClass* Tr = findClass("UnityEngine", "Transform");
    Il2CppClass* UObj = findClass("UnityEngine", "Object");
    Il2CppClass* PC = findClass("", "ProgressControl");
    Il2CppClass* JC = findClass("", "JudgeControl");
    Il2CppClass* JL = findClass("", "JudgeLineControl");
    Il2CppClass* Note = findClass("", "ChartNote");
    Il2CppClass* SC = findClass("", "ScoreControl");
    Il2CppClass* NUM = findClass("", "NoteUpdateManager");
    Il2CppClass* CC = findClass("", "ClickControl");
    if (!M.Cam || !M.SR || !M.Canvas || !M.Texture2D || !M.Color32 || !M.Sprite || !Renderer || !Tr || !PC || !JC || !JL ||
        !Note)
        return false;
    M.camMain = findMethod(M.Cam, "get_main", 0);
    M.orthoSize = findMethod(M.Cam, "get_orthographicSize", 0);
    M.aspect = findMethod(M.Cam, "get_aspect", 0);
    M.isOrtho = findMethod(M.Cam, "get_orthographic", 0);
    M.texCtor = findMethod(M.Texture2D, ".ctor", 2, IL2CPP_TYPE_I4);
    M.setPixels = findMethod(M.Texture2D, "SetPixels32", 1);
    M.texApply = findMethod(M.Texture2D, "Apply", 0);
    M.texWrap = Tex ? findMethod(Tex, "set_wrapMode", 1) : nullptr;
    M.spriteCreate = findMethod(M.Sprite, "Create", 7);
    M.srSprite = findMethod(M.SR, "set_sprite", 1);
    M.srColor = findMethod(M.SR, "set_color", 1);
    M.rLayer = findMethod(Renderer, "set_sortingLayerID", 1);
    M.rGetLayer = findMethod(Renderer, "get_sortingLayerID", 0);
    M.right = findMethod(Tr, "get_right", 0);
    M.rOrder = findMethod(Renderer, "set_sortingOrder", 1);
    M.goLayer = M.GO ? findMethod(M.GO, "set_layer", 1) : nullptr;
    M.goGetLayer = M.GO ? findMethod(M.GO, "get_layer", 0) : nullptr;
    M.cvMode = findMethod(M.Canvas, "get_renderMode", 0);
    M.cvOrder = findMethod(M.Canvas, "get_sortingOrder", 0);
    M.cvLayer = findMethod(M.Canvas, "get_sortingLayerID", 0);
    M.hide = UObj ? findMethod(UObj, "set_hideFlags", 1) : nullptr;
    M.trScale = findMethod(Tr, "set_localScale", 1);
    static const char* const v3[1] = {"Vector3"};
    M.inverse = findMethodSig(Tr, "InverseTransformPoint", 1, v3);
    M.transformPoint = findMethodSig(Tr, "TransformPoint", 1, v3);
    const char* lists[4] = {"clickControls", "dragControls", "holdControls", "flickControls"};
    for (int i = 0; i < 4; i++) M.numLists[i] = NUM ? fieldOffset(NUM, lists[i]) : -1;
    if (const MethodInfo* ap = findMethod(PC, "OnApplicationPause", 1))
        orig_appPause = (AppPauseFn)hookMethod(ap, (void*)hk_appPause);
    M.pcJc = fieldOffset(PC, "judgeControl");
    M.pcNow = fieldOffset(PC, "nowTime");
    M.pcScore = fieldOffset(PC, "scoreControl");
    M.jcLines = fieldOffset(JC, "judgeLineControls");
    M.jcNum = fieldOffset(JC, "noteUpdateManager");
    M.jlAbove = fieldOffset(JL, "notesAbove");
    M.jlBelow = fieldOffset(JL, "notesBelow");
    M.noteTime = fieldOffset(Note, "realTime");
    M.scScoreText = -1;  // (no scoreText field in 4.0.x)
    M.scCombo = SC ? fieldOffset(SC, "comboText") : -1;
    M.numLists0 = NUM ? fieldOffset(NUM, "clickControls") : -1;
    M.ccSR = CC ? fieldOffset(CC, "_spriteRenderer") : -1;
    const void* must[] = {M.camMain, M.orthoSize, M.aspect, M.isOrtho, M.texCtor, M.setPixels, M.texApply,
                          M.spriteCreate, M.srSprite, M.srColor, M.rLayer, M.rOrder, M.cvMode, M.cvOrder, M.cvLayer,
                          M.trScale, M.right};
    M.ok = M.pcJc >= 0 && M.pcNow >= 0 && M.jcLines >= 0 && M.noteTime >= 0;
    for (const void* p : must) M.ok = M.ok && p;
    LOG("mask: %s", M.ok ? "ready" : "off (methods missing)");
    return M.ok;
}
