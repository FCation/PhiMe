// Music waveform, the decoding half: a song's Addressables bundle -> its FSB5 Vorbis stream -> peak / RMS per
// 128 samples. Unity streams Phigros' music (load type Streaming), so AudioClip.GetData has nothing to give; the
// mod reads the bundle file itself instead:
//   UnityFS bundle (LZ4 blocks) -> the .resource node -> FSB5 -> its Vorbis packets (FMOD keeps only the CRC32
//   of the setup header: vorbis_setups.inc has the three Phigros uses) -> an Ogg stream in memory -> stb_vorbis
// No Unity calls here (it runs on a worker thread); wave.cpp finds the file and shows the result.
#ifdef PM_WAVE_TEST
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define LOG(...) (printf(__VA_ARGS__), printf("\n"))
#else
#include "platform.h"
extern "C" {
double cos(double);
double sin(double);
double pow(double, double);
double exp(double);
double log(double);
double ldexp(double, int);
}
#endif
#include "wave_decode.h"

// ---------- stb_vorbis (public domain), freestanding ----------
#define STB_VORBIS_NO_CRT
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION
#ifndef PM_WAVE_TEST
#define floor(x) __builtin_floor(x)
#define abs(x) __builtin_abs(x)
#define assert(x) ((void)0)
#define alloca(n) __builtin_alloca(n)
#ifndef NULL
#define NULL nullptr
#endif
#ifndef INT_MAX
#define INT_MAX 0x7fffffff
#endif
#ifndef UINT_MAX
#define UINT_MAX 0xffffffffu
#endif
#else
#include <assert.h>
#include <limits.h>
#include <malloc.h>
#endif
// qsort for stb_vorbis' codebook setup (small arrays, once per song): shell sort
static void pm_qsort(void* base, size_t n, size_t sz, int (*cmp)(const void*, const void*)) {
    unsigned char* a = (unsigned char*)base;
    unsigned char tmp[64];
    if (sz > sizeof tmp) return;
    for (size_t gap = n / 2; gap > 0; gap /= 2)
        for (size_t i = gap; i < n; i++) {
            memcpy(tmp, a + i * sz, sz);
            size_t j = i;
            while (j >= gap && cmp(a + (j - gap) * sz, tmp) > 0) {
                memcpy(a + j * sz, a + (j - gap) * sz, sz);
                j -= gap;
            }
            memcpy(a + j * sz, tmp, sz);
        }
}
#define qsort pm_qsort
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wunused-value"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wmisleading-indentation"
#pragma clang diagnostic ignored "-Wtautological-compare"
#include "third_party/stb_vorbis.c"
#pragma clang diagnostic pop
#undef floor
#undef abs
#undef qsort

#include "vorbis_setups.inc"

// ---------- byte helpers ----------
static size_t strnlen_pm(const char* s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}
static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint64_t be64(const uint8_t* p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
static uint32_t le32(const uint8_t* p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }
static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[1] << 8 | p[0]); }
static uint64_t le64(const uint8_t* p) { return (uint64_t)le32(p + 4) << 32 | le32(p); }

// LZ4 block format (what Unity's LZ4 / LZ4HC bundles use); false on any inconsistency
static bool lz4(const uint8_t* src, size_t sn, uint8_t* dst, size_t dn) {
    size_t i = 0, o = 0;
    while (i < sn) {
        unsigned tok = src[i++];
        size_t lit = tok >> 4;
        if (lit == 15) {
            unsigned b;
            do {
                if (i >= sn) return false;
                b = src[i++];
                lit += b;
            } while (b == 255);
        }
        if (i + lit > sn || o + lit > dn) return false;
        memcpy(dst + o, src + i, lit);
        i += lit;
        o += lit;
        if (i >= sn) break;  // the last sequence has literals only
        if (i + 2 > sn) return false;
        size_t off = src[i] | src[i + 1] << 8;
        i += 2;
        if (!off || off > o) return false;
        size_t m = (tok & 15) + 4;
        if ((tok & 15) == 15) {
            unsigned b;
            do {
                if (i >= sn) return false;
                b = src[i++];
                m += b;
            } while (b == 255);
        }
        if (o + m > dn) return false;
        for (size_t k = 0; k < m; k++, o++) dst[o] = dst[o - off];  // may overlap
    }
    return o == dn;
}

