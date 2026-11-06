// The mod's own text rasterizer. Unity's font system let us down on the iPad (system CJK fonts drew blank /
// partial glyphs, and fonts opened from a file are not dynamic there), so the mod lays out and draws its text
// itself with stb_truetype (public domain), from the fonts it carries (assets.cpp):
//   every character: Source Serif 4 if it has the glyph, else the CJK font (Noto Sans SC 思源黑体)
//   bold: Source Serif 4 Bold / Noto Sans SC Bold (the mod's own characters), else the regular face drawn twice
// The result is a white image with coverage in alpha, shown by a RawImage (unity.cpp); the colour is the tint.
#ifndef PM_GLYPHS_TEST
#include "mod.h"
#endif

#define STBTT_ifloor(x) ((int)__builtin_floor(x))
#define STBTT_iceil(x) ((int)__builtin_ceil(x))
#define STBTT_sqrt(x) __builtin_sqrt(x)
#define STBTT_pow(x, y) pm_pow(x, y)
#define STBTT_fmod(x, y) pm_fmod(x, y)
#define STBTT_cos(x) pm_cos(x)
#define STBTT_acos(x) pm_acos(x)
#define STBTT_fabs(x) __builtin_fabs(x)
#define STBTT_malloc(x, u) ((void)(u), malloc(x))
#define STBTT_free(x, u) ((void)(u), free(x))
#define STBTT_assert(x) ((void)0)
#define STBTT_strlen(x) strlen(x)
#define STBTT_memcpy memcpy
#define STBTT_memset memset
// only the SDF functions (unused) need these
static double pm_pow(double, double) { return 0; }
static double pm_fmod(double x, double y) { return y ? x - (double)(long)(x / y) * y : 0; }
static double pm_cos(double) { return 1; }
static double pm_acos(double) { return 0; }
#ifndef NULL
#define NULL nullptr
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#include "third_party/stb_truetype.h"
#pragma clang diagnostic pop

namespace unity {
bool embeddedFont(int which, const unsigned char** data, int* size, const char** name);
const char* serifFontName();  // assets.cpp: which Latin font this build carries
}

namespace glyphs {

enum { F_SERIF_R, F_SERIF_B, F_CJK_R, F_CJK_B, F_N };
static stbtt_fontinfo g_f[F_N];
static float g_ascent[F_N], g_descent[F_N], g_gap[F_N];  // per em
static int g_state = 0;

bool ready() {
    if (g_state) return g_state > 0;
    g_state = -1;
    for (int i = 0; i < F_N; i++) {
        const unsigned char* d;
        int n;
        const char* name;
        if (!unity::embeddedFont(i, &d, &n, &name) || !stbtt_InitFont(&g_f[i], d, stbtt_GetFontOffsetForIndex(d, 0))) {
            LOG("glyphs: font %d did not load", i);
            return false;
        }
        int a, de, g;
        stbtt_GetFontVMetrics(&g_f[i], &a, &de, &g);
        float em = stbtt_ScaleForMappingEmToPixels(&g_f[i], 1.f);
        g_ascent[i] = a * em;
        g_descent[i] = de * em;
        g_gap[i] = g * em;
    }
    g_state = 1;
    LOG("glyphs: own text rendering on (%s + Noto Sans SC)", unity::serifFontName());
    return true;
}

static int decode(const char*& p) {
    const unsigned char* s = (const unsigned char*)p;
    int c = s[0], n = 1;
    if (c < 0x80) {
    } else if ((c & 0xE0) == 0xC0 && s[1]) c = ((c & 0x1F) << 6) | (s[1] & 0x3F), n = 2;
    else if ((c & 0xF0) == 0xE0 && s[1] && s[2]) c = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F), n = 3;
    else if ((c & 0xF8) == 0xF0 && s[1] && s[2] && s[3])
        c = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F), n = 4;
    else c = 0xFFFD;
    p += n;
    return c;
}

struct G {
    int cp, face, glyph;
    bool synth;     // drawn twice (bold wanted, the face has none)
    float x, adv;   // position on its line, advance (px)
};

