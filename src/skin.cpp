// Phira-format skins (resource packs): note sprites and hit effects. Hit sounds are not replaced yet.
//
// The pack (a .zip) is checked by skinpack.cpp and kept as <mod folder>/skin.zip. Its PNGs become textures once
// (ImageConversion.LoadImage); sprites are cut from them with the same world size as the game's own:
//   - notes: the game's sprites are learned from the notes themselves (ClickControl / DragControl / FlickControl
//     ._spriteRenderer, HoldControl head / body / end renderers) and from the judge lines' multi-hit (HL) sprite
//     fields. Each original sprite maps to a skin sprite with pixelsPerUnit = skin width x original ppu / original
//     width (same width on screen, the skin's own aspect). A hold body is a centre slice of the atlas' middle part,
//     as tall as the game's body sprite (the game stretches it to the hold's length).
//   - hit effects: a clone of ScoreControl.perfectJudge / goodJudge (critical.cpp's Instantiate hook calls
//     skinOnClone) gets its sprite renderers switched off and a frame animation of hit_fx.png on top, tinted with
//     colorPerfect / colorGood, as wide as the game's effect sprite x hitFxScale.
// While a skin is on, Critical Perfect is off (skins have no gold / orange variants).
#include "mod.h"
#include "skinpack.h"

using namespace il2cpp;
using namespace unity;

static struct {
    bool ok;
    Il2CppClass *Texture2D, *Byte, *SR, *Renderer, *HoldC, *ClickC, *DragC, *FlickC;
    const MethodInfo *texCtor, *loadImage, *texWrap, *spriteCreate, *spRect, *spPPU, *spPivot;
    const MethodInfo *srGetSprite, *srSetSprite, *srSetColor, *rEnabled, *rGetOrder, *rSetOrder, *rGetLayer,
        *rSetLayer, *lossy, *hideFlags, *goGetLayer, *goSetLayer, *rGetShared, *rSetShared, *rGetMat, *matHas, *matSetColor, *srDrawMode, *srSize, *trGetLocal, *trSetLocal, *getPixels, *setPixels, *texApply, *imgSprite, *imgGetSprite;
    Il2CppClass* Image;
    int setNote;
    Il2CppClass* Color32;
    FieldInfo* giMain;
    int giNoteScale;
    int pcJc, pcScore, jcNum, jcLines, numLists[4];
    int ccSR, dcSR, fcSR, hHead, hBody, hEnd;
    int scPerfect, scGood;
    int jlHL[6];  // ClickHL, DragHL, FlickHL, HoldHL0, HoldHL1, (unused)
    int listItems = -1, listSize = -1;
} S;

// ---- the loaded pack ----
static bool g_loaded = false;
static char g_name[128], g_author[128];
static SkinPack g_info;  // values only (its blobs are freed after the textures are made)
static Handle g_tex[SK_FILE_COUNT];
static Handle g_frames[256];
static int g_frameN = 0;
// the note sprite map (game sprite -> skin sprite) and the skin's pixels per unit per note kind (from its plain note)
struct MapE {
    Handle from, to;
};
static MapE g_map[24];
static int g_mapN = 0;
static float g_ppu[6];
// stand-ins made so far, by what they depend on (see standIn)
struct StandIn {
    int role;
    bool mh;
    float w, h, ppu, px, py;
    Handle s;
};
static StandIn g_si[32];
static int g_siN = 0;
static Handle g_bodyTex[2];  // the hold bodies (normal, multi-hit), resampled from the atlas' middle
static int g_bodyRows[2];
static int g_loadErr = 0;  // the last load's SkinError (0 = fine)

static int g_loadedIdEarly();
bool skinActive() { return g_cfg.skin && g_loaded && g_loadedIdEarly() == g_cfg.skinId; }
const char* skinName() { return g_loaded ? g_name : ""; }