static bool unpack(int comp, const uint8_t* src, size_t sn, uint8_t* dst, size_t dn) {
    if (comp == 0) {
        if (sn != dn) return false;
        memcpy(dst, src, dn);
        return true;
    }
    if (comp == 2 || comp == 3) return lz4(src, sn, dst, dn);
    return false;  // LZMA (1) / LZHAM: not used by Phigros
}

// ---------- UnityFS: the largest ".resource" / ".resS" node, uncompressed (malloc'd) ----------
static uint8_t* bundleResource(const uint8_t* b, size_t n, size_t* outN, char* why, int whyCap) {
    if (n < 64 || memcmp(b, "UnityFS", 8) != 0) {
        snprintf(why, whyCap, "not a UnityFS bundle");
        return nullptr;
    }
    size_t p = 8;
    uint32_t ver = be32(b + p);
    p += 4;
    for (int s = 0; s < 2; s++) {  // engine version strings
        while (p < n && b[p]) p++;
        p++;
    }
    if (p + 20 > n) return nullptr;
    p += 8;  // total size
    uint32_t cInfo = be32(b + p), uInfo = be32(b + p + 4), flags = be32(b + p + 8);
    p += 12;
    if (ver >= 7) p = (p + 15) & ~(size_t)15;
    const uint8_t* infoSrc;
    if (flags & 0x80) infoSrc = b + n - cInfo;  // blocks info at the end
    else {
        infoSrc = b + p;
        p += cInfo;
    }
    if (infoSrc < b || infoSrc + cInfo > b + n || uInfo > (1u << 24)) {
        snprintf(why, whyCap, "bad blocks info");
        return nullptr;
    }
    uint8_t* info = (uint8_t*)malloc(uInfo);
    if (!info || !unpack(flags & 0x3f, infoSrc, cInfo, info, uInfo)) {
        free(info);
        snprintf(why, whyCap, "blocks info compression %u", flags & 0x3f);
        return nullptr;
    }
    if (flags & 0x200) p = (p + 15) & ~(size_t)15;
    // blocks
    size_t q = 16;
    int nb = (int)be32(info + q);
    q += 4;
    if (nb <= 0 || q + (size_t)nb * 10 + 4 > uInfo) {
        free(info);
        return nullptr;
    }
    const uint8_t* blocks = info + q;
    q += (size_t)nb * 10;
    int nn = (int)be32(info + q);
    q += 4;
    uint64_t bestOff = 0, bestSize = 0;
    for (int i = 0; i < nn && q + 20 < uInfo; i++) {
        uint64_t off = be64(info + q), size = be64(info + q + 8);
        const char* path = (const char*)info + q + 20;
        size_t len = strnlen_pm(path, uInfo - q - 20);
        q += 20 + len + 1;
        bool res = (len > 9 && !memcmp(path + len - 9, ".resource", 9)) || (len > 5 && !memcmp(path + len - 5, ".resS", 5));
        if (res && size > bestSize) {
            bestOff = off;
            bestSize = size;
        }
    }
    if (!bestSize) {
        free(info);
        snprintf(why, whyCap, "no resource node");
        return nullptr;
    }
    uint8_t* out = (uint8_t*)malloc(bestSize);
    if (!out) {
        free(info);
        return nullptr;
    }
    // walk the blocks: uncompressed stream position u, file position p
    uint64_t u = 0;
    uint8_t* tmp = nullptr;
    size_t tmpCap = 0;
    bool ok = true;
    for (int i = 0; i < nb && ok; i++) {
        const uint8_t* e = blocks + (size_t)i * 10;
        uint32_t us = be32(e), cs = be32(e + 4);
        int comp = be16(e + 8) & 0x3f;
        if (p + cs > n) {
            ok = false;
            break;
        }
        uint64_t a = u > bestOff ? u : bestOff, z = u + us < bestOff + bestSize ? u + us : bestOff + bestSize;
        if (a < z) {  // this block holds part of the resource
            const uint8_t* plain = b + p;
            if (comp != 0) {
                if (tmpCap < us) {
                    free(tmp);
                    tmp = (uint8_t*)malloc(us);
                    tmpCap = tmp ? us : 0;
                }
                if (!tmp || !unpack(comp, b + p, cs, tmp, us)) {
                    snprintf(why, whyCap, "block %d compression %d", i, comp);
                    ok = false;
                    break;
                }
                plain = tmp;
            } else if (cs != us) {
                ok = false;
                break;
            }
            memcpy(out + (a - bestOff), plain + (a - u), (size_t)(z - a));
        }
        p += cs;
        u += us;
    }
    free(tmp);
    free(info);
    if (!ok || u < bestOff + bestSize) {
        free(out);
        if (!why[0]) snprintf(why, whyCap, "bundle blocks short");
        return nullptr;
    }
    *outN = (size_t)bestSize;
    return out;
}

