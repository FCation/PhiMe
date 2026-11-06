// B30 查分: the save's Best 27 + Phi 3, worked out in the game like the community score checkers do.
//   single chart RKS = constant x ((acc - 55) / 45)^2 (0 below 70 %)
//   RKS = (the 27 best single RKS + the constants of the 3 hardest APs) / 30
// Per chart, 推分 = the accuracy that lifts the 2-decimal RKS by 0.01 (new APs not counted).
// Data: SongBase.AllSongs (SongsItem: songsId, songsName, levels, difficulty) and GameInformation.GetSongRecord,
// read once when the page opens. The game's own RankingScore is shown beside ours as a check.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

static const int C_CLAY = 0xD97757, C_IVORY = 0xFAF9F5, C_LINE = 0xE8E6DC, C_TEXT = 0x141413, C_MUTED = 0x87867F,
                 C_BODY = 0x3D3D3A;

static struct {
    Il2CppClass* GI;
    const MethodInfo *allSongs, *getRec, *getRks;
    int giSong, songId, songName, songLevels, songDiff, recS, recA, recC;
    bool recCBool, ok;
} K;

struct Chart {
    char name[96], level[8];
    float k, a, rks;
    int s;
    bool fc;
};
static Chart* g_ch = nullptr;
static int g_n = 0, g_cap = 0;
static int g_best[40], g_nBest = 0;  // indices by single RKS, best first (27 + the next few)
static int g_phi[3], g_nPhi = 0;     // APs, highest constant first
static float g_sum27 = 0, g_sumPhi = 0, g_rks = 0, g_gameRks = -1;

static Obj* gameInfo() {
    FieldInfo* f = K.GI ? il2cpp_class_get_field_from_name(K.GI, "_main") : nullptr;
    Obj* gi = nullptr;
    if (f) il2cpp_field_static_get_value(f, &gi);
    return gi;
}
// elements of a List<T> or a T[]
static int seq(Obj* o, void*** out) {
    *out = nullptr;
    if (!o) return 0;
    Il2CppClass* k = il2cpp_object_get_class(o);
    FieldInfo* fi = il2cpp_class_get_field_from_name(k, "_items");
    FieldInfo* fs = il2cpp_class_get_field_from_name(k, "_size");
    Il2CppArray* a;
    int n;
    if (fi && fs) {
        a = fld<Il2CppArray*>(o, (int)il2cpp_field_get_offset(fi));
        n = fld<int>(o, (int)il2cpp_field_get_offset(fs));
        if (!a || n < 0 || (uintptr_t)n > a->max_length) return 0;
    } else {
        const char* nm = il2cpp_class_get_name(k);
        size_t L = strlen(nm);
        if (L < 2 || nm[L - 2] != '[' || nm[L - 1] != ']') return 0;
        a = (Il2CppArray*)o;
        n = (int)a->max_length;
    }
    *out = arrayData(a);
    return n;
}
static float chartRks(float k, float a) {
    if (a < 70) return 0;
    float x = (a - 55) / 45;
    return k * x * x;
}