// the face for a character: serif when it has it, else CJK (bold where possible)
static void pick(int cp, bool bold, G* g) {
    g->synth = false;
    int sf = bold ? F_SERIF_B : F_SERIF_R;
    int gi = stbtt_FindGlyphIndex(&g_f[sf], cp);
    if (gi) {
        g->face = sf, g->glyph = gi;
        return;
    }
    if (bold) {
        gi = stbtt_FindGlyphIndex(&g_f[F_CJK_B], cp);
        if (gi) {
            g->face = F_CJK_B, g->glyph = gi;
            return;
        }
    }
    gi = stbtt_FindGlyphIndex(&g_f[F_CJK_R], cp);
    g->face = F_CJK_R, g->glyph = gi;
    g->synth = bold;
    if (!gi) {  // nowhere: the serif's own fallback glyph
        g->face = sf, g->glyph = 0;
        g->synth = false;
    }
}

static bool isCjk(int cp) { return cp >= 0x2E80; }
// no line starts with these (Chinese / Japanese closing punctuation)
static bool noStart(int cp) {
    static const int k[] = {0x3001, 0x3002, 0xFF0C, 0xFF0E, 0xFF1A, 0xFF1B, 0xFF01, 0xFF1F, 0xFF09, 0x300D, 0x300F,
                            0x3011, 0x2026, 0x00B7, 0x30FC, ')', ',', '.', '!', '?', ':', ';'};
    for (int v : k)
        if (cp == v) return true;
    return false;
}

struct Layout {
    G* g = nullptr;
    int n = 0;
    int lineStart[64], lineEnd[64], lines = 0;
    float lineW[64];
    float asc = 0, desc = 0, lineH = 0;
    ~Layout() { free(g); }
};

static float scaleOf(int face, float px) { return stbtt_ScaleForMappingEmToPixels(&g_f[face], px); }

static bool layout(const char* s, float px, bool bold, float wrap, Layout* L) {
    int cap = (int)strlen(s) + 1;
    L->g = (G*)malloc(sizeof(G) * cap);
    if (!L->g) return false;
    // shared line box: the larger of the two families
    float asc = g_ascent[F_SERIF_R] > g_ascent[F_CJK_R] ? g_ascent[F_SERIF_R] : g_ascent[F_CJK_R];
    float desc = g_descent[F_SERIF_R] < g_descent[F_CJK_R] ? g_descent[F_SERIF_R] : g_descent[F_CJK_R];
    L->asc = asc * px;
    L->desc = -desc * px;
    L->lineH = (asc - desc) * px * 1.05f;
    for (const char* p = s; *p;) {
        G& g = L->g[L->n];
        g.cp = decode(p);
        if (g.cp == '\r') continue;
        if (g.cp == '\n') {
            g.face = -1, g.adv = 0;
            L->n++;
            continue;
        }
        pick(g.cp, bold, &g);
        int adv, lsb;
        stbtt_GetGlyphHMetrics(&g_f[g.face], g.glyph, &adv, &lsb);
        g.adv = adv * scaleOf(g.face, px);
        if (g.synth) g.adv += px / 24;
        if (L->n > 0 && L->g[L->n - 1].face == g.face)
            L->g[L->n - 1].adv += stbtt_GetGlyphKernAdvance(&g_f[g.face], L->g[L->n - 1].glyph, g.glyph) *
                                  scaleOf(g.face, px);
        L->n++;
    }
    // lines: explicit breaks, and (wrap > 0) the last allowed break before the width runs out
    int i = 0;
    while (i <= L->n && L->lines < 64) {
        int start = i, lastBreak = -1;
        float x = 0;
        while (i < L->n && L->g[i].face >= 0) {
            G& g = L->g[i];
            if (wrap > 0 && i > start && x + g.adv > wrap && !(g.cp == ' ')) {
                int b = -1;
                if (lastBreak > start) b = lastBreak;
                else if ((isCjk(g.cp) || isCjk(L->g[i - 1].cp)) && !noStart(g.cp)) b = i;
                if (b < 0 && i - 1 > start) b = i;  // one long word: cut it
                if (b > start) {
                    i = b;
                    break;
                }
            }
            if (g.cp == ' ') lastBreak = i + 1;
            else if (i + 1 < L->n && (isCjk(g.cp) || isCjk(L->g[i + 1].cp)) && !noStart(L->g[i + 1].cp))
                lastBreak = i + 1;
            g.x = x;
            x += g.adv;
            i++;
        }
        int end = i;
        while (end > start && L->g[end - 1].cp == ' ') end--;  // trailing spaces don't count
        float w = 0;
        for (int k = start; k < end; k++) {
            L->g[k].x = w;
            w += L->g[k].adv;
        }
        L->lineStart[L->lines] = start;
        L->lineEnd[L->lines] = end;
        L->lineW[L->lines] = w;
        L->lines++;
        if (i < L->n && L->g[i].face < 0) i++;  // past the '\n'
        else if (i >= L->n) break;
        while (i < L->n && L->g[i].cp == ' ' && wrap > 0) i++;  // a wrapped line doesn't start with spaces
    }
    return true;
}