// ---------- FSB5 Vorbis -> Ogg ----------
static uint32_t g_crcTab[256];
static void crcInit() {
    if (g_crcTab[1]) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t r = i << 24;
        for (int k = 0; k < 8; k++) r = r & 0x80000000u ? (r << 1) ^ 0x04c11db7u : r << 1;
        g_crcTab[i] = r;
    }
}

struct Buf {
    uint8_t* d = nullptr;
    size_t n = 0, cap = 0;
    bool put(const void* s, size_t k) {
        if (n + k > cap) {
            size_t c = cap ? cap * 2 : 65536;
            while (c < n + k) c *= 2;
            uint8_t* nd = (uint8_t*)malloc(c);
            if (!nd) return false;
            if (d) {
                memcpy(nd, d, n);
                free(d);
            }
            d = nd;
            cap = c;
        }
        memcpy(d + n, s, k);
        n += k;
        return true;
    }
};

// one packet per page; granule -1 (nothing to trim: we only look at the samples)
static bool oggPage(Buf& o, uint32_t seq, int type, const uint8_t* pk, size_t len) {
    uint8_t h[27 + 255];
    int segs = (int)(len / 255) + 1;
    if (segs > 255) return false;
    memcpy(h, "OggS", 4);
    h[4] = 0;
    h[5] = (uint8_t)type;
    memset(h + 6, 0xff, 8);
    h[14] = 0x50, h[15] = 0x4d, h[16] = 0x57, h[17] = 0x31;  // serial "PMW1"
    h[18] = (uint8_t)seq, h[19] = (uint8_t)(seq >> 8), h[20] = (uint8_t)(seq >> 16), h[21] = (uint8_t)(seq >> 24);
    memset(h + 22, 0, 4);
    h[26] = (uint8_t)segs;
    for (int i = 0; i < segs - 1; i++) h[27 + i] = 255;
    h[27 + segs - 1] = (uint8_t)(len % 255);
    uint32_t crc = 0;
    for (int i = 0; i < 27 + segs; i++) crc = (crc << 8) ^ g_crcTab[(crc >> 24) ^ h[i]];
    for (size_t i = 0; i < len; i++) crc = (crc << 8) ^ g_crcTab[(crc >> 24) ^ pk[i]];
    h[22] = (uint8_t)crc, h[23] = (uint8_t)(crc >> 8), h[24] = (uint8_t)(crc >> 16), h[25] = (uint8_t)(crc >> 24);
    return o.put(h, 27 + segs) && o.put(pk, len);
}

static const int kFsbRates[] = {0, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000};