// ---------------- the numbers ----------------
static bool gather() {
    g_n = 0;
    g_nBest = g_nPhi = 0;
    g_sum27 = g_sumPhi = g_rks = 0;
    g_gameRks = -1;
    Obj* gi = gameInfo();
    Obj* base = gi && K.giSong >= 0 ? fld<Obj*>(gi, K.giSong) : nullptr;
    if (!base || !K.ok) return false;
    bool ok = false;
    Obj* list = invokeObj(K.allSongs, base, nullptr, &ok);
    void** songs;
    int ns = ok ? seq(list, &songs) : 0;
    int recs = 0;
    for (int i = 0; i < ns; i++) {
        Obj* s = (Obj*)songs[i];
        if (!s) continue;
        Il2CppString* id = fld<Il2CppString*>(s, K.songId);
        void** lv;
        int nl = seq(fld<Obj*>(s, K.songLevels), &lv);
        Obj* d = K.songDiff >= 0 ? fld<Obj*>(s, K.songDiff) : nullptr;
        if (!id || !d || strcmp(il2cpp_class_get_name(il2cpp_object_get_class(d)), "Single[]")) continue;
        float* diffs = (float*)arrayData((Il2CppArray*)d);
        int nd = (int)((Il2CppArray*)d)->max_length;
        for (int l = 0; l < nl && l < nd; l++) {
            if (!lv[l] || !(diffs[l] > 0)) continue;
            void* args[2] = {id, lv[l]};
            bool rok = false;
            Obj* rec = invokeObj(K.getRec, gi, args, &rok);
            if (!rok || !rec) continue;
            int sc = fld<int>(rec, K.recS);
            float a = fld<float>(rec, K.recA);
            if (sc <= 0 && a <= 0) continue;
            recs++;
            if (g_n == g_cap) {
                int cap = g_cap ? g_cap * 2 : 512;
                Chart* m = (Chart*)malloc(sizeof(Chart) * cap);
                if (!m) break;
                if (g_ch) {
                    memcpy(m, g_ch, sizeof(Chart) * g_n);
                    free(g_ch);
                }
                g_ch = m;
                g_cap = cap;
            }
            Chart& c = g_ch[g_n++];
            c.name[0] = c.level[0] = 0;
            Il2CppString* nm = K.songName >= 0 ? fld<Il2CppString*>(s, K.songName) : nullptr;
            if (!nm || utf8(nm, c.name, sizeof c.name) <= 0) utf8(id, c.name, sizeof c.name);
            utf8((Il2CppString*)lv[l], c.level, sizeof c.level);
            c.k = diffs[l];
            c.s = sc;
            c.a = a > 100 ? 100 : a;
            c.fc = K.recCBool ? fld<bool>(rec, K.recC) : fld<int>(rec, K.recC) != 0;
            c.rks = chartRks(c.k, c.a);
        }
    }
    // best by single RKS (top 40 kept: 27 + candidates), APs by constant (top 3)
    for (int i = 0; i < g_n; i++) {
        const Chart& c = g_ch[i];
        if (c.rks > 0) {
            int p = g_nBest < 40 ? g_nBest++ : 40;
            while (p > 0 && g_ch[g_best[p - 1]].rks < c.rks) {
                if (p < 40) g_best[p] = g_best[p - 1];
                p--;
            }
            if (p < 40) g_best[p] = i;
        }
        if (c.s >= 1000000) {
            int p = g_nPhi < 3 ? g_nPhi++ : 3;
            while (p > 0 && g_ch[g_phi[p - 1]].k < c.k) {
                if (p < 3) g_phi[p] = g_phi[p - 1];
                p--;
            }
            if (p < 3) g_phi[p] = i;
        }
    }
    for (int i = 0; i < g_nBest && i < 27; i++) g_sum27 += g_ch[g_best[i]].rks;
    for (int i = 0; i < g_nPhi; i++) g_sumPhi += g_ch[g_phi[i]].k;
    g_rks = (g_sum27 + g_sumPhi) / 30;
    if (K.getRks) g_gameRks = callm<float>(K.getRks, gi);
    LOG("b30: %d songs, %d played charts, %d APs counted; RKS %.4f (best 27 %.3f + phi %.3f), game says %.4f", ns, recs,
        g_nPhi, g_rks, g_sum27, g_sumPhi, g_gameRks);
    return true;
}

