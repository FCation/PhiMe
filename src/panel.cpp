// "PhiMe 小助手" entry inside the game's settings list + the mod's own settings panel.
// Minimal style after the Claude desktop app: ivory card, hairline dividers, Claude orange accents,
// and Clawd standing on the card's top edge.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

typedef void (*VoidFn)(Obj*, const MethodInfo*);
static VoidFn orig_Setting_Start, orig_Setting_Update;

static struct {
    int settingItemsManager;
    Il2CppClass *MLTS, *UnityAction;
    const MethodInfo *addButton, *refreshItems, *mltsCtor, *mltsSet;
    bool listenersIsArray;
} P;

// palette (Claude desktop light theme)
static const int C_CLAY = 0xD97757,   // accent
    C_IVORY = 0xFAF9F5,                // card
    C_LINE = 0xE8E6DC,                 // borders / dividers
    C_TEXT = 0x141413,                 // primary text
    C_MUTED = 0x87867F,                // secondary text
    C_BODY = 0x3D3D3A,                 // paragraphs (关于)
    C_TRACK_OFF = 0xE3E1D7;

// layout (canvas units, 1080 tall)
static const float kCardW = 820, kPad = 44;
static const float kRowStep = 124;  // one toggle row + its divider
static const float kStepRow = 84;  // the replay-length stepper under its switch
// the "游玩" page is the tallest: 3 switches + the stepper ("显示" and "其他" fit in the same height)
static const int kRows = 3;
static const float kTabsH = 78;  // tab bar under the header
static const float kCardH = 460 + kTabsH - 40 + (kRows - 1) * kRowStep + kStepRow;
static const float kTop = kCardH / 2;
// Phones (unity::isPhone, canvas drawn 1.3x bigger): too short for the tab bar + footer, so the tabs go into a
// sidebar on the left with 完成 under them, and every page sits on the right, unchanged (page units are the same).
// Taller pages (皮肤 with many packs, 关于) scroll.
static const float kSideW = 210, kPhoneCardH = 732;
static bool g_phone = false;
static float g_cardW = kCardW, g_cardH = kCardH, g_top = kTop;
static Vec2 clawdPos() { return Vec2{g_cardW / 2 - 120, g_top + 2 + 34}; }  // feet on the top border (sprite 68 tall)

struct ToggleRow {
    bool* value;
    Handle track, knob;
    float knobX;
};
static const int kMaxRows = 8;
static ToggleRow g_rows[kMaxRows];
static int g_rowCount = 0;
static Handle g_root, g_card, g_dim, g_clawd, g_page, g_sub;
static float g_openT = -1;  // opening animation progress, -1 = idle
static float g_clawdT = 0;
static int g_tab = 0;  // 0 游玩, 1 显示, 2 其他, 3 皮肤, 4 账号, 5 关于 (kept while the game runs)
static void buildPage();
static void pageBody(Obj* page, float& y);
static void paintSub();

static float target(const ToggleRow& r) { return *r.value ? 19.f : -19.f; }

static void paintRow(ToggleRow& r, bool snap) {
    Obj* track = (Obj*)r.track.get();
    Obj* knob = (Obj*)r.knob.get();
    if (!alive(track) || !alive(knob)) return;
    setColor(track, rgb(*r.value ? C_CLAY : C_TRACK_OFF));
    if (snap) r.knobX = target(r);
    setPos(rt(knob), Vec2{r.knobX, 0});
}

static void onRowClick(void* user) {
    ToggleRow* r = (ToggleRow*)user;
    *r->value = !*r->value;
    // a skin has no Critical Perfect: the two switches turn each other off
    bool* other = nullptr;
    if (r->value == &g_cfg.skin && g_cfg.skin) other = &g_cfg.critical;
    if (r->value == &g_cfg.critical && g_cfg.critical) other = &g_cfg.skin;
    if (other && *other) {
        *other = false;
        for (int i = 0; i < g_rowCount; i++)
            if (g_rows[i].value == other) paintRow(g_rows[i], false);
    }
    configSave();
    paintRow(*r, false);
    paintSub();
    LOG("config: autoplay=%d practice=%d resumePreview=%d acc=%d/%d swap=%d judge=%d critical=%d fastLoad=%d",
        g_cfg.autoplay, g_cfg.practice, g_cfg.resumePreview, g_cfg.accShow, g_cfg.accMode, g_cfg.accSwap,
        g_cfg.judgeShow, g_cfg.critical, g_cfg.fastLoad);
}

// ---- segmented control (tab bar, accuracy mode) ----
struct Seg {
    int* value;
    int n;
    void (*changed)();
    Handle pill[6], label[6];
};
struct SegOpt {
    Seg* seg;
    int i;
};
static Seg g_segs[2];  // 0 = tabs, 1 = accuracy mode
static SegOpt g_segOpts[2][6];
static const int C_SEG_BG = 0xF0EEE6;

static void paintSeg(Seg& s) {
    for (int i = 0; i < s.n; i++) {
        Obj* p = (Obj*)s.pill[i].get();
        Obj* l = (Obj*)s.label[i].get();
        bool on = *s.value == i;
        if (alive(p)) setColor(p, on ? rgb(0xFFFFFF) : rgb(0xFFFFFF, 0));
        if (alive(l)) setColor(l, rgb(on ? C_TEXT : C_MUTED));
    }
}

static void onSeg(void* user) {
    SegOpt* o = (SegOpt*)user;
    if (*o->seg->value == o->i) return;
    *o->seg->value = o->i;
    paintSeg(*o->seg);
    if (o->seg->changed) o->seg->changed();
}

static void clearSeg(Seg& s) {
    for (int i = 0; i < 6; i++) {
        s.pill[i].clear();
        s.label[i].clear();
    }
}