static uint8_t* fsbToOgg(const uint8_t* f, size_t n, size_t* oggN, int* rateOut, int* chOut, uint32_t* samplesOut,
                         char* why, int whyCap) {
    if (n < 72 || memcmp(f, "FSB5", 4) != 0) {
        snprintf(why, whyCap, "no FSB5");
        return nullptr;
    }
    uint32_t ver = le32(f + 4), shSize = le32(f + 12), nameSize = le32(f + 16), dataSize = le32(f + 20),
             mode = le32(f + 24);
    if (mode != 15) {
        snprintf(why, whyCap, "FSB5 codec %u, not Vorbis", mode);
        return nullptr;
    }
    size_t hdr = ver == 0 ? 64 : 60, p = hdr;
    uint64_t s = le64(f + p);
    p += 8;
    bool next = s & 1;
    int rate = (int)((s >> 1) & 15) < 11 ? kFsbRates[(s >> 1) & 15] : 0, ch = (int)((s >> 5) & 1) + 1;
    uint32_t samples = (uint32_t)((s >> 34) & 0x3fffffff);
    uint32_t crc = 0;
    while (next && p + 4 <= hdr + shSize) {
        uint32_t c = le32(f + p);
        next = c & 1;
        uint32_t size = (c >> 1) & 0xffffff, type = (c >> 25) & 0x7f;
        const uint8_t* body = f + p + 4;
        if (type == 1) ch = body[0];
        else if (type == 2) rate = (int)le32(body);
        else if (type == 11) crc = le32(body);
        p += 4 + size;
    }
    const uint8_t* setup = nullptr;
    int setupN = 0;
    for (auto& e : kSetups)
        if (e.crc == crc) {
            setup = e.data;
            setupN = e.size;
        }
    if (!setup || !rate || ch < 1 || ch > 2) {
        snprintf(why, whyCap, "unknown Vorbis setup %08x (%d Hz, %d ch)", crc, rate, ch);
        return nullptr;
    }
    size_t d0 = hdr + shSize + nameSize, d1 = d0 + dataSize;
    if (d1 > n) d1 = n;
    crcInit();
    Buf o;
    uint8_t id[30] = {1, 'v', 'o', 'r', 'b', 'i', 's', 0, 0, 0, 0, (uint8_t)ch};
    id[12] = (uint8_t)rate, id[13] = (uint8_t)(rate >> 8), id[14] = (uint8_t)(rate >> 16), id[15] = (uint8_t)(rate >> 24);
    id[28] = 0xB8;  // blocksizes 256 / 2048
    id[29] = 1;     // framing
    static const uint8_t comment[] = {3, 'v', 'o', 'r', 'b', 'i', 's', 5, 0, 0, 0, 'P', 'h', 'i', 'M', 'e', 0, 0, 0, 0, 1};
    uint32_t seq = 0;
    bool ok = oggPage(o, seq++, 2, id, sizeof id) && oggPage(o, seq++, 0, comment, sizeof comment) &&
              oggPage(o, seq++, 0, setup, (size_t)setupN);
    size_t q = d0;
    while (ok && q + 2 <= d1) {
        size_t len = le16(f + q);
        q += 2;
        if (!len || q + len > d1) break;
        bool last = q + len + 2 > d1 || le16(f + q + len) == 0;
        ok = oggPage(o, seq++, last ? 4 : 0, f + q, len);
        q += len;
    }
    if (!ok) {
        free(o.d);
        snprintf(why, whyCap, "out of memory");
        return nullptr;
    }
    *oggN = o.n;
    *rateOut = rate;
    *chOut = ch;
    *samplesOut = samples;
    return o.d;
}

// ---------- the whole thing ----------
bool waveDecode(const uint8_t* bundle, size_t n, WaveSink* sink, char* why, int whyCap) {
    why[0] = 0;
    size_t resN = 0;
    uint8_t* res = bundleResource(bundle, n, &resN, why, whyCap);
    if (!res) return false;
    size_t fsbAt = 0;  // the FSB5 is normally the whole resource
    while (fsbAt + 4 <= resN && memcmp(res + fsbAt, "FSB5", 4) != 0) fsbAt++;
    size_t oggN = 0;
    int rate = 0, ch = 0;
    uint32_t samples = 0;
    uint8_t* ogg = fsbAt + 4 <= resN ? fsbToOgg(res + fsbAt, resN - fsbAt, &oggN, &rate, &ch, &samples, why, whyCap)
                                     : nullptr;
    free(res);
    if (!ogg) {
        if (!why[0]) snprintf(why, whyCap, "no FSB5 in the resource");
        return false;
    }
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_memory(ogg, (int)oggN, &err, nullptr);
    if (!v) {
        free(ogg);
        snprintf(why, whyCap, "stb_vorbis error %d", err);
        return false;
    }
    if (!sink->begin(sink->user, rate, samples)) {
        stb_vorbis_close(v);
        free(ogg);
        snprintf(why, whyCap, "cancelled");
        return false;
    }
    const int B = WAVE_BIN;
    float peak = 0, sum = 0;
    int inBin = 0;
    uint32_t bin = 0;
    for (;;) {
        int chs = 0;
        float** out = nullptr;
        int k = stb_vorbis_get_frame_float(v, &chs, &out);
        if (k <= 0) break;
        for (int i = 0; i < k; i++) {
            float l = out[0][i], r = chs > 1 ? out[1][i] : l;
            float al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
            float m = al > ar ? al : ar;
            if (m > peak) peak = m;
            sum += (l * l + r * r) * 0.5f;
            if (++inBin == B) {
                if (!sink->bin(sink->user, bin++, peak, __builtin_sqrtf(sum / B))) {
                    stb_vorbis_close(v);
                    free(ogg);
                    snprintf(why, whyCap, "cancelled");
                    return false;
                }
                peak = sum = 0;
                inBin = 0;
            }
        }
    }
    if (inBin) sink->bin(sink->user, bin++, peak, __builtin_sqrtf(sum / inBin));
    stb_vorbis_close(v);
    free(ogg);
    if (bin < 16) {
        snprintf(why, whyCap, "no audio decoded");
        return false;
    }
    return true;
}