// The accuracy this chart needs for the 2-decimal RKS to go up by 0.01; 0 = not possible below 100 % (or already AP)
static float pushAcc(int idx, int rank) {
    const Chart& c = g_ch[idx];
    if (c.s >= 1000000) return 0;
    float shown = (float)(int)(g_rks * 100 + 0.5f) / 100;
    float need = (shown + 0.005f) * 30 - (g_sum27 + g_sumPhi);  // what the 27 must gain
    if (need < 0.00001f) need = 0.00001f;
    float floorRks = g_nBest >= 27 ? g_ch[g_best[26]].rks : 0;   // what an outsider has to beat
    float want = (rank < 27 ? c.rks : floorRks) + need;
    if (want > c.k) return 0;
    float a = 55 + 45 * __builtin_sqrtf(want / c.k);
    a = (float)(int)(a * 100 + 0.999f) / 100;  // up to the next 0.01 %
    if (a > 100) return 0;
    return a < c.a ? c.a : a;
}

// ---------------- the page ----------------
static Handle g_root, g_card, g_dim, g_clawd;
static float g_openT = -1, g_clawdT = 0, g_cardW = 1700, g_cardH = 1000;

static int diffColor(const char* lv) {
    if (!strcmp(lv, "EZ")) return 0x57B65A;
    if (!strcmp(lv, "HD")) return 0x3B7DD8;
    if (!strcmp(lv, "IN")) return 0xC93B3B;
    if (!strcmp(lv, "AT")) return 0x3A3A3A;
    return C_MUTED;
}

static void cell(Obj* page, float x, float y, float w, float h, int idx, const char* rank, bool phi, int pushRank,
                 bool dimmed) {
    const Chart& c = g_ch[idx];
    Obj* border = image(page, "chart", SPR_ROUND, rgb(phi ? 0xF0C9B8 : C_LINE), Vec2{0.5f, 1}, Vec2{x, y - h / 2},
                        Vec2{w + 4, h + 4}, 16);
    Obj* bg = image(rt(border), "bg", SPR_ROUND, rgb(0xFFFFFF), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{w, h}, 14);
    Obj* r = rt(bg);
    if (dimmed) setGroupAlpha(addCanvasGroup(gameObject(border)), 0.72f);
    // line 1: rank + song name
    text(r, rank, 20, rgb(phi ? C_CLAY : C_MUTED), MiddleLeft, true, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{18, 28},
         Vec2{60, 32});
    text(r, c.name, 24, rgb(C_TEXT), MiddleLeft, true, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{76, 28}, Vec2{w - 76 - 170, 34});
    // line 2: difficulty chip + FC / AP
    char b[48];
    snprintf(b, sizeof b, "%s %.1f", c.level, c.k);
    Obj* chip = image(r, "chip", SPR_ROUND, rgb(diffColor(c.level)), Vec2{0, 0.5f}, Vec2{18 + 52, -2}, Vec2{104, 30}, 9);
    text(rt(chip), b, 17, rgb(0xFFFFFF), MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, 0},
         Vec2{104, 30});
    if (c.s >= 1000000 || c.fc) {
        bool ap = c.s >= 1000000;
        Obj* tag = image(r, "fc", SPR_ROUND, rgb(ap ? 0xE7B24A : 0x5AA9E6), Vec2{0, 0.5f}, Vec2{130 + 24, -2},
                         Vec2{48, 30}, 9);
        text(rt(tag), ap ? "AP" : "FC", 17, rgb(0xFFFFFF), MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f},
             Vec2{0, 0}, Vec2{48, 30});
    }
    // line 3: score + accuracy (cut, not rounded)
    int ac = (int)(c.a * 100 + 0.0001f);
    snprintf(b, sizeof b, "%07d   %d.%02d%%", c.s, ac / 100, ac % 100);
    text(r, b, 19, rgb(C_BODY), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{18, -33}, Vec2{w - 200, 30});
    // right: single RKS, 推分
    snprintf(b, sizeof b, "%.3f", c.rks);
    text(r, b, 32, rgb(C_TEXT), MiddleRight, true, Vec2{1, 0.5f}, Vec2{1, 0.5f}, Vec2{-18, 16}, Vec2{160, 40});
    if (c.s >= 1000000) {
        text(r, "AP", 17, rgb(C_MUTED), MiddleRight, false, Vec2{1, 0.5f}, Vec2{1, 0.5f}, Vec2{-18, -26}, Vec2{170, 26});
    } else {
        float pa = pushRank >= 0 ? pushAcc(idx, pushRank) : 0;
        if (pa > 0) {
            int pc = (int)(pa * 100 + 0.5f);
            snprintf(b, sizeof b, T("推分 %d.%02d%%", "+0.01 at %d.%02d%%"), pc / 100, pc % 100);
            text(r, b, 17, rgb(C_CLAY), MiddleRight, true, Vec2{1, 0.5f}, Vec2{1, 0.5f}, Vec2{-18, -26}, Vec2{190, 26});
        } else {
            text(r, T("推分需 AP", "only an AP helps"), 17, rgb(C_MUTED), MiddleRight, false, Vec2{1, 0.5f},
                 Vec2{1, 0.5f}, Vec2{-18, -26}, Vec2{190, 26});
        }
    }
}