// n options of w x h, the control centred at pos (relative to anchor on the parent); vertical = stacked top to bottom
static void segmented(int slot, Obj* parent, Vec2 anchor, Vec2 pos, float w, float h, const char* const* labels, int n,
                      int* value, void (*changed)(), bool vertical = false) {
    Seg& s = g_segs[slot];
    clearSeg(s);
    s.value = value;
    s.n = n;
    s.changed = changed;
    float total = n * (vertical ? h : w) + 8;
    Obj* bg = image(parent, "seg", SPR_ROUND, rgb(C_SEG_BG), anchor, pos,
                    vertical ? Vec2{w + 8, total} : Vec2{total, h + 8}, 16);
    for (int i = 0; i < n; i++) {
        Vec2 p = vertical ? Vec2{0, total / 2 - 4 - h * (i + 0.5f)} : Vec2{-total / 2 + 4 + w * (i + 0.5f), 0};
        Obj* pill = image(rt(bg), "opt", SPR_ROUND, rgb(0xFFFFFF, 0), Vec2{0.5f, 0.5f}, p, Vec2{w, h}, 12, true);
        Obj* l = text(rt(pill), labels[i], 22, rgb(C_MUTED), MiddleCenter, false, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f},
                      Vec2{0, 1}, Vec2{w, h});
        s.pill[i].set(pill);
        s.label[i].set(l);
        g_segOpts[slot][i] = SegOpt{&s, i};
        onClick(gameObject(pill), pill, onSeg, &g_segOpts[slot][i]);
    }
    paintSeg(s);
}

static void onTab() { buildPage(); }
static void openPanel(void*);
static void onLang() {
    configSave();  // also switches the font (English: serif)
    LOG("config: language %s", g_cfg.lang ? "English" : "中文");
    songButtonsRelabel();  // song select underneath, if it is up
    detailsRelabel();
    openPanel(nullptr);  // rebuilt in the new language, same tab
}
static void onAccMode() {
    configSave();
    LOG("config: accuracy mode %d", g_cfg.accMode);
}

// the accuracy sub-options fade out while the accuracy line is off
static void paintSub() {
    Obj* cg = (Obj*)g_sub.get();
    if (alive(cg)) setGroupAlpha(cg, g_cfg.accShow ? 1.f : 0.35f);
}

// ---- replay length stepper: 0 - 3 s in 0.1 s steps ----
static Handle g_durText;
static Handle g_volText[3];  // the hit sound volume values (skin page)
static Handle g_maskText;     // the hide bar height value

static void paintDuration() {
    Obj* t = (Obj*)g_durText.get();
    if (!alive(t)) return;
    char b[32];
    int v = g_cfg.resumePreviewTenths;
    snprintf(b, sizeof(b), T("%d.%d 秒", "%d.%d s"), v / 10, v % 10);
    setText(t, b);
}

static void onDuration(void* user) {
    int v = g_cfg.resumePreviewTenths + (int)(intptr_t)user;
    if (v < 0) v = 0;
    if (v > 30) v = 30;
    g_cfg.resumePreviewTenths = v;
    configSave();
    paintDuration();
    LOG("config: resumePreview %.1fs", v / 10.f);
}

static void stepButton(Obj* card, float x, float y, const char* glyph, int delta) {
    Obj* b = image(card, "step", SPR_ROUND, rgb(C_LINE), Vec2{1, 0.5f}, Vec2{x, y}, Vec2{60, 46}, 14, true);
    Obj* f = image(rt(b), "fill", SPR_ROUND, rgb(C_IVORY), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{56, 42}, 12);
    text(rt(f), glyph, 28, rgb(C_TEXT), MiddleCenter, false, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, 1},
         Vec2{56, 42});
    onClick(gameObject(b), b, onDuration, (void*)(intptr_t)delta);
}

static void durationRow(Obj* card, float y) {
    text(card, T("准备时间", "Lead-in"), 23, rgb(C_TEXT), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{kPad, y},
         Vec2{300, 36});
    text(card, T("0 秒 = 立即开始", "0 s = start at once"), 17, rgb(C_TEXT, 0.5f), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f},
         Vec2{kPad + 100, y - 1}, Vec2{200, 30});
    // [ − ]  3.0 秒  [ + ]  on the right, under the switch
    float plusX = -kPad - 30, valueX = plusX - 30 - 62, minusX = valueX - 62 - 30;
    stepButton(card, plusX, y, "+", 1);
    Obj* v = text(card, "", 23, rgb(C_CLAY), MiddleCenter, true, Vec2{1, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{valueX, y},
                  Vec2{120, 40});
    g_durText.set(v);
    paintDuration();
    stepButton(card, minusX, y, "−", -1);
}

static void clearPage() {
    g_durText.clear();
    for (Handle& h : g_volText) h.clear();
    g_maskText.clear();
    g_sub.clear();
    clearSeg(g_segs[1]);
    for (int i = 0; i < g_rowCount; i++) {
        g_rows[i].track.clear();
        g_rows[i].knob.clear();
    }
    g_rowCount = 0;
}

static void closePanel(void*) {
    Obj* root = (Obj*)g_root.get();
    uiFadeAway(root);  // fades out, then destroyed
    g_root.clear();
    g_card.clear();
    g_dim.clear();
    g_clawd.clear();
    g_page.clear();
    clearSeg(g_segs[0]);
    clearPage();
    g_openT = -1;
    songButtonsRepaint();  // the panel can change 练习 / 自动 while song select is underneath
}

bool panelIsOpen() { return g_root.h && alive((Obj*)g_root.get()); }
void panelCloseNow() {
    if (panelIsOpen()) closePanel(nullptr);
}
void panelRefresh() {
    if (panelIsOpen()) buildPage();
}

// ---- building blocks ----
static void divider(Obj* card, float y) {
    image(card, "divider", SPR_SOLID, rgb(C_LINE), Vec2{0.5f, 0.5f}, Vec2{0, y}, Vec2{kCardW - 2 * kPad, 2});
}

// One settings row: label + description on the left, switch on the right. The whole row is the button.
static void toggleRow(Obj* card, float y, const char* label, const char* desc, bool* value) {
    if (g_rowCount >= kMaxRows) return;
    ToggleRow& r = g_rows[g_rowCount++];
    r.value = value;
    Obj* hit = image(card, "row", SPR_SOLID, Color{1, 1, 1, 0}, Vec2{0.5f, 0.5f}, Vec2{0, y}, Vec2{kCardW - 2 * kPad, 104},
                     0, true);
    Obj* hrt = rt(hit);
    text(hrt, label, 27, rgb(C_TEXT), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{0, 17}, Vec2{560, 40});
    text(hrt, desc, 20, rgb(C_MUTED), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{0, -19}, Vec2{600, 30});
    Obj* track = image(hrt, "track", SPR_ROUND, rgb(C_TRACK_OFF), Vec2{1, 0.5f}, Vec2{-44, 0}, Vec2{88, 50}, 25);
    Obj* knob = image(rt(track), "knob", SPR_CIRCLE, rgb(0xFFFFFF), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{40, 40});
    shadow(knob, Color{0, 0, 0, 0.12f}, Vec2{0, -1.5f});
    r.track.set(track);
    r.knob.set(knob);
    paintRow(r, true);
    onClick(gameObject(hit), nullptr, onRowClick, &r);
}