// ---------- drawing ----------
// the look (the desktop test overrides them to compare)
#ifndef WAVE_BODY
#define WAVE_BODY 0.78f  // the song's loud level (99th percentile RMS), in parts of the half height
#define WAVE_CURVE 1.7f  // > 1 expands: quiet parts lower, so sections and beats stand apart
#define WAVE_CREST 0.8f  // how far the peak outline reaches past the body
#endif
float waveRef(const uint16_t* bins, uint32_t n) {
    if (!n) return 1;
    static uint32_t hist[4096];
    memset(hist, 0, sizeof hist);
    for (uint32_t i = 0; i < n; i++) hist[bins[2 * i + 1] >> 4]++;
    uint32_t want = n - n / 100, acc = 0;
    for (int k = 0; k < 4096; k++) {
        acc += hist[k];
        if (acc >= want) {
            float r = (k + 1) * 16 / 65535.f;
            return r > 0.01f ? r : 0.01f;
        }
    }
    return 1;
}

// x^k without libm (x > 0, small k): exp2(k * log2 x) from the float's exponent + short series
static float powk(float x, float k) {
    if (x <= 1e-6f) return 0;
    union {
        float f;
        uint32_t u;
    } v{x};
    int e = (int)((v.u >> 23) & 255) - 127;
    v.u = (v.u & 0x007fffff) | 0x3f800000;  // mantissa in [1, 2)
    float m = v.f, t = (m - 1) / (m + 1), t2 = t * t;
    float l2 = (e + 2.8853900f * t * (1 + t2 * (0.33333333f + t2 * 0.2f)));  // log2 x
    float y = k * l2;
    int yi = (int)__builtin_floorf(y);
    float yf = y - yi;
    float p = 1 + yf * (0.69314718f + yf * (0.24022651f + yf * (0.05550411f + yf * 0.00961813f)));  // 2^yf
    v.f = p;
    v.u += (uint32_t)(yi << 23);
    return v.f;
}