float measure(const char* s, float px, bool bold, float wrap, float* height) {
    if (!ready() || !s) return 0;
    Layout L;
    if (!layout(s, px, bold, wrap, &L)) return 0;
    float w = 0;
    for (int i = 0; i < L.lines; i++)
        if (L.lineW[i] > w) w = L.lineW[i];
    if (height) *height = L.lines ? L.lineH * (L.lines - 1) + L.asc + L.desc : 0;
    return w;
}

// draws `s` at `px` into a new RGBA image (white, coverage in alpha, top row first) with `pad` px around it.
// align: 0 left, 1 centre, 2 right (each line in the widest one). Free *rgba with free().
bool render(const char* s, float px, bool bold, float wrap, int align, int pad, unsigned char** rgba, int* w, int* h,
            float* textW, float* textH) {
    *rgba = nullptr;
    if (!ready() || !s || px < 1) return false;
    Layout L;
    if (!layout(s, px, bold, wrap, &L)) return false;
    float maxW = 0;
    for (int i = 0; i < L.lines; i++)
        if (L.lineW[i] > maxW) maxW = L.lineW[i];
    float th = L.lines ? L.lineH * (L.lines - 1) + L.asc + L.desc : px;
    *textW = maxW;
    *textH = th;
    int W = (int)__builtin_ceil(maxW) + 2 * pad + 2, H = (int)__builtin_ceil(th) + 2 * pad + 2;
    if (W < 4) W = 4;
    if (W > 4096) W = 4096;
    if (H > 4096) H = 4096;
    unsigned char* a = (unsigned char*)calloc((size_t)W * H, 1);
    if (!a) return false;
    unsigned char* tmp = nullptr;
    int tmpCap = 0;
    for (int ln = 0; ln < L.lines; ln++) {
        float x0 = pad + 1 + (align == 1 ? (maxW - L.lineW[ln]) / 2 : align == 2 ? maxW - L.lineW[ln] : 0);
        float base = pad + 1 + L.asc + ln * L.lineH;
        for (int k = L.lineStart[ln]; k < L.lineEnd[ln]; k++) {
            G& g = L.g[k];
            if (g.cp == ' ' || g.face < 0) continue;
            float sc = scaleOf(g.face, px);
            float gx = x0 + g.x;
            int ix = (int)__builtin_floor(gx);
            float sub = gx - ix;
            int bx0, by0, bx1, by1;
            stbtt_GetGlyphBitmapBoxSubpixel(&g_f[g.face], g.glyph, sc, sc, sub, 0, &bx0, &by0, &bx1, &by1);
            int gw = bx1 - bx0, gh = by1 - by0;
            if (gw <= 0 || gh <= 0) continue;
            if (gw * gh > tmpCap) {
                free(tmp);
                tmpCap = gw * gh;
                tmp = (unsigned char*)malloc(tmpCap);
                if (!tmp) break;
            }
            stbtt_MakeGlyphBitmapSubpixel(&g_f[g.face], tmp, gw, gh, gw, sc, sc, sub, 0, g.glyph);
            int by = (int)__builtin_floor(base) + by0;
            int passes = g.synth ? 2 : 1, off = (int)(px / 24) > 1 ? (int)(px / 24) : 1;
            for (int p = 0; p < passes; p++) {
                int bxs = ix + bx0 + p * off;
                for (int y = 0; y < gh; y++) {
                    int yy = by + y;
                    if (yy < 0 || yy >= H) continue;
                    unsigned char* row = a + (size_t)yy * W;
                    const unsigned char* src = tmp + y * gw;
                    for (int x = 0; x < gw; x++) {
                        int xx = bxs + x;
                        if (xx < 0 || xx >= W) continue;
                        int v = row[xx] + src[x];
                        row[xx] = v > 255 ? 255 : (unsigned char)v;
                    }
                }
            }
        }
    }
    free(tmp);
    unsigned char* out = (unsigned char*)malloc((size_t)W * H * 4);
    if (!out) {
        free(a);
        return false;
    }
    for (int i = 0; i < W * H; i++) {
        out[i * 4] = out[i * 4 + 1] = out[i * 4 + 2] = 255;
        out[i * 4 + 3] = a[i];
    }
    free(a);
    *rgba = out;
    *w = W;
    *h = H;
    return true;
}

}  // namespace glyphs