// One action row ("存档" page): label + description on the left, one or two small buttons on the right
// (an outlined secondary one, then the Claude-orange primary one at the far right)
static void onSaveButton(void* user) { saveAction((int)(intptr_t)user); }
static void smallButton(Obj* row, float right, const char* label, bool primary, int action) {
    const float w = 96, h = 48;
    Obj* b = image(row, "btn", SPR_ROUND, rgb(primary ? C_CLAY : C_LINE), Vec2{1, 0.5f}, Vec2{-right - w / 2, 0},
                   Vec2{w, h}, 14, true);
    if (!primary)
        image(rt(b), "fill", SPR_ROUND, rgb(C_IVORY), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{w - 4, h - 4}, 12);
    text(rt(b), label, 22, rgb(primary ? 0xFFFFFF : C_TEXT), MiddleCenter, primary, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f},
         Vec2{0, 1}, Vec2{w, h});
    onClick(gameObject(b), b, onSaveButton, (void*)(intptr_t)action);
}
static void actionRow(Obj* card, float y, const char* label, const char* desc, const char* second, int secondAction,
                      const char* primary, int primaryAction) {
    Obj* row = newUI("row", card);
    place(row, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, y}, Vec2{kCardW - 2 * kPad, 104});
    text(row, label, 27, rgb(C_TEXT), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{0, 17}, Vec2{460, 40});
    text(row, desc, 20, rgb(C_MUTED), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{0, -19}, Vec2{500, 30});
    smallButton(row, 0, primary, true, primaryAction);
    if (second) smallButton(row, 96 + 12, second, false, secondAction);
}

static void note(Obj* parent, float y, const char* s) {
    text(parent, s, 20, rgb(C_MUTED), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{kPad, y},
         Vec2{kCardW - 2 * kPad, 30});
}

static void volRow(Obj* page, float y, int k);

// ---- 皮肤: the game's own look, then every imported pack; 使用 switches, 删除 asks first ----
static void onSkinUse(void* u) { skinUse((int)(intptr_t)u); }
static void onSkinDelete(void* u) { skinAskDelete((int)(intptr_t)u); }
static void skinButton(Obj* row, float right, const char* label, int kind, il2cpp::NativeCallback cb, int id) {
    // kind 0 = outlined (删除), 1 = Claude orange (使用), 2 = the pack in use (no action)
    const float w = 96, h = 48;
    Obj* b = image(row, "btn", SPR_ROUND, rgb(kind == 1 ? C_CLAY : C_LINE), Vec2{1, 0.5f}, Vec2{-right - w / 2, 0},
                   Vec2{w, h}, 14, kind != 2);
    if (kind != 1) image(rt(b), "fill", SPR_ROUND, rgb(kind == 2 ? C_SEG_BG : C_IVORY), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{w - 4, h - 4}, 12);
    text(rt(b), label, 22, rgb(kind == 1 ? 0xFFFFFF : kind == 2 ? C_CLAY : C_TEXT), MiddleCenter, kind != 0,
         Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, 1}, Vec2{w, h});
    if (kind != 2) onClick(gameObject(b), b, cb, (void*)(intptr_t)id);
}
static void skinRow(Obj* page, float y, const char* label, const char* desc, int id) {
    Obj* row = newUI("row", page);
    place(row, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, y}, Vec2{kCardW - 2 * kPad, 104});
    text(row, label, 27, rgb(C_TEXT), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{0, 17}, Vec2{460, 40});
    text(row, desc, 20, rgb(C_MUTED), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{0, -19}, Vec2{500, 30});
    bool cur = skinCurrent() == id;
    skinButton(row, 0, cur ? T("使用中", "In use") : T("使用", "Use"), cur ? 2 : 1, onSkinUse, id);
    if (id) skinButton(row, 96 + 12, T("删除", "Delete"), 0, onSkinDelete, id);
}
static void skinPage(Obj* page, float& y) {
    y -= 10 + 52;
    skinRow(page, y, T("游戏原版", "The game's own"), T("不换皮肤；可以使用「区分大小 P」", "No skin; Critical Perfect available"), 0);
    int n = skinLibCount();
    for (int i = 0; i < n; i++) {
        int id;
        const char *name, *author;
        if (!skinLibEntry(i, &id, &name, &author)) continue;
        y -= 52 + 10;
        divider(page, y);
        y -= 10 + 52;
        char desc[200];
        snprintf(desc, sizeof desc, "%s%s", author[0] ? author : T("作者未知", "Unknown author"),
                 skinBroken(id) ? T("  ·  读不出来了，请删除后重新导入", "  ·  unreadable, delete and import again") : "");
        skinRow(page, y, name, desc, id);
    }
    y -= 52 + 10;
    divider(page, y);
    y -= 10 + 52;
    actionRow(page, y, T("导入皮肤包", "Import a skin"), T("Phira 格式的 .zip，导入前会检查格式；最多 16 个", "A Phira .zip, checked first; up to 16"), nullptr, 0, T("导入", "Import"), SAVE_SKIN_IMPORT);
    y -= 52 + 10;
    divider(page, y);
    y -= 10 + 30;
    text(page, T("打击音量", "Hit sound volume"), 25, rgb(C_TEXT), MiddleLeft, true, Vec2{0, 0.5f}, Vec2{0, 0.5f},
         Vec2{kPad, y}, Vec2{300, 36});
    text(page, T("叠加在游戏的音效音量上，皮肤和原版音效都生效", "On top of the game's SE volume, skin or not"), 18,
         rgb(C_MUTED), MiddleRight, false, Vec2{1, 0.5f}, Vec2{1, 0.5f}, Vec2{-kPad, y}, Vec2{460, 30});
    for (int k = 0; k < 3; k++) {
        y -= 30 + 34;
        volRow(page, y, k);
    }
    y -= 34 + 10;
    divider(page, y);
    y -= 34;
    note(page, y, T("皮肤会替换音符、打击特效和打击音，下一首歌生效。", "Skins replace notes, hit effects and hit sounds from the next song."));
    y -= 32;
    note(page, y, T("使用皮肤时不区分大小 P（皮肤没有大小 P 两种样式）。", "Critical Perfect is off while a skin is in use (skins have one Perfect look)."));
}