void waveRender(const WaveData& d, double t0, double t1, uint8_t* rgba, int w, int h, uint32_t core, float coreA,
                uint32_t edge, float edgeA) {
    memset(rgba, 0, (size_t)w * h * 4);
    if (!d.n || !d.rate || t1 <= t0 || w <= 0 || h <= 1) return;
    // heights in parts of the half height
    const float kBody = WAVE_BODY, kBodyMax = 0.9f, kEdgeMax = 0.96f, kCurve = WAVE_CURVE, kCrest = WAVE_CREST;
    double binsPerSec = (double)d.rate / WAVE_BIN, half = h / 2.0;
    uint8_t cr = core >> 16, cg = core >> 8, cb = core, er = edge >> 16, eg = edge >> 8, eb = edge;
    float inv = 1.f / (d.ref * 65535.f);
    float* hbs = (float*)malloc(sizeof(float) * (size_t)w * 4);
    if (!hbs) return;
    float *hes = hbs + w, *tmp = hbs + 2 * w;
    bool* has = (bool*)(hbs + 3 * w);
    // 1. per column: loudness over the column, but never less than ~12 ms (zoomed in, single 3 ms bins jitter)
    double colBins = (t1 - t0) * binsPerSec / w, minBins = binsPerSec * 0.012;
    for (int x = 0; x < w; x++) {
        double mid = (t0 + (t1 - t0) * (x + 0.5) / w) * binsPerSec, span = colBins > minBins ? colBins : minBins;
        long b0 = (long)(mid - span / 2), b1 = (long)(mid + span / 2 + 0.5);
        if (b1 <= b0) b1 = b0 + 1;
        if (b0 < 0) b0 = 0;
        if (b1 > (long)d.n) b1 = (long)d.n;
        has[x] = b0 < b1 && mid >= 0 && mid < d.n;
        hbs[x] = hes[x] = 0;
        if (!has[x]) continue;
        uint32_t pk = 0;
        double e = 0;
        for (long i = b0; i < b1; i++) {
            uint32_t p = d.bins[2 * i], r = d.bins[2 * i + 1];
            if (p > pk) pk = p;
            e += (double)r * r;
        }
        float rms = (float)__builtin_sqrt(e / (b1 - b0)) * inv, peak = pk * inv;
        // body: RMS on a slightly expanding curve (quiet / loud and the beats read clearly), the song's loud
        // level at kBody; outline: the body widened by the crest factor, so hits stand out without filling the bar
        float hb = kBody * powk(rms < 4 ? rms : 4, kCurve);
        if (hb > kBodyMax) hb = kBodyMax;
        float crest = rms > 1e-4f ? peak / rms : 1;
        float he = hb * (1 + kCrest * (powk(crest < 16 ? crest : 16, 0.6f) - 1));
        if (he < hb) he = hb;
        if (he > kEdgeMax) he = kEdgeMax;
        hbs[x] = hb;
        hes[x] = he;
    }
    // 2. smooth across columns (1 4 6 4 1): no stair steps or fuzz along the edges
    for (int pass = 0; pass < 2; pass++) {
        float* v = pass ? hes : hbs;
        for (int x = 0; x < w; x++) {
            float s = 0, wsum = 0;
            static const float k5[5] = {1, 4, 6, 4, 1};
            for (int j = -2; j <= 2; j++) {
                int xx = x + j;
                if (xx < 0 || xx >= w || !has[xx]) continue;
                s += v[xx] * k5[j + 2];
                wsum += k5[j + 2];
            }
            tmp[x] = wsum > 0 ? s / wsum : 0;
        }
        memcpy(v, tmp, sizeof(float) * (size_t)w);
    }
    for (int x = 0; x < w; x++) {
        if (!has[x]) continue;
        float hb = hbs[x], he = hes[x] < hb ? hb : hes[x];
        float yb = (float)(hb * half), ye = (float)(he * half);
        if (yb < 0.6f) yb = 0.6f;  // a hairline along the middle where the song is (also in silence)
        for (int y = 0; y < h; y++) {
            float dy = (float)((y + 0.5) - half);
            dy = dy < 0 ? -dy : dy;
            float cb_ = yb + 0.5f - dy, ce = ye + 0.5f - dy;  // pixel coverage of body / outline
            cb_ = cb_ < 0 ? 0 : cb_ > 1 ? 1 : cb_;
            ce = ce < 0 ? 0 : ce > 1 ? 1 : ce;
            if (ce <= 0) continue;
            uint8_t* o = rgba + ((size_t)y * w + x) * 4;
            // body over outline
            float ab = cb_ * coreA, ae = ce * edgeA * (1 - ab);
            float al = ab + ae;
            if (al <= 0) continue;
            o[0] = (uint8_t)((cr * ab + er * ae) / al + 0.5f);
            o[1] = (uint8_t)((cg * ab + eg * ae) / al + 0.5f);
            o[2] = (uint8_t)((cb * ab + eb * ae) / al + 0.5f);
            o[3] = (uint8_t)(al * 255 + 0.5f);
        }
    }
    free(hbs);
}