static void section(Obj* page, float pad, float& y, const char* title, const char* note) {
    text(page, title, 25, rgb(C_TEXT), MiddleLeft, true, Vec2{0, 1}, Vec2{0, 0.5f}, Vec2{pad, y - 20}, Vec2{260, 36});
    text(page, note, 19, rgb(C_MUTED), MiddleLeft, false, Vec2{0, 1}, Vec2{0, 0.5f}, Vec2{pad + 150, y - 21},
         Vec2{900, 30});
    y -= 46;
}

static void closePage(void*) {
    Obj* root = (Obj*)g_root.get();
    if (root) uiFadeAway(root, (Obj*)g_card.get());  // the card shrinks a little as it fades
    g_root.clear();
    g_card.clear();
    g_dim.clear();
    g_clawd.clear();
    g_openT = -1;
}

static void b30Frame(void*) {
    if (!g_root.h) return;
    if (!alive((Obj*)g_root.get())) {  // scene changed under us
        closePage(nullptr);
        return;
    }
    float dt = unscaledDt();
    if (g_openT >= 0) {
        g_openT += dt / 0.2f;
        float t = g_openT >= 1 ? 1 : g_openT, u = 1 - t, e = 1 - u * u * u;
        Obj* card = (Obj*)g_card.get();
        if (alive(card)) setScale(card, 0.97f + 0.03f * e);
        Obj* dim = (Obj*)g_dim.get();
        if (alive(dim)) setColor(dim, Color{0.08f, 0.07f, 0.06f, 0.45f * e});
        if (g_openT >= 1) g_openT = -1;
    }
    Obj* clawd = (Obj*)g_clawd.get();
    if (alive(clawd)) {
        g_clawdT += dt;
        setPos(rt(clawd), Vec2{g_cardW / 2 - 140, g_cardH / 2 + 36 + clawdHop(g_clawdT, 14)});
    }
}