// ---- hit sound volume per note colour (blue = tap / hold, yellow = drag, red = flick): 0 - 200 % in 10 % steps ----
static int* volOf(int k) { return k == 0 ? &g_cfg.volTap : k == 1 ? &g_cfg.volDrag : &g_cfg.volFlick; }
static void paintVol(int k) {
    Obj* t = (Obj*)g_volText[k].get();
    if (!alive(t)) return;
    char b[16];
    snprintf(b, sizeof b, "%d%%", *volOf(k));
    setText(t, b);
}
static void onVol(void* user) {
    int u = (int)(intptr_t)user, k = u >> 1;
    int v = *volOf(k) + ((u & 1) ? 10 : -10);
    *volOf(k) = v < 0 ? 0 : v > 200 ? 200 : v;
    configSave();
    paintVol(k);
    LOG("config: hit sound volume tap %d drag %d flick %d", g_cfg.volTap, g_cfg.volDrag, g_cfg.volFlick);
}
static void volButton(Obj* page, float x, float y, const char* glyph, int user) {
    Obj* b = image(page, "step", SPR_ROUND, rgb(C_LINE), Vec2{1, 0.5f}, Vec2{x, y}, Vec2{60, 46}, 14, true);
    Obj* f = image(rt(b), "fill", SPR_ROUND, rgb(C_IVORY), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{56, 42}, 12);
    text(rt(f), glyph, 28, rgb(C_TEXT), MiddleCenter, false, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, 1}, Vec2{56, 42});
    onClick(gameObject(b), b, onVol, (void*)(intptr_t)user);
}
static void volRow(Obj* page, float y, int k) {
    static const int dots[3] = {0x0AC3FF, 0xF0ED69, 0xFE4365};  // the game's tap blue, drag yellow, flick red
    const char* const names[3] = {T("蓝键（Tap / Hold）", "Blue (Tap / Hold)"), T("黄键（Drag）", "Yellow (Drag)"),
                                  T("红键（Flick）", "Red (Flick)")};
    image(page, "dot", SPR_CIRCLE, rgb(dots[k]), Vec2{0, 0.5f}, Vec2{kPad + 9, y}, Vec2{18, 18});
    text(page, names[k], 23, rgb(C_TEXT), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{kPad + 30, y}, Vec2{360, 36});
    float plusX = -kPad - 30, valueX = plusX - 30 - 62, minusX = valueX - 62 - 30;
    volButton(page, plusX, y, "+", k * 2 + 1);
    Obj* v = text(page, "", 23, rgb(C_CLAY), MiddleCenter, true, Vec2{1, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{valueX, y}, Vec2{120, 40});
    g_volText[k].set(v);
    paintVol(k);
    volButton(page, minusX, y, "−", k * 2);
}

// ---- 上隐条 height: 0 - 50 % of the screen in 1 % steps ----
static void paintMask() {
    Obj* t = (Obj*)g_maskText.get();
    if (!alive(t)) return;
    char b[16];
    snprintf(b, sizeof b, "%d%%", g_cfg.maskPct);
    setText(t, b);
}
static void onMask(void* user) {
    int v = g_cfg.maskPct + (int)(intptr_t)user;
    g_cfg.maskPct = v < 0 ? 0 : v > 50 ? 50 : v;
    configSave();
    paintMask();
    LOG("config: hide bar %d%%", g_cfg.maskPct);
}
static void maskRow(Obj* page, float y) {
    text(page, T("遮罩高度", "Band height"), 23, rgb(C_TEXT), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{kPad, y},
         Vec2{300, 36});
    text(page, T("从屏幕顶部算起", "from the top of the screen"), 17, rgb(C_TEXT, 0.5f), MiddleLeft, false, Vec2{0, 0.5f},
         Vec2{0, 0.5f}, Vec2{kPad + 110, y - 1}, Vec2{240, 30});
    float plusX = -kPad - 30, valueX = plusX - 30 - 62, minusX = valueX - 62 - 30;
    const char* glyph[2] = {"−", "+"};
    for (int s = 0; s < 2; s++) {
        Obj* b = image(page, "step", SPR_ROUND, rgb(C_LINE), Vec2{1, 0.5f}, Vec2{s ? plusX : minusX, y}, Vec2{60, 46}, 14, true);
        Obj* f = image(rt(b), "fill", SPR_ROUND, rgb(C_IVORY), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{56, 42}, 12);
        text(rt(f), glyph[s], 28, rgb(C_TEXT), MiddleCenter, false, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, 1}, Vec2{56, 42});
        onClick(gameObject(b), b, onMask, (void*)(intptr_t)(s ? 1 : -1));
    }
    Obj* v = text(page, "", 23, rgb(C_CLAY), MiddleCenter, true, Vec2{1, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{valueX, y}, Vec2{120, 40});
    g_maskText.set(v);
    paintMask();
}

static void accModeRow(Obj* parent, float y) {
    text(parent, T("计算方式", "Method"), 23, rgb(C_TEXT), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{kPad, y},
         Vec2{300, 36});
    const char* const modes[] = {T("实时", "Live"), T("最高", "Best")};
    const float w = 100;
    segmented(1, parent, Vec2{1, 0.5f}, Vec2{-kPad - (2 * w + 8) / 2, y}, w, 40, modes, 2, &g_cfg.accMode, onAccMode);
}

// both languages in the label, so it can be found whichever one is on
static void langRow(Obj* parent, float y) {
    text(parent, "语言 / Language", 23, rgb(C_TEXT), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{kPad, y},
         Vec2{360, 36});
    const char* const langs[] = {"中文", "English"};
    const float w = 120;
    segmented(1, parent, Vec2{1, 0.5f}, Vec2{-kPad - (2 * w + 8) / 2, y}, w, 40, langs, 2, &g_cfg.lang, onLang);
}

// "游玩": play switches. "显示": the in-level accuracy line and judgement counts.
static void buildPage() {
    Obj* card = (Obj*)g_card.get();
    if (!alive(card)) return;
    Obj* old = (Obj*)g_page.get();
    if (alive(old)) destroy(gameObject(old));
    clearPage();
    // The page scrolls inside a viewport under the header / tabs (and above 完成 on tablets). Rows keep their
    // card coordinates: the page is the card's size, placed in the content so that, unscrolled, it sits exactly
    // where the card is.
    float pageW = g_phone ? kCardW : g_cardW;
    float pageX = g_phone ? g_cardW / 2 - kCardW / 2 : 0;  // right of the sidebar on phones
    float vpTop = g_phone ? g_top - 96 - 2 : kTop - 96 - kTabsH + 4;
    float vpBot = g_phone ? -g_top + 14 : -g_top + 30 + 28 + 28 + 12;  // tablets: above 完成
    Obj* content = scrollArea(card, "pageView", Vec2{pageX, (vpTop + vpBot) / 2}, Vec2{pageW, vpTop - vpBot}, 0);
    g_page.set(parentOf(content));
    Obj* page = newUI("page", content);
    place(page, Vec2{0.5f, 1}, Vec2{0.5f, 1}, Vec2{0, g_top - vpTop}, Vec2{pageW, g_cardH});

    float y = g_phone ? g_top - 96 - 4 : kTop - 96 - kTabsH;
    pageBody(page, y);
    // content height: from the viewport's top to the lowest row, plus a little room
    scrollAreaSetHeight(content, vpTop - (y - 36));
}

// ---- 关于: the mod folder, the license, respecting the official copyright ----
// a wrapped paragraph with its top at y (left edge kPad + indent); returns its height
static float para(Obj* page, float y, const char* s, int size, int color, bool bold = false, float indent = 0) {
    float w = kCardW - 2 * kPad - indent;
    float h = textHeight(s, size, bold, w);
    Obj* t = text(page, s, size, rgb(color), UpperLeft, bold, Vec2{0, 0.5f}, Vec2{0, 1}, Vec2{kPad + indent, y},
                  Vec2{w, h + 6});
    setTextWrap(t, true);
    return h;
}
// one term of the license: its name on the left, what it means beside it
static void termRow(Obj* page, float& y, const char* term, const char* desc) {
    const float col = 176;
    text(page, term, 21, rgb(C_TEXT), UpperLeft, true, Vec2{0, 0.5f}, Vec2{0, 1}, Vec2{kPad, y}, Vec2{col - 14, 30});
    float h = para(page, y, desc, 21, C_BODY, false, col);
    y -= (h > 28 ? h : 28) + 12;
}
static void aboutPage(Obj* page, float& y) {
    y -= 10 + 52;
#if defined(__APPLE__)
    actionRow(page, y, T("Mod 文件夹", "Mod folder"), T("在「文件」App 里打开：日志 PhiMeMod.log 和皮肤", "Opens in Files: PhiMeMod.log and skins"), nullptr, 0, T("打开", "Open"), SAVE_OPEN_FOLDER);
#else
    actionRow(page, y, T("Mod 文件夹", "Mod folder"), T("日志在这里；新的 libPhiMeMod.so 放这里重启即生效", "The log; drop a new libPhiMeMod.so here + restart"), nullptr, 0, T("打开", "Open"), SAVE_OPEN_FOLDER);
#endif
    y -= 52 + 10;
    divider(page, y);
    y -= 10 + 52;
    actionRow(page, y, T("新手引导", "Guide"), T("功能介绍，以及这个安装该怎么登录 TapTap", "What PhiMe does, and how this install logs in to TapTap"), nullptr, 0, T("查看", "Open"), SAVE_TUTORIAL);
    y -= 52 + 10;
    divider(page, y);
    y -= 10 + 52;
    actionRow(page, y, T("开源协议", "License"), "CC BY-NC-SA 4.0", nullptr, 0, T("原文", "Read"), SAVE_LICENSE);
    y -= 52 + 10;
    termRow(page, y, T("署名", "Attribution"), T("转载、修改或再发布时，注明出自 PhiMe，并附上原项目的链接。", "Credit PhiMe and link the original project when you share or modify it."));
    termRow(page, y, T("非商业性使用", "NonCommercial"), T("不得用于任何商业用途：售卖、付费代装、捆绑收费、加广告盈利都不行。", "No commercial use of any kind: no selling, paid installs, paid bundles or ads."));
    termRow(page, y, T("相同方式共享", "ShareAlike"), T("基于 PhiMe 修改的版本，也要用同一个协议发布。", "Modified versions must be shared under the same license."));
    y -= 4;
    y -= para(page, y, T("PhiMe 完全免费。如果你是花钱买到的，那你被骗了！", "PhiMe is free. If you paid for it, you were scammed!"), 22, C_CLAY, true) + 24;
    divider(page, y);
    y -= 24;
    y -= para(page, y, T("尊重官方版权", "Respecting the official copyright"), 25, C_TEXT, true) + 14;
    y -= para(page, y, T("PhiMe 是玩家自制的非官方 Mod，与鸽游（Pigeon Games）没有关联。它不包含、也不分发游戏本体和任何游戏资源。", "PhiMe is an unofficial, fan-made mod with no connection to Pigeon Games. It neither contains nor distributes the game or any of its assets."), 21, C_BODY) + 10;
    y -= para(page, y, T("Phigros 以及其中的曲目、曲绘和谱面，版权归鸽游和各位原作者所有。请从官方渠道获取游戏，支持正版，支持鸽游！", "Phigros and its songs, illustrations and charts belong to Pigeon Games and their original authors. Please get the game from official sources and support it!"), 21, C_BODY) + 24;
    divider(page, y);
    y -= 24;
    y -= para(page, y, T("只为推分，绝不作弊", "For practice, never for cheating"), 25, C_TEXT, true) + 14;
    y -= para(page, y, T("PhiMe 不修改官方的判定、分数和 RKS；练习、自动游玩和课题模式的成绩，都不会写进游戏的成绩记录。PhiMe 也不提供自制谱导入，以后也不会有。", "PhiMe never changes the official judgements, scores or RKS; practice, autoplay and course runs never reach the game's records. There is no custom chart import, and there never will be."), 21, C_BODY) + 24;
    divider(page, y);
    y -= 20;
    y -= para(page, y, T("如果鸽游认为 PhiMe 有任何不妥，作者会第一时间配合处理。", "If Pigeon Games finds anything about PhiMe inappropriate, the author will cooperate right away."), 19, C_MUTED) + 8;
    y -= para(page, y, T("第三方代码：stb_vorbis、stb_truetype（Sean Barrett，公共领域 / MIT）；dr_mp3、dr_flac（David Reid，公共领域 / MIT-0）。", "Third-party code: stb_vorbis, stb_truetype (Sean Barrett, public domain / MIT); dr_mp3, dr_flac (David Reid, public domain / MIT-0)."), 19, C_MUTED);
}

// ---- 账号: the TapTap login (PhiMe's QR login where the game's own one is refused) ----
static void infoRow(Obj* page, float y, const char* label, const char* desc) {
    Obj* row = newUI("row", page);
    place(row, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, y}, Vec2{kCardW - 2 * kPad, 104});
    text(row, label, 27, rgb(C_TEXT), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{0, 17}, Vec2{660, 40});
    text(row, desc, 20, rgb(C_MUTED), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{0, -19}, Vec2{700, 30});
}
static void accountPage(Obj* page, float& y) {
    bool in = accountLoggedIn(), mod = accountModLogin();
    const char* nick = accountNick();
    char st[200];
    if (in && nick[0]) snprintf(st, sizeof st, T("已登录：%s", "Logged in: %s"), nick);
    else snprintf(st, sizeof st, "%s", in ? T("已登录", "Logged in") : T("未登录", "Not logged in"));
    y -= 10 + 52;
    if (!in && mod)
        actionRow(page, y, st, T("登录后才能用游戏的云存档同步", "Needed for the game's cloud save sync"), nullptr, 0,
                  T("登录", "Log in"), SAVE_LOGIN);
    else if (!in)
        infoRow(page, y, st, T("这个安装用游戏自带的登录：游戏「设置」里的账号页点「登录」", "This install uses the game's own login: 登录 on the game's account page"));
    else
        infoRow(page, y, st, T("同步：游戏「设置」里的账号页点「同步」；也可以打开自动同步", "Sync: 同步 on the game's account page, or turn on auto sync"));
    y -= 52 + 10;
    divider(page, y);
    y -= 24;
    char how[200];
#if defined(__APPLE__)
    if (mod) snprintf(how, sizeof how, T("登录方式：PhiMe 扫码登录（包名 %s）", "Login: PhiMe's QR login (bundle ID %s)"), accountBundleId());
    else snprintf(how, sizeof how, "%s", T("登录方式：游戏自带的 TapTap 登录（原版包名）", "Login: the game's own TapTap login (official bundle ID)"));
#else
    snprintf(how, sizeof how, "%s", T("登录方式：PhiMe 扫码登录（安装包重签过名）", "Login: PhiMe's QR login (re-signed APK)"));
#endif
    y -= para(page, y, how, 23, C_TEXT, true) + 12;
    y -= para(page, y, loginHelpText(), 20, C_BODY) + 24;
    if (mod) {
        divider(page, y);
        y -= 10 + 52;
        actionRow(page, y, T("保存的登录", "Saved login"), T("清除后下次登录要重新扫码，换账号时用", "Clear it to scan again, e.g. for another account"),
                  nullptr, 0, T("清除", "Clear"), SAVE_FORGET_LOGIN);
        y -= 52 + 10;
    }
}