// ---- small helpers ----
static int listView(Obj* list, Obj*** out) {
    if (!list) return 0;
    if (S.listItems < 0) {
        Il2CppClass* k = il2cpp_object_get_class(list);
        S.listItems = fieldOffset(k, "_items");
        S.listSize = fieldOffset(k, "_size");
        if (S.listItems < 0 || S.listSize < 0) return 0;
    }
    Il2CppArray* items = fld<Il2CppArray*>(list, S.listItems);
    int n = fld<int>(list, S.listSize);
    if (!items || n <= 0 || (uintptr_t)n > items->max_length) return 0;
    *out = (Obj**)arrayData(items);
    return n;
}
// ARGB; a colour written as 6 hex digits (0xRRGGBB) has no alpha byte: opaque, not invisible
static Color argb(uint32_t c) {
    uint32_t a = (c >> 24) & 255;
    return Color{((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, (a ? a : 255) / 255.f};
}
static bool readAll(const char* path, uint8_t** out, uint32_t* n) {
    *out = nullptr;
    *n = 0;
    int fd = open(path, 0);
    if (fd < 0) return false;
    long len = lseek(fd, 0, 2);
    if (len <= 0 || len > 64L * 1024 * 1024) {
        close(fd);
        return false;
    }
    uint8_t* b = (uint8_t*)malloc((size_t)len);
    bool ok = b && pread(fd, b, (size_t)len, 0) == len;
    close(fd);
    if (!ok) {
        free(b);
        return false;
    }
    *out = b;
    *n = (uint32_t)len;
    return true;
}
static bool writeAll(const char* path, const uint8_t* b, uint32_t n) {
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int fd = open(tmp, PM_O_REPLACE, 0644);
    if (fd < 0) return false;
    bool ok = write(fd, b, n) == (ssize_t)n;
    close(fd);
    return ok && rename(tmp, path) == 0;
}
// ---- the library: <mod folder>/skins/<id>.zip + index.txt ("id\tname\tauthor" per line) ----
extern "C" int unlink(const char* path);
struct LibEntry {
    int id;
    char name[128], author[128];
};
static const int kLibMax = 16;
static LibEntry g_lib[kLibMax];
static int g_libN = 0, g_loadedId = 0, g_failedId = 0;
static bool g_libRead = false;
static int g_loadedIdEarly() { return g_loadedId; }
static void libDir(char* out, int cap) {
    snprintf(out, cap, "%s/skins", modFolderPath());
    mkdir(out, 0770);
}
static void libFile(int id, char* out, int cap) {
    char d[500];
    libDir(d, sizeof d);
    snprintf(out, cap, "%s/%d.zip", d, id);
}
static void libWrite() {
    char d[500], p[600];
    libDir(d, sizeof d);
    snprintf(p, sizeof p, "%s/index.txt", d);
    static char buf[kLibMax * 280];
    int len = 0;
    for (int i = 0; i < g_libN && len < (int)sizeof buf - 280; i++)
        len += snprintf(buf + len, sizeof buf - len, "%d\t%s\t%s\n", g_lib[i].id, g_lib[i].name, g_lib[i].author);
    writeAll(p, (const uint8_t*)buf, (uint32_t)len);
}
static void cleanCopy(char* dst, int cap, const char* s) {  // no tabs / line breaks in the index
    snprintf(dst, cap, "%s", s);
    for (char* c = dst; *c; c++)
        if (*c == '\t' || *c == '\n' || *c == '\r') *c = ' ';
}
static int libAdd(const uint8_t* zip, uint32_t n, const char* name, const char* author) {
    if (g_libN >= kLibMax) return 0;
    int id = 1;
    for (int i = 0; i < g_libN; i++)
        if (g_lib[i].id >= id) id = g_lib[i].id + 1;
    char p[600];
    libFile(id, p, sizeof p);
    if (!writeAll(p, zip, n)) return 0;
    LibEntry& e = g_lib[g_libN++];
    e.id = id;
    cleanCopy(e.name, sizeof e.name, name && name[0] ? name : "Phira skin");
    cleanCopy(e.author, sizeof e.author, author ? author : "");
    libWrite();
    return id;
}
static void libRead() {
    if (g_libRead) return;
    g_libRead = true;
    char d[500], p[600];
    libDir(d, sizeof d);
    snprintf(p, sizeof p, "%s/index.txt", d);
    uint8_t* b;
    uint32_t n;
    if (readAll(p, &b, &n)) {
        char* s = (char*)b;
        uint32_t i = 0;
        while (i < n && g_libN < kLibMax) {
            char* line = s + i;
            uint32_t j = i;
            while (j < n && s[j] != '\n') j++;
            s[j < n ? j : n - 1] = 0;  // (a last line without \n loses its last byte only if unterminated)
            char* f[3] = {line, nullptr, nullptr};
            int k = 1;
            for (char* c = line; *c && k < 3; c++)
                if (*c == '\t') {
                    *c = 0;
                    f[k++] = c + 1;
                }
            int id = 0;
            for (char* c = f[0]; *c >= '0' && *c <= '9'; c++) id = id * 10 + (*c - '0');
            if (id > 0 && f[1]) {
                LibEntry& e = g_lib[g_libN++];
                e.id = id;
                snprintf(e.name, sizeof e.name, "%s", f[1]);
                snprintf(e.author, sizeof e.author, "%s", f[2] ? f[2] : "");
            }
            i = j + 1;
        }
        free(b);
    }
    // 0.26.x kept one pack as <mod folder>/skin.zip: move it into the library
    char old[600];
    snprintf(old, sizeof old, "%s/skin.zip", modFolderPath());
    if (readAll(old, &b, &n)) {
        SkinPack pk;
        SkinResult res;
        if (skinPackLoad(b, n, &pk, &res)) {
            int id = libAdd(b, n, pk.name, pk.author);
            if (id && g_cfg.skinId == 0) g_cfg.skinId = id;
            skinPackFree(&pk);
            LOG("skin: old skin.zip moved into the library as %d", id);
        }
        free(b);
        unlink(old);
    }
    LOG("skin: library has %d pack(s), current %d (%s)", g_libN, g_cfg.skinId, g_cfg.skin ? "on" : "off");
}

// ours, not referenced by any scene: without this, a scene change's UnloadUnusedAssets may destroy it
static Obj* keep(Obj* o) {
    if (o && S.hideFlags) callm<void>(S.hideFlags, o, 32);  // HideFlags.DontUnloadUnusedAsset
    return o;
}
static Obj* makeTexture(const SkinBlob& b) {
    if (!b.data || !b.size) return nullptr;
    Obj* tex = il2cpp_object_new(S.Texture2D);
    callm<void>(S.texCtor, tex, 2, 2);
    Il2CppArray* arr = newArray(S.Byte, b.size);
    if (!arr) return nullptr;
    memcpy(arrayData(arr), b.data, b.size);
    bool readable = false;
    void* args[] = {tex, arr, &readable};
    bool ok;
    Obj* r = invokeObj(S.loadImage, nullptr, args, &ok);
    if (!ok || !r || !*(bool*)((char*)r + sizeof(Il2CppObject))) return nullptr;  // boxed bool
    if (S.texWrap) callm<void>(S.texWrap, tex, 1);  // clamp: no bleeding at the edges
    keep(tex);
    return tex;
}

// Loads <mod folder>/skin.zip (or the bytes given) into textures. Main thread.
static bool loadPack(const uint8_t* zip, uint32_t n, SkinResult* res) {
    SkinPack p;
    if (!skinPackLoad(zip, n, &p, res)) return false;
    Obj* tex[SK_FILE_COUNT] = {};
    bool ok = true;
    for (int i = 0; i < SK_SND_CLICK && ok; i++) {
        if (!p.files[i].data) continue;
        tex[i] = makeTexture(p.files[i]);
        if (!tex[i]) {
            res->err = SKE_BAD_IMAGE;
            snprintf(res->detail, sizeof res->detail, "image %d could not be decoded", i);
            ok = false;
        }
    }
    if (ok) {
        for (int i = 0; i < SK_FILE_COUNT; i++) g_tex[i].set(i < SK_SND_CLICK ? tex[i] : nullptr);
        g_info = p;
        for (int i = 0; i < SK_FILE_COUNT; i++) g_info.files[i].data = nullptr;  // freed below
        snprintf(g_name, sizeof g_name, "%s", p.name[0] ? p.name : "Phira skin");
        snprintf(g_author, sizeof g_author, "%s", p.author);
        // hit effect frames: cols x rows, left to right, top to bottom
        for (int i = 0; i < g_frameN; i++) g_frames[i].clear();
        g_frameN = 0;
        const SkinBlob& fx = p.files[SK_HIT_FX];
        int cols = p.hitFxCols, rows = p.hitFxRows;
        float fw = (float)fx.w / cols, fh = (float)fx.h / rows;
        for (int r = 0; r < rows && g_frameN < 256; r++)
            for (int c = 0; c < cols && g_frameN < 256; c++) {
                Rect rc{c * fw, fx.h - (r + 1) * fh, fw, fh};
                Obj* s = callm<Obj*>(S.spriteCreate, tex[SK_HIT_FX], rc, Vec2{0.5f, 0.5f}, 100.f, 0u, 0, Vec4{0, 0, 0, 0});
                g_frames[g_frameN++].set(keep(s));
            }
        for (int i = 0; i < 2; i++) g_bodyTex[i].clear();
        for (int i = 0; i < g_mapN; i++) g_map[i].from.clear(), g_map[i].to.clear();
        g_mapN = 0;  // a new pack: new sprites
        for (int i = 0; i < g_siN; i++) g_si[i].s.clear();
        g_siN = 0;
        for (float& p : g_ppu) p = 0;
        g_loaded = true;
        LOG("skin: '%s' by '%s' loaded, hit fx %dx%d (%d frames, %.2f s, scale %.2f), hold atlas %d/%d, mh %d/%d",
            g_name, g_author, cols, rows, g_frameN, p.hitFxDuration, p.hitFxScale, p.holdTop, p.holdBottom,
            p.holdTopMH, p.holdBottomMH);
    }
    if (ok) skinSoundsLoad(p);  // decoded while the pack's files are still here
    skinPackFree(&p);
    return ok;
}

static void ensureLoaded() {
    if (!S.ok) return;
    libRead();
    int want = g_cfg.skin ? g_cfg.skinId : 0;
    if (!want || (g_loaded && g_loadedId == want) || g_failedId == want) return;
    char path[600];
    libFile(want, path, sizeof path);
    uint8_t* b;
    uint32_t n;
    SkinResult res;
    res.err = SKE_NOT_ZIP;
    snprintf(res.detail, sizeof res.detail, "file missing");
    bool ok = readAll(path, &b, &n);
    if (ok) {
        ok = loadPack(b, n, &res);
        free(b);
    }
    if (ok) {
        g_loadedId = want;
        g_loadErr = 0;
    } else {
        g_failedId = want;
        g_loadErr = res.err ? res.err : 1;
        LOG("skin: %s failed: %s %s", path, skinErrorName(res.err), res.detail);
    }
}

// ---- note sprites ----
enum { R_CLICK, R_DRAG, R_FLICK, R_HEAD, R_BODY, R_END };
static Obj* mapped(Obj* s) {
    for (int i = 0; i < g_mapN; i++) {
        if (g_map[i].from.get() == s) return (Obj*)g_map[i].to.get();
        if (g_map[i].to.get() == s) return nullptr;  // already ours
    }
    return nullptr;
}
static bool known(Obj* s) {
    for (int i = 0; i < g_mapN; i++)
        if (g_map[i].from.get() == s || g_map[i].to.get() == s) return true;
    return false;
}

// The hold atlas' middle part resampled to `rows` rows (box filter: every source row counts), as its own texture
struct Px {
    uint8_t r, g, b, a;
};
static Obj* bodyTexture(int ti, int rows, int bot, int top) {
    int slot = ti == SK_HOLD_MH;
    if (alive((Obj*)g_bodyTex[slot].get()) && g_bodyRows[slot] == rows) return (Obj*)g_bodyTex[slot].get();
    Obj* src = (Obj*)g_tex[ti].get();
    int W = g_info.files[ti].w, H = g_info.files[ti].h, y0 = bot, mid = H - top - bot;
    if (!alive(src) || !S.getPixels || !S.setPixels || !S.texApply || !S.Color32 || mid < 1 || W < 1) return nullptr;
    if (rows < 1) rows = 1;
    if (rows > 4096) rows = 4096;
    Il2CppArray* in = callm<Il2CppArray*>(S.getPixels, src);  // bottom row first
    if (!in || in->max_length < (uintptr_t)(W * H)) return nullptr;
    const Px* p = (const Px*)arrayData(in);
    Il2CppArray* out = newArray(S.Color32, (uintptr_t)W * rows);
    if (!out) return nullptr;
    Px* q = (Px*)arrayData(out);
    for (int j = 0; j < rows; j++) {
        int a = y0 + (int)((long)j * mid / rows), b = y0 + (int)((long)(j + 1) * mid / rows);
        if (b <= a) b = a + 1;
        int n = b - a;
        for (int x = 0; x < W; x++) {
            unsigned sr = 0, sg = 0, sb = 0, sa = 0;
            for (int y = a; y < b; y++) {
                const Px& c = p[y * W + x];
                sr += c.r, sg += c.g, sb += c.b, sa += c.a;
            }
            q[j * W + x] = Px{(uint8_t)(sr / n), (uint8_t)(sg / n), (uint8_t)(sb / n), (uint8_t)(sa / n)};
        }
    }
    Obj* tex = il2cpp_object_new(S.Texture2D);
    callm<void>(S.texCtor, tex, W, rows);
    if (S.texWrap) callm<void>(S.texWrap, tex, 1);
    callm<void>(S.setPixels, tex, out);
    callm<void>(S.texApply, tex);
    keep(tex);
    g_bodyTex[slot].set(tex);
    g_bodyRows[slot] = rows;
    LOG("skin: hold%s body: middle %d rows -> %d rows (%dx%d)", slot ? " (multi-hit)" : "", mid, rows, W, rows);
    return tex;
}

// a skin sprite standing in for `orig`: same width on screen, same pivot
static bool g_deferred = false;
// Stand-ins made so far, by what they depend on: the game loads its note sprites again for every song (new objects,
// same look), so a new level reuses these instead of making more
static Obj* makeStandIn(Obj* orig, int role, bool mh);
static Obj* standIn(Obj* orig, int role, bool mh) {
    if (!alive(orig)) return nullptr;
    Rect o = callm<Rect>(S.spRect, orig);
    float ppu = callm<float>(S.spPPU, orig);
    Vec2 pv = callm<Vec2>(S.spPivot, orig);
    for (int i = 0; i < g_siN; i++) {
        StandIn& c = g_si[i];
        if (c.role == role && c.mh == mh && c.w == o.w && c.h == o.h && c.ppu == ppu && c.px == pv.x && c.py == pv.y &&
            alive((Obj*)c.s.get()))
            return (Obj*)c.s.get();
    }
    Obj* s = makeStandIn(orig, role, mh);
    if (s && g_siN < 32) g_si[g_siN++] = StandIn{role, mh, o.w, o.h, ppu, pv.x, pv.y, {}}, g_si[g_siN - 1].s.set(s);
    return s;
}
static Obj* makeStandIn(Obj* orig, int role, bool mh) {
    bool mhOrig = mh;  // orig is the game's multi-hit sprite (even when the skin has no _mh image)
    static const int texOf[2][6] = {{SK_CLICK, SK_DRAG, SK_FLICK, SK_HOLD, SK_HOLD, SK_HOLD},
                                    {SK_CLICK_MH, SK_DRAG_MH, SK_FLICK_MH, SK_HOLD_MH, SK_HOLD_MH, SK_HOLD_MH}};
    int ti = texOf[mh ? 1 : 0][role];
    Obj* tex = (Obj*)g_tex[ti].get();
    if (!alive(tex) && mh) {  // no _mh image: the normal one
        ti = texOf[0][role];
        tex = (Obj*)g_tex[ti].get();
        mh = false;
    }
    if (!alive(tex) || !alive(orig)) return nullptr;
    Rect o = callm<Rect>(S.spRect, orig);
    float ppu = callm<float>(S.spPPU, orig);
    Vec2 pv = callm<Vec2>(S.spPivot, orig);  // pixels
    if (o.w < 1 || o.h < 1 || ppu <= 0) return nullptr;
    float W = (float)g_info.files[ti].w, H = (float)g_info.files[ti].h;  // hold.png: tail on top, head at the bottom
    // Phira draws a multi-hit image at the plain one's scale (its extra width is the glow): one pixels-per-unit per
    // note kind, learned from the plain note (0.28.5 fitted _mh to the game's narrower HL sprite: notes shrank)
    float nppu;
    if (!mhOrig) {
        nppu = W * ppu / o.w;
        g_ppu[role] = nppu;
    } else if (g_ppu[role] > 0) {
        nppu = g_ppu[role];
    } else {
        g_deferred = true;  // the plain note of this kind is not known yet: next scan
        return nullptr;
    }
    Rect r{0, 0, W, H};
    if (role == R_HEAD || role == R_BODY || role == R_END) {
        bool m = ti == SK_HOLD_MH;
        float top = (float)(m ? g_info.holdTopMH : g_info.holdTop), bot = (float)(m ? g_info.holdBottomMH : g_info.holdBottom);
        if (role == R_END) r = Rect{0, H - top, W, top};
        else if (role == R_HEAD) r = Rect{0, 0, W, bot};
        else {
            // the whole middle part (Phira stretches all of it), squeezed into the game's body aspect: the game then
            // stretches it back over the hold's length (0.28.2 took a centre slice: a gradient body lost its top)
            float want = o.h / ppu * nppu;  // as tall (world) as the sprite it replaces
            Obj* bt = bodyTexture(ti, (int)(want + 0.5f), (int)bot, (int)top);
            if (bt) {
                tex = bt;
                r = Rect{0, 0, W, (float)g_bodyRows[ti == SK_HOLD_MH]};
            } else {
                float mid = H - top - bot;
                if (want < 1) want = 1;
                if (want > mid) want = mid;
                r = Rect{0, bot + (mid - want) / 2, W, want};
            }
        }
        if (r.h < 1) return nullptr;
    }
    Vec2 pivot{pv.x / o.w, pv.y / o.h};
    return keep(callm<Obj*>(S.spriteCreate, tex, r, pivot, nppu, 0u, 0, Vec4{0, 0, 0, 0}));
}
static void learn(Obj* orig, int role, bool mh) {
    if (!orig || known(orig) || g_mapN >= 24) return;
    g_deferred = false;
    Obj* s = standIn(orig, role, mh);
    if (!s && g_deferred) return;
    if (!s) {
        static int failed = 0;
        if (failed++ < 6) {
            Rect o = callm<Rect>(S.spRect, orig);
            LOG("skin: no stand-in for role %d%s: original %.0fx%.0f ppu %.1f, skin texture %s", role, mh ? " mh" : "",
                o.w, o.h, callm<float>(S.spPPU, orig), alive((Obj*)g_tex[SK_CLICK].get()) ? "alive" : "GONE");
        }
        return;
    }
    g_map[g_mapN].from.set(orig);
    g_map[g_mapN].to.set(s);
    g_mapN++;
    char n[64] = "?";
    objName(orig, n, sizeof n);
    static const char* const roles[] = {"tap", "drag", "flick", "hold head", "hold body", "hold end"};
    LOG("skin: %s%s sprite '%s' -> skin", roles[role], mh ? " (multi-hit)" : "", n);
}

// the game's multi-hit sprites of this level (read from the judge lines before they get ours)
static Handle g_hl[5];
static bool isHL(Obj* s) {
    for (Handle& h : g_hl)
        if (h.h && h.get() == s) return true;
    return false;
}

// The game keeps each note's sprites in its own fields (ClickControl.noteImage, HoldControl.noteImages, ...) and puts
// them on the renderer when the note comes onto the screen: those fields get the skin's sprites, so a note never
// shows the game's look first (0.28.6 only swapped renderers, once a second: notes appeared in the game's skin)
struct SpriteFields {
    Il2CppClass* k;
    int off[8];
    bool arr[8];
    int n;
};
static SpriteFields g_sf[8];
static int g_sfN = 0;
static Il2CppClass* g_Sprite = nullptr;
static const SpriteFields* spriteFields(Il2CppClass* k) {
    for (int i = 0; i < g_sfN; i++)
        if (g_sf[i].k == k) return &g_sf[i];
    if (g_sfN >= 8 || !g_Sprite) return nullptr;
    SpriteFields& f = g_sf[g_sfN++];
    f.k = k;
    f.n = 0;
    char names[200] = "";
    for (Il2CppClass* c = k; c && f.n < 8; c = il2cpp_class_get_parent(c)) {
        void* it = nullptr;
        while (FieldInfo* fi = il2cpp_class_get_fields(c, &it)) {
            if (f.n >= 8) break;
            if (il2cpp_field_get_flags(fi) & 0x10) continue;  // static
            const Il2CppType* t = il2cpp_field_get_type(fi);
            int tt = il2cpp_type_get_type(t);
            Il2CppClass* fk = il2cpp_class_from_type(t);
            bool one = tt == IL2CPP_TYPE_CLASS && fk == g_Sprite;
            bool many = tt == IL2CPP_TYPE_SZARRAY && fk && !strcmp(il2cpp_class_get_name(fk), "Sprite[]");
            if (!one && !many) continue;
            f.off[f.n] = (int)il2cpp_field_get_offset(fi);
            f.arr[f.n] = many;
            f.n++;
            size_t l = strlen(names);
            snprintf(names + l, sizeof names - l, "%s%s%s", l ? ", " : "", il2cpp_field_get_name(fi), many ? "[]" : "");
        }
    }
    LOG("skin: %s keeps its sprites in %s", il2cpp_class_get_name(k), f.n ? names : "no fields");
    return &f;
}
static int holdRole(Obj* s) {  // a hold sprite's part, by its name (Hold_Head / Hold / Hold_End, Hold2HL_0 / _1)
    char n[64] = "";
    objName(s, n, sizeof n);
    size_t l = strlen(n);
    for (char* c = n; *c; c++) {
        if ((c[0] == 'H' || c[0] == 'h') && !memcmp(c + 1, "ead", 3)) return R_HEAD;
        if ((c[0] == 'E' || c[0] == 'e') && c[1] == 'n' && c[2] == 'd') return R_END;
    }
    if (l >= 2 && n[l - 2] == '_' && n[l - 1] == '0') return R_HEAD;
    return R_BODY;
}
static void fixSlot(Obj** slot, int kind) {  // kind: 0 tap, 1 drag, 2 flick, 3 hold
    Obj* s = *slot;
    if (!s) return;
    if (!known(s)) learn(s, kind < 3 ? kind : holdRole(s), isHL(s));
    Obj* to = mapped(s);
    if (to) *slot = to;
}
static void fixFields(Obj* note, int kind) {
    const SpriteFields* f = spriteFields(il2cpp_object_get_class(note));
    for (int i = 0; f && i < f->n; i++) {
        if (!f->arr[i]) {
            fixSlot(&fld<Obj*>(note, f->off[i]), kind);
            continue;
        }
        Il2CppArray* a = fld<Il2CppArray*>(note, f->off[i]);
        for (uintptr_t j = 0; a && j < a->max_length && j < 16; j++) fixSlot(&((Obj**)arrayData(a))[j], kind);
    }
}

static void swapRenderer(Obj* sr, int role) {
    if (!alive(sr)) return;
    Obj* s = callm<Obj*>(S.srGetSprite, sr);
    if (!s) return;
    if (!known(s)) learn(s, role, isHL(s));
    Obj* to = mapped(s);
    if (to) callm<void>(S.srSetSprite, sr, to);
}

static int g_lastCount = -1, g_diag = 0;  // g_diag: note diagnostics logged this level
static float g_nextScan = 0;
static Handle g_pc;
static void scanNotes(bool force) {
    Obj* pc = (Obj*)g_pc.get();
    Obj* jc = alive(pc) ? fld<Obj*>(pc, S.pcJc) : nullptr;
    Obj* num = alive(jc) ? fld<Obj*>(jc, S.jcNum) : nullptr;
    if (!alive(num)) return;
    Obj** e[4];
    int n[4], total = 0;
    for (int i = 0; i < 4; i++) {
        n[i] = listView(fld<Obj*>(num, S.numLists[i]), &e[i]);
        total += n[i];
    }
    if (!force && total == g_lastCount) return;
    g_lastCount = total;
    // the game's multi-hit sprites (judge line fields), remembered before the lines get ours
    Obj** lines = nullptr;
    int nl = listView(fld<Obj*>(jc, S.jcLines), &lines);
    static const int hlRoles[5] = {R_CLICK, R_DRAG, R_FLICK, R_HEAD, R_BODY};
    if (nl > 0 && alive(lines[0]))
        for (int k = 0; k < 5; k++) {
            Obj* o = S.jlHL[k] >= 0 ? fld<Obj*>(lines[0], S.jlHL[k]) : nullptr;
            if (!g_hl[k].h && o && !strcmp(il2cpp_class_get_name(il2cpp_object_get_class(o)), "Sprite") && !mapped(o) &&
                !known(o))
                g_hl[k].set(o);
        }
    // plain notes first (they set each kind's scale), then the multi-hit sprites, then once more for those
    for (int pass = 0; pass < 2; pass++) {
    int before = g_mapN;    const int srOff[3] = {S.ccSR, S.dcSR, S.fcSR};
    const int lists[3] = {0, 1, 3};  // click, drag, flick lists (2 = holds)
    for (int t = 0; t < 3; t++)
        for (int i = 0; i < n[lists[t]]; i++) {
            Obj* note = e[lists[t]][i];
            if (!alive(note)) continue;
            Obj* sr = srOff[t] >= 0 ? fld<Obj*>(note, srOff[t]) : nullptr;
            if (!alive(sr)) sr = getComponent(gameObject(note), S.SR);  // field not set yet: the renderer itself
            if (!alive(sr)) {
                Il2CppArray* rs = componentsInChildren(gameObject(note), S.SR);
                sr = rs && rs->max_length ? (Obj*)arrayData(rs)[0] : nullptr;
            }
            if (g_diag < 3 && i == 0 && alive(sr)) {
                g_diag++;
                Obj* s = alive(sr) ? callm<Obj*>(S.srGetSprite, sr) : nullptr;
                char sn[64] = "-";
                if (s) objName(s, sn, sizeof sn);
                float ns = -1;  // the game's 按键大小 (GameInformation.noteScale) and what it does to a note
                if (S.giNoteScale >= 0 && S.giMain) {
                    Obj* gi = nullptr;
                    il2cpp_field_static_get_value(S.giMain, &gi);
                    if (gi) ns = fld<float>(gi, S.giNoteScale);
                }
                Vec3 ls = alive(sr) ? callm<Vec3>(S.lossy, goTransform(gameObject(sr))) : Vec3{0, 0, 0};
                int dm = alive(sr) && S.srDrawMode ? callm<int>(S.srDrawMode, sr) : -1;
                Vec2 sz = alive(sr) && S.srSize ? callm<Vec2>(S.srSize, sr) : Vec2{0, 0};
                LOG("skin: %s note: renderer field %s, renderer %s, sprite '%s', noteScale %.3f, renderer scale %.3f x %.3f, "
                    "drawMode %d size %.3f x %.3f",
                    t == 0 ? "tap" : t == 1 ? "drag" : "flick", srOff[t] >= 0 && fld<Obj*>(note, srOff[t]) ? "set" : "EMPTY",
                    alive(sr) ? "found" : "MISSING", sn, ns, ls.x, ls.y, dm, sz.x, sz.y);
            }
            swapRenderer(sr, t);
            fixFields(note, t);
        }
    for (int i = 0; i < n[2]; i++) {
        Obj* h = e[2][i];
        if (!alive(h)) continue;
        Obj* part[3] = {S.hHead >= 0 ? fld<Obj*>(h, S.hHead) : nullptr, S.hBody >= 0 ? fld<Obj*>(h, S.hBody) : nullptr,
                        S.hEnd >= 0 ? fld<Obj*>(h, S.hEnd) : nullptr};
        if (!alive(part[0]) || !alive(part[1]) || !alive(part[2])) {  // fields not set yet: by child name
            Il2CppArray* rs = componentsInChildren(gameObject(h), S.SR);
            int rn = rs ? (int)rs->max_length : 0;
            for (int k = 0; k < rn; k++) {
                Obj* r = (Obj*)arrayData(rs)[k];
                char nm[64] = "";
                objName(gameObject(r), nm, sizeof nm);
                bool head = false, end = false;
                for (char* c = nm; *c; c++) {
                    if ((c[0] == 'H' || c[0] == 'h') && c[1] == 'e' && c[2] == 'a' && c[3] == 'd') head = true;
                    if ((c[0] == 'E' || c[0] == 'e') && c[1] == 'n' && c[2] == 'd') end = true;
                }
                int slot = head ? 0 : end ? 2 : 1;
                if (!alive(part[slot])) part[slot] = r;
                static int hl = 0;
                if (hl < 4) {
                    hl++;
                    LOG("skin: hold renderer %d '%s' -> %s", k, nm, slot == 0 ? "head" : slot == 2 ? "end" : "body");
                }
            }
        }
        swapRenderer(part[0], R_HEAD);
        swapRenderer(part[1], R_BODY);
        swapRenderer(part[2], R_END);
        fixFields(h, 3);
    }
    for (int k = 0; k < 5; k++)
        if (g_hl[k].h) learn((Obj*)g_hl[k].get(), hlRoles[k], true);
    // future notes take the skin's multi-hit sprites straight from the lines
    for (int l = 0; l < nl; l++) {
        if (!alive(lines[l])) continue;
        for (int k = 0; k < 5; k++) {
            if (S.jlHL[k] < 0) continue;
            Obj* o = fld<Obj*>(lines[l], S.jlHL[k]);
            Obj* to = o ? mapped(o) : nullptr;
            if (to) fld<Obj*>(lines[l], S.jlHL[k]) = to;
        }
    }
    if (g_mapN == before) break;
    }
    static int logged = 0;
    if (logged++ < 3) LOG("skin: %d notes skinned (%d sprite pairs)", total, g_mapN);
}
void skinRefreshNotes() {
    if (skinActive()) scanNotes(true);
}

// ---- hit effects ----
struct Fx {
    Handle sr;      // the game's own effect sprite renderer, now showing our frames
    Handle go;      // the effect object: whatever else renders under it (its sparks, made after the clone) is hidden
    float born;
    Color c;
    bool good;
    Vec3 scale;     // its local scale for our frame size (the game's animation is overridden every frame)
};
static Fx g_fx[96];
static int g_fxNext = 0;
static int g_inLevelFrames = 0;  // frames since the level started (clone logging only during play)
static Handle g_setting;          // the settings scene (its delay preview gets the skin too)

// The game's effect keeps its renderer (layer, material, camera all stay right: a renderer of our own drew nothing,
// 0.27.1): only its sprite, colour and size are ours, set again every frame after the game's animation.
void skinOnClone(Obj* src, Obj* clone) {
    // Every Instantiate passes here (materials too, while scenes load): compare pointers only, nothing else is
    // touched unless src is one of the two effect prefabs (0.26.0 called gameObject() on anything -> black screen)
    if (!skinActive() || !g_frameN || !src || !clone) return;
    Obj* pc = (Obj*)g_pc.get();
    bool good = false, perf = false;
    if (pc) {
        Obj* sc = alive(pc) ? fld<Obj*>(pc, S.pcScore) : nullptr;  // read now: not set yet at ProgressControl.Start
        Obj* sp = alive(sc) && S.scPerfect >= 0 ? fld<Obj*>(sc, S.scPerfect) : nullptr;
        Obj* gp = alive(sc) && S.scGood >= 0 ? fld<Obj*>(sc, S.scGood) : nullptr;
        good = gp && src == gp, perf = sp && src == sp;
    } else if (alive((Obj*)g_setting.get()) && !strcmp(il2cpp_class_get_name(il2cpp_object_get_class(src)), "GameObject")) {
        // the settings' delay preview: the same "perfect" effect prefab, found by its name
        char n[32] = "";
        objName(src, n, sizeof n);
        perf = !strcmp(n, "perfect");
        static int logged = 0;
        if (logged < 6) {
            logged++;
            LOG("skin: settings clone '%s'%s", n, perf ? " -> the skin's effect" : "");
        }
        if (!perf) return;
    } else {
        return;
    }
    if (!good && !perf) {
        static int logged = 0;  // which prefabs a level clones (distinct names), to find where sparks come from
        static char seen[12][48];
        if (logged < 12 && g_inLevelFrames > 0) {
            char n[64] = "?";
            objName(src, n, sizeof n);
            bool dup = !strcmp(n, "JudgeLine");
            for (int q = 0; q < logged && !dup; q++) dup = !strcmp(seen[q], n);
            if (dup) return;
            snprintf(seen[logged++], 48, "%s", n);
            LOG("skin: clone of '%s' (%s) left alone", n, il2cpp_class_get_name(il2cpp_object_get_class(src)));
        }
        return;
    }
    Obj* go = strcmp(il2cpp_class_get_name(il2cpp_object_get_class(clone)), "GameObject") == 0 ? clone : gameObject(clone);
    Il2CppArray* rs = componentsInChildren(go, S.Renderer);
    int n = rs ? (int)rs->max_length : 0;
    Obj* main = nullptr;
    float width = 0;
    Vec3 ls0{1, 1, 1}, lossy{1, 1, 1};
    Color c = g_info.hitFxTinted ? argb(good ? g_info.colorGood : g_info.colorPerfect) : Color{1, 1, 1, 1};
    int sprites = 0, sparks = 0;
    for (int i = 0; i < n; i++) {
        Obj* r = (Obj*)arrayData(rs)[i];
        if (!alive(r)) continue;
        if (il2cpp_object_get_class(r) == S.SR) {
            sprites++;
            Obj* s = !main ? callm<Obj*>(S.srGetSprite, r) : nullptr;
            if (s) {
                Rect o = callm<Rect>(S.spRect, s);
                float ppu = callm<float>(S.spPPU, s);
                Obj* tr = goTransform(gameObject(r));
                lossy = callm<Vec3>(S.lossy, tr);
                if (S.trGetLocal) ls0 = callm<Vec3>(S.trGetLocal, tr);
                if (ppu > 0) width = o.w / ppu * lossy.x;
                main = r;
            } else {
                callm<void>(S.rEnabled, r, false);  // any other sprite of the effect
            }
        } else if (g_info.hideParticles) {
            callm<void>(S.rEnabled, r, false);
            sparks++;
        } else if (g_info.hitFxTinted && S.rGetMat && S.matHas && S.matSetColor) {  // the game's sparks, in skin colours
            Obj* m = callm<Obj*>(S.rGetMat, r);
            const char* props[2] = {"_Color", "_TintColor"};
            for (const char* p : props)
                if (m && callm<bool>(S.matHas, m, str(p))) callm<void>(S.matSetColor, m, str(p), c);
        }
    }
    if (!main || width <= 0) return;
    // our frame (100 px per unit) as wide as the game's sprite x hitFxScale, at the scale it has now
    Rect fr = callm<Rect>(S.spRect, (Obj*)g_frames[0].get());
    float frameW = fr.w / 100.f * (ls0.x != 0 ? lossy.x / ls0.x : 1);  // world width at local scale 1
    float k = frameW > 0 ? width * g_info.hitFxScale / frameW : 1;
    Fx& f = g_fx[g_fxNext];
    g_fxNext = (g_fxNext + 1) % 96;
    f.sr.set(main);
    f.go.set(go);
    f.born = timeSinceLevelLoad();
    f.c = c;
    f.good = good;
    f.scale = Vec3{k, k, ls0.z};
    callm<void>(S.srSetSprite, main, (Obj*)g_frames[0].get());
    callm<void>(S.srSetColor, main, c);
    if (S.trSetLocal) callm<void>(S.trSetLocal, goTransform(gameObject(main)), f.scale);
    static int logged = 0;
    if (logged++ < 2)
        LOG("skin: %s effect: its renderer shows our frames (game sprite %.3f wide, local scale %.3f -> %.3f; %d sprites, "
            "%d sparks hidden, tint %s %.2f %.2f %.2f a %.2f, pack colour %08X)",
            good ? "Good" : "Perfect", width, ls0.x, k, sprites, sparks, g_info.hitFxTinted ? "on" : "off", c.r, c.g, c.b, c.a,
            good ? g_info.colorGood : g_info.colorPerfect);
}

// the sparks' material colour: the skin colour over the game's own spark colour (their particle colour is gold /
// blue; the material is white), so what shows is the skin colour
static Color sparkTint(bool good) {
    Color s = argb(good ? g_info.colorGood : g_info.colorPerfect);
    Color base = good ? Color{0.70f, 0.88f, 1.f, 1} : Color{1.f, 0.93f, 0.63f, 1};
    return Color{s.r / base.r, s.g / base.g, s.b / base.b, s.a};
}
// the skin's Perfect / Good colours as RRGGBB for rich text (the in-level judgement counts); false = no skin
bool skinJudgeHex(char perf[8], char good[8]) {
    if (!skinActive()) return false;
    snprintf(perf, 8, "%06X", g_info.colorPerfect & 0xFFFFFF);
    snprintf(good, 8, "%06X", g_info.colorGood & 0xFFFFFF);
    return true;
}
bool skinLineColors(Color out[2]) {
    if (!skinActive()) return false;
    out[0] = argb(g_info.colorPerfect);
    out[1] = argb(g_info.colorGood);
    return true;
}

static void fxTick() {
    float now = timeSinceLevelLoad(), dur = g_info.hitFxDuration > 0 ? g_info.hitFxDuration : 0.5f;
    for (Fx& f : g_fx) {
        if (!f.sr.h) continue;
        Obj* sr = (Obj*)f.sr.get();
        if (!alive(sr)) {
            f.sr.clear();
            continue;
        }
        int i = (int)((now - f.born) / dur * g_frameN);
        if (i >= g_frameN) {
            callm<void>(S.rEnabled, sr, false);
            f.sr.clear();
            continue;
        }
        if (i < 0) i = 0;
        // the game's own sparks (spawned by the effect after it was cloned): in the skin's colours (hidden with hideParticles)
        Obj* go = (Obj*)f.go.get();
        Il2CppArray* rs = alive(go) ? componentsInChildren(go, S.Renderer) : nullptr;
        for (uintptr_t k = 0; rs && k < rs->max_length; k++) {
            Obj* r = (Obj*)arrayData(rs)[k];
            if (!r || r == sr || !alive(r)) continue;
            if (g_info.hideParticles) callm<void>(S.rEnabled, r, false);
            else if (g_info.hitFxTinted) critTintRenderer(r, sparkTint(f.good));
        }
        callm<void>(S.srSetSprite, sr, (Obj*)g_frames[i].get());
        callm<void>(S.srSetColor, sr, f.c);  // after the game's fade animation
        if (S.trSetLocal) callm<void>(S.trSetLocal, goTransform(gameObject(sr)), f.scale);
    }
}
// ---- the settings' delay preview: its note in the skin's tap look, its click effect (skinOnClone) and sounds ----
static Handle g_uiTap;  // the skin's tap image as a UI sprite
static void skinSettingFrame(void*) {
    Obj* st = (Obj*)g_setting.get();
    if (!st) return;
    if (!alive(st) || !skinActive()) {
        g_setting.clear();
        return;
    }
    fxTick();
    Obj* o = S.setNote >= 0 ? fld<Obj*>(st, S.setNote) : nullptr;
    if (!alive(o)) return;
    const char* cn = il2cpp_class_get_name(il2cpp_object_get_class(o));
    Obj* go = !strcmp(cn, "GameObject") ? o : gameObject(o);
    // a UI Image: the skin's tap texture, stretched over the note's rect
    Obj* img = S.Image ? getComponent(go, S.Image) : nullptr;
    if (!img && S.Image) {
        Il2CppArray* a = componentsInChildren(go, S.Image);
        img = a && a->max_length ? (Obj*)arrayData(a)[0] : nullptr;
    }
    if (img && S.imgSprite) {
        if (!alive((Obj*)g_uiTap.get())) {
            Obj* tex = (Obj*)g_tex[SK_CLICK].get();
            float w = (float)g_info.files[SK_CLICK].w, h = (float)g_info.files[SK_CLICK].h;
            if (alive(tex)) g_uiTap.set(keep(callm<Obj*>(S.spriteCreate, tex, Rect{0, 0, w, h}, Vec2{0.5f, 0.5f}, 100.f, 0u, 0, Vec4{0, 0, 0, 0})));
        }
        Obj* s = (Obj*)g_uiTap.get();
        if (alive(s) && callm<Obj*>(S.imgGetSprite, img) != s) callm<void>(S.imgSprite, img, s);
        return;
    }
    // a world sprite: the same stand-in as a tap note's
    Obj* sr = getComponent(go, S.SR);
    if (!sr) {
        Il2CppArray* a = componentsInChildren(go, S.SR);
        sr = a && a->max_length ? (Obj*)arrayData(a)[0] : nullptr;
    }
    if (alive(sr)) swapRenderer(sr, R_CLICK);
}

void skinSettingStart(Obj* setting) {
    if (!S.ok) return;
    ensureLoaded();
    skinSoundsApply(skinActive());  // the preview's hit sound too
    g_setting.clear();
    if (!skinActive()) return;
    g_setting.set(setting);
    Obj* o = S.setNote >= 0 ? fld<Obj*>(setting, S.setNote) : nullptr;
    LOG("skin: settings preview, note is %s", o ? il2cpp_class_get_name(il2cpp_object_get_class(o)) : "missing");
    static bool registered = false;
    if (!registered) {
        registered = true;
        onEveryFrame(skinSettingFrame);
    }
}
// ---- per level ----
static void skinFrame(void*) {
    Obj* pc = (Obj*)g_pc.get();
    if (!pc) return;
    if (!alive(pc)) {
        g_pc.clear();
        return;
    }
    g_inLevelFrames++;
    fxTick();
    g_nextScan -= unscaledDt();
    bool force = g_nextScan <= 0;
    if (force) g_nextScan = 1.f;
    scanNotes(force);
}

void skinLevelStart(Obj* pc) {
    if (!S.ok) return;
    ensureLoaded();
    g_pc.clear();
    skinSoundsApply(skinActive());  // the skin's hit sounds, or the game's back
    if (!skinActive()) return;
    g_pc.set(pc);
    g_lastCount = -1;
    g_nextScan = 0;
    g_inLevelFrames = 0;
    g_diag = 0;
    for (Handle& h : g_hl) h.clear();
    // the game's sprites of the last song are gone (it loads them again per song): a fresh map, so it never fills up
    // (0.28.7 kept them: after two or three songs the 24 entries were full and notes stayed unskinned)
    for (int i = 0; i < g_mapN; i++) g_map[i].from.clear(), g_map[i].to.clear();
    g_mapN = 0;
    static bool registered = false;
    if (!registered) {
        registered = true;
        onEveryFrame(skinFrame);
    }
    LOG("skin: level start with '%s'", g_name);
}

// ---- import (settings panel) ----
static const char* errText(SkinError e) {
    switch (e) {
    case SKE_NOT_ZIP: return T("这不是 zip 压缩包。", "This is not a zip file.");
    case SKE_ZIP_CORRUPT: return T("压缩包已损坏。", "The zip file is damaged.");
    case SKE_ZIP_UNSUPPORTED: return T("压缩包用了不支持的格式（加密、zip64 或特殊压缩方式）。", "The zip uses an unsupported format (encryption, zip64 or an unusual compression).");
    case SKE_TOO_BIG: return T("皮肤包太大了。", "The skin pack is too big.");
    case SKE_NO_INFO: return T("缺少 info.yml，可能不是 Phira 格式的皮肤包。", "info.yml is missing: probably not a Phira skin pack.");
    case SKE_INFO_SYNTAX: return T("info.yml 格式有误。", "info.yml has a syntax error.");
    case SKE_INFO_VALUE: return T("info.yml 里有不合法的数值。", "info.yml has an invalid value.");
    case SKE_MISSING_FILE: return T("缺少必需的图片。", "A required image is missing.");
    case SKE_BAD_IMAGE: return T("有图片不是合法的 PNG，或尺寸不对。", "An image is not a valid PNG or has the wrong size.");
    case SKE_BAD_SOUND: return T("有音效文件格式不对。", "A sound file has the wrong format.");
    case SKE_NO_MEMORY: return T("内存不足。", "Out of memory.");
    default: return "";
    }
}

static void imported(int status, const char* path, const char* name) {
    if (status == 0) return;
    if (status < 0 || !path) {
        saveMessage(T("没能打开文件", "Could not open the file"), name ? name : "");
        return;
    }
    uint8_t* b;
    uint32_t n;
    if (!readAll(path, &b, &n)) {
        saveMessage(T("没能读取文件", "Could not read the file"), T("文件为空、太大（超过 64 MB）或无法读取。", "The file is empty, too big (over 64 MB) or unreadable."));
        return;
    }
    SkinResult res;
    char msg[700];
    if (!loadPack(b, n, &res)) {
        LOG("skin: import of %s rejected: %s %s", name ? name : path, skinErrorName(res.err), res.detail);
        snprintf(msg, sizeof msg, "%s\n\n%s", errText(res.err), res.detail);
        saveMessage(T("皮肤包无法使用", "This skin pack cannot be used"), msg);
        free(b);
        return;
    }
    libRead();
    int id = libAdd(b, n, g_name, g_author);
    free(b);
    if (!id) {
        g_loaded = false;  // the textures are of a pack we could not keep
        g_loadedId = 0;
        saveMessage(T("没能保存皮肤包", "Could not keep the skin"), g_libN >= kLibMax ? T("最多 16 个皮肤包，先删掉几个吧。", "16 packs at most: delete some first.") : T("写入失败，存储空间可能不足。", "Writing failed; storage may be full."));
        return;
    }
    g_loadedId = id;
    g_failedId = 0;
    g_cfg.skin = true;
    g_cfg.skinId = id;
    g_cfg.critical = false;
    configSave();
    panelRefresh();
    LOG("skin: imported %s as pack %d", name ? name : path, id);
    char by[200] = "";
    if (g_author[0]) snprintf(by, sizeof by, T("（作者：%s）", " (by %s)"), g_author);
    snprintf(msg, sizeof msg, T("%s%s\n\n已开启，下一首歌生效。皮肤开启时不区分大小 P。", "%s%s\n\nOn from the next song. Critical Perfect is off while a skin is on."),
             g_name, by);
    saveMessage(T("皮肤已导入", "Skin imported"), msg);
}

void skinImport() {
    char copy[600] = "";
    if (!saveTempFile(copy, sizeof copy, "PhiMeSkin", "skin.zip")) copy[0] = 0;
    if (pickerAvailable() && pickerOpen(PICK_ZIP, copy, imported)) return;
    char hint[700];
    snprintf(hint, sizeof hint, T("系统文件选择器打不开。可以把皮肤包改名为 skin.zip 放进：\n%s\n然后重启游戏。", "The file picker could not open. Rename the pack to skin.zip, put it in:\n%s\nand restart the game."), saveFolderHint());
    saveMessage(T("导入皮肤包", "Import a skin"), hint);
}

// ---- the panel's 皮肤 page ----
int skinLibCount() {
    libRead();
    return g_libN;
}
bool skinLibEntry(int i, int* id, const char** name, const char** author) {
    if (i < 0 || i >= g_libN) return false;
    *id = g_lib[i].id;
    *name = g_lib[i].name;
    *author = g_lib[i].author;
    return true;
}
int skinCurrent() { return g_cfg.skin ? g_cfg.skinId : 0; }
bool skinBroken(int id) { return id && id == g_failedId && g_loadErr; }

void skinUse(int id) {
    if (id == 0) {
        g_cfg.skin = false;
    } else {
        g_cfg.skin = true;
        g_cfg.skinId = id;
        g_cfg.critical = false;
        if (g_failedId == id) g_failedId = 0;  // try it again
        ensureLoaded();
        if (g_failedId == id) {
            g_cfg.skin = false;
            saveMessage(T("皮肤包读不出来了", "The skin could not be read"), T("文件可能被删掉或损坏了，删掉它重新导入吧。", "The file may be gone or damaged: delete it and import it again."));
        }
    }
    configSave();
    panelRefresh();
    LOG("skin: using %d (%s)", id, g_cfg.skin ? "on" : "off");
}

static int g_pendingDelete = 0;
static void doDelete() {
    int id = g_pendingDelete;
    for (int i = 0; i < g_libN; i++)
        if (g_lib[i].id == id) {
            for (int k = i; k < g_libN - 1; k++) g_lib[k] = g_lib[k + 1];
            g_libN--;
            break;
        }
    char p[600];
    libFile(id, p, sizeof p);
    unlink(p);
    libWrite();
    if (g_cfg.skinId == id) {
        g_cfg.skin = false;
        g_cfg.skinId = 0;
        configSave();
    }
    if (g_loadedId == id) {
        g_loaded = false;
        g_loadedId = 0;
    }
    panelRefresh();
    LOG("skin: pack %d deleted", id);
}
void skinAskDelete(int id) {
    static char body[300];
    const char* name = "?";
    for (int i = 0; i < g_libN; i++)
        if (g_lib[i].id == id) name = g_lib[i].name;
    g_pendingDelete = id;
    snprintf(body, sizeof body, T("删除皮肤包「%s」？", "Delete the skin \"%s\"?"), name);
    SaveDialog d = {};
    d.title = T("删除皮肤包", "Delete skin");
    d.body = body;
    d.cancel = T("取消", "Cancel");
    d.ok = T("删除", "Delete");
    d.onOk = doDelete;
    saveDialog(d);
}

bool skinInstall() {
    S.Texture2D = findClass("UnityEngine", "Texture2D");
    g_Sprite = findClass("UnityEngine", "Sprite");
    S.Image = findClass("UnityEngine.UI", "Image");
    S.imgSprite = S.Image ? findMethod(S.Image, "set_sprite", 1) : nullptr;
    S.imgGetSprite = S.Image ? findMethod(S.Image, "get_sprite", 0) : nullptr;
    Il2CppClass* SetC = findClass("", "Setting");
    S.setNote = SetC ? fieldOffset(SetC, "note") : -1;
    S.Byte = findClass("System", "Byte");
    S.SR = findClass("UnityEngine", "SpriteRenderer");
    S.Renderer = findClass("UnityEngine", "Renderer");
    Il2CppClass* Sprite = findClass("UnityEngine", "Sprite");
    Il2CppClass* IC = findClass("UnityEngine", "ImageConversion");
    Il2CppClass* Tex = findClass("UnityEngine", "Texture");
    Il2CppClass* Tr = findClass("UnityEngine", "Transform");
    Il2CppClass* PC = findClass("", "ProgressControl");
    Il2CppClass* JC = findClass("", "JudgeControl");
    Il2CppClass* JL = findClass("", "JudgeLineControl");
    Il2CppClass* NUM = findClass("", "NoteUpdateManager");
    Il2CppClass* SC = findClass("", "ScoreControl");
    S.ClickC = findClass("", "ClickControl");
    S.DragC = findClass("", "DragControl");
    S.FlickC = findClass("", "FlickControl");
    S.HoldC = findClass("", "HoldControl");
    if (!S.Texture2D || !S.Byte || !S.SR || !S.Renderer || !Sprite || !IC || !Tr || !PC || !JC || !JL || !NUM || !SC)
        return false;
    S.texCtor = findMethod(S.Texture2D, ".ctor", 2, IL2CPP_TYPE_I4);
    S.getPixels = findMethod(S.Texture2D, "GetPixels32", 0);
    S.setPixels = findMethod(S.Texture2D, "SetPixels32", 1);
    S.texApply = findMethod(S.Texture2D, "Apply", 0);
    S.Color32 = findClass("UnityEngine", "Color32");
    S.loadImage = findMethod(IC, "LoadImage", 3);
    S.texWrap = Tex ? findMethod(Tex, "set_wrapMode", 1) : nullptr;
    S.spriteCreate = findMethod(Sprite, "Create", 7);
    S.spRect = findMethod(Sprite, "get_rect", 0);
    S.spPPU = findMethod(Sprite, "get_pixelsPerUnit", 0);
    S.spPivot = findMethod(Sprite, "get_pivot", 0);
    S.srGetSprite = findMethod(S.SR, "get_sprite", 0);
    S.srSetSprite = findMethod(S.SR, "set_sprite", 1);
    S.srSetColor = findMethod(S.SR, "set_color", 1);
    S.rEnabled = findMethod(S.Renderer, "set_enabled", 1);
    S.rGetOrder = findMethod(S.Renderer, "get_sortingOrder", 0);
    S.rSetOrder = findMethod(S.Renderer, "set_sortingOrder", 1);
    S.rGetLayer = findMethod(S.Renderer, "get_sortingLayerID", 0);
    S.rSetLayer = findMethod(S.Renderer, "set_sortingLayerID", 1);
    S.lossy = findMethod(Tr, "get_lossyScale", 0);
    S.trGetLocal = findMethod(Tr, "get_localScale", 0);
    S.trSetLocal = findMethod(Tr, "set_localScale", 1);
    Il2CppClass* UObj = findClass("UnityEngine", "Object");
    S.hideFlags = UObj ? findMethod(UObj, "set_hideFlags", 1) : nullptr;
    Il2CppClass* GO = findClass("UnityEngine", "GameObject");
    Il2CppClass* Mat = findClass("UnityEngine", "Material");
    S.goGetLayer = GO ? findMethod(GO, "get_layer", 0) : nullptr;
    S.goSetLayer = GO ? findMethod(GO, "set_layer", 1) : nullptr;
    S.rGetShared = findMethod(S.Renderer, "get_sharedMaterial", 0);
    S.srDrawMode = findMethod(S.SR, "get_drawMode", 0);
    S.srSize = findMethod(S.SR, "get_size", 0);
    Il2CppClass* GI = findClass("", "GameInformation");
    S.giMain = GI ? il2cpp_class_get_field_from_name(GI, "_main") : nullptr;
    S.giNoteScale = GI ? fieldOffset(GI, "noteScale") : -1;
    S.rSetShared = findMethod(S.Renderer, "set_sharedMaterial", 1);
    S.rGetMat = findMethod(S.Renderer, "get_material", 0);
    S.matHas = Mat ? findMethod(Mat, "HasProperty", 1, IL2CPP_TYPE_STRING) : nullptr;
    S.matSetColor = Mat ? findMethod(Mat, "SetColor", 2, IL2CPP_TYPE_STRING) : nullptr;
    S.pcJc = fieldOffset(PC, "judgeControl");
    S.pcScore = fieldOffset(PC, "scoreControl");
    S.jcNum = fieldOffset(JC, "noteUpdateManager");
    S.jcLines = fieldOffset(JC, "judgeLineControls");
    const char* lists[4] = {"clickControls", "dragControls", "holdControls", "flickControls"};
    for (int i = 0; i < 4; i++) S.numLists[i] = fieldOffset(NUM, lists[i]);
    S.ccSR = S.ClickC ? fieldOffset(S.ClickC, "_spriteRenderer") : -1;
    S.dcSR = S.DragC ? fieldOffset(S.DragC, "_spriteRenderer") : -1;
    S.fcSR = S.FlickC ? fieldOffset(S.FlickC, "_spriteRenderer") : -1;
    S.hHead = S.HoldC ? fieldOffset(S.HoldC, "_holdHeadSpriteRenderer") : -1;
    S.hBody = S.HoldC ? fieldOffset(S.HoldC, "_holdSpriteRenderer") : -1;
    S.hEnd = S.HoldC ? fieldOffset(S.HoldC, "_holdEndSpriteRenderer1") : -1;
    S.scPerfect = fieldOffset(SC, "perfectJudge");
    S.scGood = fieldOffset(SC, "goodJudge");
    const char* hl[5] = {"ClickHL", "DragHL", "FlickHL", "HoldHL0", "HoldHL1"};
    for (int i = 0; i < 5; i++) S.jlHL[i] = fieldOffset(JL, hl[i]);
    S.jlHL[5] = -1;
    const void* must[] = {S.texCtor, S.loadImage, S.spriteCreate, S.spRect, S.spPPU, S.spPivot, S.srGetSprite,
                          S.srSetSprite, S.srSetColor, S.rEnabled, S.rGetOrder, S.rSetOrder, S.rGetLayer, S.rSetLayer,
                          S.lossy};
    S.ok = true;
    for (const void* m : must) S.ok = S.ok && m;
    LOG("skin: %s; renderers tap %d drag %d flick %d hold %d/%d/%d, HL %d %d %d %d %d, effects %d %d",
        S.ok ? "ready" : "off (methods missing)", S.ccSR, S.dcSR, S.fcSR, S.hHead, S.hBody, S.hEnd, S.jlHL[0], S.jlHL[1],
        S.jlHL[2], S.jlHL[3], S.jlHL[4], S.scPerfect, S.scGood);
    return S.ok;
}