void b30Open() {
    closePage(nullptr);
    if (!K.ok || !gather()) {
        saveMessage(T("B30 成绩", "Best 30"), T("读不到存档里的成绩（日志里有详细原因）。", "Could not read the records (see the log)."));
        return;
    }
    onEveryFrame(b30Frame);
    Obj* root = overlayCanvas("PhiMeB30", 32100);
    g_root.set(root);
    Obj* dim = image(root, "dim", SPR_SOLID, Color{0.08f, 0.07f, 0.06f, 0}, Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{100, 100},
                     0, true);
    stretch(rt(dim), -20);
    onClick(gameObject(dim), nullptr, closePage, nullptr);
    g_dim.set(dim);

    Vec2 ss = screenSize();
    float cw = ss.y > 0 ? ss.x * canvasH() / ss.y : 1920;
    g_cardW = cw - 64 < 1760 ? cw - 64 : 1760;
    g_cardH = canvasH() - 96;
    Obj* card = newUI("card", root);
    place(card, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, -18}, Vec2{g_cardW, g_cardH});
    g_card.set(card);
    image(card, "border", SPR_ROUND, rgb(C_LINE), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{g_cardW + 4, g_cardH + 4}, 26);
    image(card, "bg", SPR_ROUND, rgb(C_IVORY), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{g_cardW, g_cardH}, 24, true);
    g_clawd.set(image(card, "clawd", SPR_CLAWD, rgb(0xFFFFFF), Vec2{0.5f, 0.5f}, Vec2{g_cardW / 2 - 140, g_cardH / 2 + 36},
                      Vec2{120, 68}));
    g_clawdT = 0;

    // header: title, RKS (ours + the game's), 完成
    const float pad = 44, top = g_cardH / 2;
    text(card, T("B30 成绩", "Best 30"), 34, rgb(C_TEXT), MiddleLeft, true, Vec2{0, 0.5f}, Vec2{0, 0.5f},
         Vec2{pad, top - 50}, Vec2{400, 48});
    text(card, T("Best 27 + Phi 3，按本机存档计算（云端的成绩请先同步）", "Best 27 + Phi 3 from this device's save (sync first for cloud records)"), 19,
         rgb(C_MUTED), MiddleLeft, false, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{pad, top - 88}, Vec2{900, 30});
    Obj* done = image(card, "done", SPR_ROUND, rgb(C_CLAY), Vec2{1, 0.5f}, Vec2{-pad - 70, top - 64}, Vec2{140, 56}, 14,
                      true);
    text(rt(done), T("完成", "Done"), 23, rgb(0xFFFFFF), MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f},
         Vec2{0, 1}, Vec2{130, 50});
    onClick(gameObject(done), done, closePage, nullptr);
    char b[64];
    int r3 = (int)(g_rks * 1000 + 0.0001f);
    snprintf(b, sizeof b, "%d.%03d", r3 / 1000, r3 % 1000);
    text(card, b, 46, rgb(C_CLAY), MiddleRight, true, Vec2{1, 0.5f}, Vec2{1, 0.5f}, Vec2{-pad - 170, top - 52},
         Vec2{240, 56});
    text(card, "RKS", 19, rgb(C_MUTED), MiddleRight, true, Vec2{1, 0.5f}, Vec2{1, 0.5f}, Vec2{-pad - 418, top - 46},
         Vec2{80, 30});
    if (g_gameRks >= 0) {
        int g3 = (int)(g_gameRks * 1000 + 0.0001f);
        snprintf(b, sizeof b, T("游戏内 %d.%03d", "in game %d.%03d"), g3 / 1000, g3 % 1000);
        text(card, b, 18, rgb(C_MUTED), MiddleRight, false, Vec2{1, 0.5f}, Vec2{1, 0.5f}, Vec2{-pad - 170, top - 92},
             Vec2{240, 28});
    }
    image(card, "divider", SPR_SOLID, rgb(C_LINE), Vec2{0.5f, 0.5f}, Vec2{0, top - 116}, Vec2{g_cardW - 2 * pad, 2});

    // the grid scrolls under the header
    float vpTop = top - 120, vpBot = -top + 16;
    Obj* content = scrollArea(card, "grid", Vec2{0, (vpTop + vpBot) / 2}, Vec2{g_cardW, vpTop - vpBot}, 0);
    Obj* page = newUI("page", content);
    place(page, Vec2{0.5f, 1}, Vec2{0.5f, 1}, Vec2{0, 0}, Vec2{g_cardW, 10});
    const float gap = 18, cw3 = (g_cardW - 2 * pad - 2 * gap) / 3, ch = 112, step = ch + 16;
    float y = -14;
    auto colX = [&](int c) { return -g_cardW / 2 + pad + cw3 / 2 + c * (cw3 + gap); };
    char rank[8];
    section(page, pad, y, "Phi 3", T("AP 的谱面里定数最高的 3 张，每张按定数计入", "The 3 hardest APs, each counts its constant"));
    if (!g_nPhi) {
        text(page, T("还没有 AP 的谱面。", "No AP yet."), 21, rgb(C_MUTED), MiddleLeft, false, Vec2{0, 1}, Vec2{0, 0.5f},
             Vec2{pad, y - 24}, Vec2{600, 32});
        y -= 56;
    } else {
        for (int i = 0; i < g_nPhi; i++) {
            snprintf(rank, sizeof rank, "P%d", i + 1);
            int br = -1;  // its rank among the best, for 推分 (an AP needs none)
            cell(page, colX(i), y, cw3, ch, g_phi[i], rank, true, br, false);
        }
        y -= step;
    }
    y -= 10;
    section(page, pad, y, "Best 27", T("单曲 RKS 最高的 27 张；推分 = 打到这个准度，RKS 两位小数 +0.01", "The 27 best single RKS; +0.01 = the accuracy that lifts RKS by 0.01"));
    int nb = g_nBest < 27 ? g_nBest : 27;
    for (int i = 0; i < nb; i++) {
        snprintf(rank, sizeof rank, "#%d", i + 1);
        cell(page, colX(i % 3), y - (i / 3) * step, cw3, ch, g_best[i], rank, false, i, false);
    }
    y -= ((nb + 2) / 3) * step;
    if (g_nBest > 27) {
        y -= 10;
        section(page, pad, y, T("候补", "Next up"), T("差一点进 B27 的谱面", "Just outside the best 27"));
        int extra = g_nBest - 27 < 6 ? g_nBest - 27 : 6;
        for (int i = 0; i < extra; i++) {
            snprintf(rank, sizeof rank, "#%d", 28 + i);
            cell(page, colX(i % 3), y - (i / 3) * step, cw3, ch, g_best[27 + i], rank, false, 27 + i, true);
        }
        y -= ((extra + 2) / 3) * step;
    }
    scrollAreaSetHeight(content, -y + 20);
    setScale(card, 0.97f);
    g_openT = 0;
    LOG("b30: page opened");
}