static void pageBody(Obj* page, float& y) {
    if (g_tab == 3) {
        skinPage(page, y);
        return;
    }
    if (g_tab == 4) {
        accountPage(page, y);
        return;
    }
    if (g_tab == 5) {
        aboutPage(page, y);
        return;
    }
    if (g_tab == 0) {
        y -= 10 + 52;
        actionRow(page, y, T("B30 成绩", "Best 30"), T("Best 27 + Phi 3、单曲 RKS 和推分准度", "Best 27 + Phi 3, single RKS and the accuracy for +0.01"), nullptr, 0, T("查看", "Open"), SAVE_B30);
        y -= 52 + 10;
        divider(page, y);
        y -= 10 + 52;
        toggleRow(page, y, T("自动游玩", "Autoplay"), T("Clawd 帮你把所有音符打成 Perfect", "Clawd hits every note as a Perfect for you"), &g_cfg.autoplay);
        y -= 52 + 10;
        divider(page, y);
        y -= 10 + 52;
        toggleRow(page, y, T("练习模式", "Practice mode"), T("进入谱面直接打开选段；不会结算（选歌界面也有开关）", "Opens the section picker; results are never saved"), &g_cfg.practice);
        y -= 52 + 10;
        divider(page, y);
        y -= 10 + 52;
        toggleRow(page, y, T("继续前回放谱面", "Replay before resuming"), T("按继续后倒数准备时间并回放谱面，不影响判定", "Replays the chart during a lead-in after Resume"), &g_cfg.resumePreview);
        y -= 52 + 4 + 34;
        durationRow(page, y);
        y -= 34 + 12;
        divider(page, y);
        y -= 34;
        note(page, y, T("自动游玩会忽略手指判定；结算照常显示，但成绩不会保存到本地和云端。", "Autoplay ignores touches; results show but are never saved."));
    } else if (g_tab == 2) {
        y -= 10 + 52;
        toggleRow(page, y, T("区分大小 P", "Critical Perfect"), T("始终判定（±40 ms，课题 ±20 ms）；开启后分开显示：金黄特效、每个 +1 分", "Always judged (±40 ms, course ±20 ms); shown: gold effect, +1 each"), &g_cfg.critical);
        y -= 52 + 10;
        divider(page, y);

        y -= 10 + 52;
        toggleRow(page, y, T("正解音", "On-time hit sounds"), T("打击音不跟手指时机走，每个音符都在正确的时间响", "Every note's hit sound plays at its exact time, not at your touch"), &g_cfg.correctSound);
        y -= 52 + 10;
        divider(page, y);
        y -= 10 + 52;
        toggleRow(page, y, T("快速加载", "Fast loading"), T("进出曲包的加载界面不再干等，加载好就进入", "Chapter loading screens no longer wait idly"), &g_cfg.fastLoad);
        y -= 52 + 10;
        divider(page, y);
        y -= 10 + 30;
        langRow(page, y);
        y -= 30 + 10;
        divider(page, y);
        y -= 34;
        note(page, y, T("大 P 加的分只在打歌时显示，结算和存档里的分数还是游戏原本的。", "Critical bonus points show only in play; saves keep the game's score."));
        y -= 32;
        note(page, y, T("快速加载不影响进入歌曲时的加载（那边要等谱面和音乐）。", "Fast loading does not change song loading (chart and music)."));
    } else {
        y -= 10 + 52;
        toggleRow(page, y, T("准度显示", "Accuracy line"), T("打歌时在分数下方用小字显示 Accuracy", "Shows the accuracy under the score while playing"), &g_cfg.accShow);
        // sub-options of the accuracy line, faded while it is off
        Obj* sub = newUI("sub", page);
        stretch(sub, 0);
        g_sub.set(addCanvasGroup(gameObject(sub)));
        y -= 52 + 4 + 34;
        accModeRow(sub, y);
        y -= 34 + 4 + 52;
        toggleRow(sub, y, T("准度显示为大字", "Accuracy as the big number"), T("准度换到分数的位置，分数改为下方小字", "Accuracy in the score's place, score on the small line"), &g_cfg.accSwap);
        y -= 52 + 10;
        divider(page, y);
        y -= 10 + 52;
        toggleRow(page, y, T("判定详情", "Judgement counts"), T("连击下方显示 Perfect / Good / Bad+Miss 数量", "Perfect / Good / Bad+Miss counts under the combo"), &g_cfg.judgeShow);
        y -= 52 + 10;
        divider(page, y);
        y -= 10 + 52;
        toggleRow(page, y, T("隐藏分数和连击", "Hide score and combo"), T("打歌时不显示分数、准度、连击数和 COMBO 字样", "No score, accuracy, combo count or COMBO label while playing"), &g_cfg.hideScore);
        y -= 52 + 10;
        divider(page, y);
        y -= 10 + 52;
        toggleRow(page, y, T("上隐条", "Top hide bar"), T("屏幕上方的黑色遮罩，只遮谱面；有音符的判定线进入时自动变透明", "A black band over the top of the chart; clears for judge lines with notes"), &g_cfg.maskOn);
        y -= 52 + 10 + 30;
        maskRow(page, y);
        y -= 30 + 10 + 52;
        actionRow(page, y, T("预览调整", "Adjust in a preview"), T("在 Rrhar'il AT 的 1:22 处拖动调整遮罩高度", "Drag the band's height over Rrhar'il AT from 1:22"), nullptr, 0, T("开始", "Start"), SAVE_MASK_PREVIEW);
        y -= 52 + 10;
        divider(page, y);
        y -= 34;
        note(page, y, T("实时：按已判定的音符计算；最高：之后全部 Perfect 能达到的准度。", "Live: notes judged so far. Best: reachable with all Perfects left."));
        y -= 32;
        note(page, y, T("判定详情颜色：黄色 Perfect，蓝色 Good，灰色 Bad + Miss。", "Colours: yellow Perfect, blue Good, grey Bad + Miss."));
        paintSub();
    }
}