// ---------- reading a bundle: a plain file, or an entry stored in a zip (an APK) ----------
static bool readAt(WaveFile* f, uint64_t off, void* dst, size_t n) { return f->readAt(f->ctx, off, dst, n); }

uint8_t* waveReadBundle(WaveFile* f, const char* zipEntry, size_t* outN, char* why, int whyCap) {
    why[0] = 0;
    uint64_t off = 0, size = f->size;
    if (zipEntry && zipEntry[0]) {
        // end of central directory (+ zip64)
        const size_t kTail = 65557;
        size_t tn = f->size < kTail ? (size_t)f->size : kTail;
        uint8_t* tail = (uint8_t*)malloc(kTail);
        if (!tail || !readAt(f, f->size - tn, tail, tn)) {
            free(tail);
            return nullptr;
        }
        long e = -1;
        for (long i = (long)tn - 22; i >= 0; i--)
            if (le32(tail + i) == 0x06054b50) {
                e = i;
                break;
            }
        if (e < 0) {
            free(tail);
            snprintf(why, whyCap, "zip: no end record");
            return nullptr;
        }
        uint64_t cdOff = le32(tail + e + 16), cdSize = le32(tail + e + 12), count = le16(tail + e + 10);
        if ((cdOff == 0xffffffffu || count == 0xffff) && e >= 20 && le32(tail + e - 20) == 0x07064b50) {
            uint8_t z[56];
            if (!readAt(f, le64(tail + e - 20 + 8), z, sizeof z) || le32(z) != 0x06064b50) {
                free(tail);
                return nullptr;
            }
            count = le64(z + 32);
            cdSize = le64(z + 40);
            cdOff = le64(z + 48);
        }
        free(tail);
        if (cdSize > (64u << 20)) return nullptr;
        uint8_t* cd = (uint8_t*)malloc((size_t)cdSize);
        if (!cd || !readAt(f, cdOff, cd, (size_t)cdSize)) {
            free(cd);
            return nullptr;
        }
        size_t want = strlen(zipEntry), p = 0;
        bool found = false;
        int method = -1;
        uint64_t lho = 0, csz = 0;
        for (uint64_t i = 0; i < count && p + 46 <= cdSize; i++) {
            if (le32(cd + p) != 0x02014b50) break;
            size_t nl = le16(cd + p + 28), xl = le16(cd + p + 30), cl = le16(cd + p + 32);
            if (nl == want && p + 46 + nl <= cdSize && !memcmp(cd + p + 46, zipEntry, nl)) {
                method = le16(cd + p + 10);
                csz = le32(cd + p + 20);
                lho = le32(cd + p + 42);
                // zip64 extra: sizes / offset that did not fit
                const uint8_t* x = cd + p + 46 + nl;
                for (size_t k = 0; k + 4 <= xl;) {
                    size_t id = le16(x + k), len = le16(x + k + 2);
                    if (id == 1) {
                        size_t r = k + 4;
                        if (le32(cd + p + 24) == 0xffffffffu) r += 8;
                        if (csz == 0xffffffffu) csz = le64(x + r), r += 8;
                        if (lho == 0xffffffffu) lho = le64(x + r);
                    }
                    k += 4 + len;
                }
                found = true;
                break;
            }
            p += 46 + nl + xl + cl;
        }
        free(cd);
        if (!found) {
            snprintf(why, whyCap, "zip: %s not found", zipEntry);
            return nullptr;
        }
        if (method != 0) {
            snprintf(why, whyCap, "zip: entry compressed (%d)", method);
            return nullptr;
        }
        uint8_t lh[30];
        if (!readAt(f, lho, lh, sizeof lh) || le32(lh) != 0x04034b50) return nullptr;
        off = lho + 30 + le16(lh + 26) + le16(lh + 28);
        size = csz;
    }
    if (size < 64 || size > (256u << 20)) {
        snprintf(why, whyCap, "bundle size %llu", (unsigned long long)size);
        return nullptr;
    }
    uint8_t* b = (uint8_t*)malloc((size_t)size);
    if (!b || !readAt(f, off, b, (size_t)size)) {
        free(b);
        snprintf(why, whyCap, "read failed");
        return nullptr;
    }
    *outN = (size_t)size;
    return b;
}