bool b30Install() {
    K.GI = findClass("", "GameInformation");
    Il2CppClass* SongBase = findClass("", "SongBase");
    Il2CppClass* Song = findClass("", "SongsItem");
    Il2CppClass* Rec = findClass("", "LevelRecord");
    if (!K.GI || !SongBase || !Song || !Rec) return false;
    K.allSongs = findMethod(SongBase, "get_AllSongsBypassLimitedAndOthers", 0);
    if (!K.allSongs) K.allSongs = findMethod(SongBase, "get_AllSongs", 0);
    K.getRec = findMethod(K.GI, "GetSongRecord", 2);
    K.getRks = findMethod(K.GI, "get_RankingScore", 0);
    K.giSong = fieldOffset(K.GI, "song");
    K.songId = fieldOffset(Song, "songsId");
    K.songName = fieldOffset(Song, "songsName");
    K.songLevels = fieldOffset(Song, "levels");
    K.songDiff = fieldOffset(Song, "difficulty");
    K.recS = fieldOffset(Rec, "s");
    K.recA = fieldOffset(Rec, "a");
    K.recC = fieldOffset(Rec, "c");
    FieldInfo* c = il2cpp_class_get_field_from_name(Rec, "c");
    K.recCBool = c && il2cpp_type_get_type(il2cpp_field_get_type(c)) == IL2CPP_TYPE_BOOLEAN;
    K.ok = K.allSongs && K.getRec && K.giSong >= 0 && K.songId >= 0 && K.songLevels >= 0 && K.songDiff >= 0 &&
           K.recS >= 0 && K.recA >= 0 && K.recC >= 0;
    LOG("b30: %s", K.ok ? "on" : "off (names missing)");
    return K.ok;
}