static void openPanel(void*) {
    configLoad();
    closePanel(nullptr);
    Obj* root = overlayCanvas("PhiMeModPanel", 32000);
    g_root.set(root);

    // backdrop, tap outside the card to close
    Obj* dim = image(root, "dim", SPR_SOLID, Color{0.08f, 0.07f, 0.06f, 0.f}, Vec2{0.5f, 0.5f}, Vec2{0, 0},
                     Vec2{100, 100}, 0, true);
    stretch(rt(dim), -20);
    onClick(gameObject(dim), nullptr, closePanel, nullptr);
    g_dim.set(dim);

    g_phone = isPhone();
    g_cardW = g_phone ? kSideW + kCardW : kCardW;
    g_cardH = g_phone ? kPhoneCardH : kCardH;
    g_top = g_cardH / 2;
    Obj* card = newUI("card", root);
    place(card, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, g_phone ? -36.f : -20.f}, Vec2{g_cardW, g_cardH});
    g_card.set(card);
    image(card, "border", SPR_ROUND, rgb(C_LINE), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{g_cardW + 4, g_cardH + 4}, 26);
    image(card, "bg", SPR_ROUND, rgb(C_IVORY), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{g_cardW, g_cardH}, 24, true);

    // Clawd on the top edge, hopping now and then
    Obj* clawd = image(card, "clawd", SPR_CLAWD, rgb(0xFFFFFF), Vec2{0.5f, 0.5f}, clawdPos(), Vec2{120, 68});
    g_clawd.set(clawd);
    g_clawdT = 0;

    // header
    float y = g_top - 48;
    text(card, T("PhiMe 小助手", "PhiMe Assistant"), 32, rgb(C_TEXT), MiddleLeft, true, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{kPad, y},
         Vec2{400, 48});
    text(card, "v" PM_VERSION, 19, rgb(C_MUTED), MiddleRight, false, Vec2{1, 0.5f}, Vec2{1, 0.5f}, Vec2{-kPad, y - 2},
         Vec2{160, 30});
    y = g_top - 96;
    image(card, "divider", SPR_SOLID, rgb(C_LINE), Vec2{0.5f, 0.5f}, Vec2{0, y}, Vec2{g_cardW - 2 * kPad, 2});

    const char* const tabs[] = {T("游玩", "Play"),  T("显示", "Display"),  T("其他", "Other"),
                                T("皮肤", "Skins"), T("账号", "Account"), T("关于", "About")};
    Obj* done;
    if (g_phone) {
        // sidebar: the tabs stacked, 完成 at the bottom, a hairline between it and the page
        const float tw = kSideW - 52, th = 60, sx = kSideW / 2 + 10;
        segmented(0, card, Vec2{0, 0.5f}, Vec2{sx, g_top - 96 - 22 - (6 * th + 8) / 2}, tw, th, tabs, 6, &g_tab, onTab,
                  true);
        image(card, "sideLine", SPR_SOLID, rgb(C_LINE), Vec2{0, 0.5f}, Vec2{kSideW + 4, -36},
              Vec2{2, 2 * g_top - 96 - 24 - 40});
        done = image(card, "done", SPR_ROUND, rgb(C_CLAY), Vec2{0, 0}, Vec2{sx, 30 + 28}, Vec2{tw, 56}, 14, true);
    } else {
        // tab bar under the header, 完成 bottom right
        segmented(0, card, Vec2{0, 0.5f}, Vec2{kPad + (6 * 120 + 8) / 2.f, kTop - 96 - kTabsH / 2 - 4}, 120, 44, tabs, 6,
                  &g_tab, onTab);
        done = image(card, "done", SPR_ROUND, rgb(C_CLAY), Vec2{1, 0}, Vec2{-kPad - 70, 30 + 28}, Vec2{140, 56}, 14,
                     true);
    }
    buildPage();
    text(rt(done), T("完成", "Done"), 23, rgb(0xFFFFFF), MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, 1},
         Vec2{130, 50});
    onClick(gameObject(done), done, closePanel, nullptr);

    setScale(card, 0.96f);
    g_openT = 0;
}

static float easeOutCubic(float t) {
    float u = 1 - t;
    return 1 - u * u * u;
}

// driven by Setting.Update in the settings screen and by the per-frame callback elsewhere: once a frame
static int g_tickFrame = -1;
static void panelTick() {
    if (!g_root.h) return;
    int frame = frameCount();
    if (frame == g_tickFrame) return;
    g_tickFrame = frame;
    Obj* root = (Obj*)g_root.get();
    if (!alive(root)) {  // scene changed under us
        closePanel(nullptr);
        return;
    }
    float dt = unscaledDt();
    if (g_openT >= 0) {  // quiet fade + slight zoom in
        g_openT += dt / 0.18f;
        float t = g_openT >= 1 ? 1 : g_openT;
        float e = easeOutCubic(t);
        Obj* card = (Obj*)g_card.get();
        if (alive(card)) setScale(card, 0.96f + 0.04f * e);
        Obj* dim = (Obj*)g_dim.get();
        if (alive(dim)) setColor(dim, Color{0.08f, 0.07f, 0.06f, 0.4f * e});
        if (g_openT >= 1) g_openT = -1;
    }
    Obj* clawd = (Obj*)g_clawd.get();
    if (alive(clawd)) {
        g_clawdT += dt;
        Vec2 cp = clawdPos();
        setPos(rt(clawd), Vec2{cp.x, cp.y + clawdHop(g_clawdT, 14)});
    }
    float k = dt * 18;
    if (k > 1) k = 1;
    for (int i = 0; i < g_rowCount; i++) {
        ToggleRow& r = g_rows[i];
        float tx = target(r);
        if (r.knobX != tx) {
            r.knobX += (tx - r.knobX) * k;
            if ((tx - r.knobX) * (tx - r.knobX) < 0.04f) r.knobX = tx;
            paintRow(r, false);
        }
    }
}

static void panelFrame(void*) { panelTick(); }

void panelOpen() {
    onEveryFrame(panelFrame);
    openPanel(nullptr);
    LOG("panel opened from song select");
}

static void addEntry(Obj* setting) {
    Obj* mgr = fld<Obj*>(setting, P.settingItemsManager);
    if (!mgr) return;
    Obj* title = il2cpp_object_new(P.MLTS);
    callm<void>(P.mltsCtor, title);
    callm<void>(P.mltsSet, title, str(T("PhiMe 小助手", "PhiMe Assistant")));
    Obj* action = makeUnityAction(openPanel, nullptr);
    if (!action) return;
    Obj* listeners = action;
    if (P.listenersIsArray) {
        Il2CppArray* arr = newArray(P.UnityAction, 1);
        arrayData(arr)[0] = action;
        listeners = (Obj*)arr;
    }
    callm<Obj*>(P.addButton, mgr, title, listeners);
    if (P.refreshItems) callm<void>(P.refreshItems, mgr);
    LOG("settings entry added");
}

static void hk_Setting_Start(Obj* self, const MethodInfo* m) {
    orig_Setting_Start(self, m);
    configLoad();
    addEntry(self);
    skinSettingStart(self);  // the delay preview's note, click effect and hit sound in the skin
}

static void hk_Setting_Update(Obj* self, const MethodInfo* m) {
    orig_Setting_Update(self, m);
    panelTick();
}

bool panelInstall() {
    Il2CppClass* Setting = findClass("", "Setting");
    Il2CppClass* SIM = findClass("", "SettingItemsManager");
    P.MLTS = findClass("Phigros2.Utils.MultiLanguageText", "MultiLanguageTextString");
    P.UnityAction = findClass("UnityEngine.Events", "UnityAction");
    if (!Setting || !SIM || !P.MLTS || !P.UnityAction) return false;
    P.settingItemsManager = fieldOffset(Setting, "settingItemsManager");
    P.addButton = findMethod(SIM, "AddButton", 2);
    P.refreshItems = findMethod(SIM, "RefreshItems", 0);
    P.mltsCtor = findMethod(P.MLTS, ".ctor", 0);
    P.mltsSet = findMethod(P.MLTS, "set_NewString", 1);
    if (P.settingItemsManager < 0 || !P.addButton || !P.mltsCtor || !P.mltsSet) return false;
    P.listenersIsArray = il2cpp_type_get_type(il2cpp_method_get_param(P.addButton, 1)) == IL2CPP_TYPE_SZARRAY;

    const MethodInfo* start = findMethod(Setting, "Start", 0);
    const MethodInfo* update = findMethod(Setting, "Update", 0);
    if (!start || !update) return false;
    orig_Setting_Start = (VoidFn)hookMethod(start, (void*)hk_Setting_Start);
    orig_Setting_Update = (VoidFn)hookMethod(update, (void*)hk_Setting_Update);
    return true;
}
