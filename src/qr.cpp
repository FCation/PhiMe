// A small QR code encoder (byte mode, error correction M, versions 1..10), enough for the TapTap login link.
// A port of android/picker/src/phime/Qr.java (which follows Project Nayuki's reference encoder), used by the iOS
// login. qrEncode returns a malloc'd "N:" + N*N chars of '0' / '1', row by row from the top, '1' = dark (null if
// the text does not fit).
#ifdef QR_HOST_TEST
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include "platform.h"
#endif

namespace {

const int ECC_PER_BLOCK[11] = {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};
const int BLOCKS[11] = {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};
const int kMax = 10 * 4 + 17;

struct Qr {
    int ver, size;
    bool dark[kMax][kMax], fixed[kMax][kMax];

    static int rawModules(int v) {
        int r = (16 * v + 128) * v + 64;
        if (v >= 2) {
            int n = v / 7 + 2;
            r -= (25 * n - 10) * n - 55;
            if (v >= 7) r -= 36;
        }
        return r;
    }
    static int dataCodewords(int v) { return rawModules(v) / 8 - ECC_PER_BLOCK[v] * BLOCKS[v]; }
    static void put(uint8_t* out, int& pos, int val, int bits) {
        for (int i = bits - 1; i >= 0; i--) {
            if ((val >> i) & 1) out[pos >> 3] |= (uint8_t)(0x80 >> (pos & 7));
            pos++;
        }
    }
    void set(int x, int y, bool d) {
        dark[y][x] = d;
        fixed[y][x] = true;
    }
    void finder(int cx, int cy) {
        for (int dy = -4; dy <= 4; dy++)
            for (int dx = -4; dx <= 4; dx++) {
                int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= size || y >= size) continue;
                int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, d = ax > ay ? ax : ay;
                set(x, y, d != 2 && d != 4);
            }
    }
    int alignPositions(int* r) {
        if (ver == 1) return 0;
        int n = ver / 7 + 2;
        int step = (ver * 8 + n * 3 + 5) / (n * 4 - 4) * 2;
        r[0] = 6;
        for (int i = n - 1, p = size - 7; i >= 1; i--, p -= step) r[i] = p;
        return n;
    }
    static bool bit(int x, int i) { return ((x >> i) & 1) != 0; }
    void format(int m) {
        int data = 0 << 3 | m, rem = data;  // level M = 0
        for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        int bits = (data << 10 | rem) ^ 0x5412;
        for (int i = 0; i <= 5; i++) set(8, i, bit(bits, i));
        set(8, 7, bit(bits, 6));
        set(8, 8, bit(bits, 7));
        set(7, 8, bit(bits, 8));
        for (int i = 9; i < 15; i++) set(14 - i, 8, bit(bits, i));
        for (int i = 0; i < 8; i++) set(size - 1 - i, 8, bit(bits, i));
        for (int i = 8; i < 15; i++) set(8, size - 15 + i, bit(bits, i));
        set(8, size - 8, true);
    }
    void version() {
        if (ver < 7) return;
        int rem = ver;
        for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        int bits = ver << 12 | rem;
        for (int i = 0; i < 18; i++) {
            bool b = bit(bits, i);
            int a = size - 11 + i % 3, c = i / 3;
            set(a, c, b);
            set(c, a, b);
        }
    }
    static int mul(int x, int y) {
        int z = 0;
        for (int i = 7; i >= 0; i--) {
            z = (z << 1) ^ ((z >> 7) * 0x11D);
            z ^= ((y >> i) & 1) * x;
        }
        return z & 0xFF;
    }
    static void rsDivisor(int degree, uint8_t* r) {
        memset(r, 0, degree);
        r[degree - 1] = 1;
        int root = 1;
        for (int i = 0; i < degree; i++) {
            for (int j = 0; j < degree; j++) {
                r[j] = (uint8_t)mul(r[j], root);
                if (j + 1 < degree) r[j] ^= r[j + 1];
            }
            root = mul(root, 0x02);
        }
    }
    static void rsRemainder(const uint8_t* data, int n, const uint8_t* div, int degree, uint8_t* r) {
        memset(r, 0, degree);
        for (int k = 0; k < n; k++) {
            int f = (data[k] ^ r[0]) & 0xFF;
            for (int i = 0; i + 1 < degree; i++) r[i] = r[i + 1];
            r[degree - 1] = 0;
            for (int i = 0; i < degree; i++) r[i] ^= (uint8_t)mul(div[i], f);
        }
    }
    // data codewords -> all codewords with error correction, interleaved
    void interleave(const uint8_t* data, uint8_t* out) {
        int nb = BLOCKS[ver], ecc = ECC_PER_BLOCK[ver], raw = rawModules(ver) / 8;
        int nShort = nb - raw % nb, shortLen = raw / nb;
        uint8_t div[32], blocks[5][160];
        rsDivisor(ecc, div);
        for (int i = 0, k = 0; i < nb; i++) {
            int len = shortLen - ecc + (i < nShort ? 0 : 1);
            memset(blocks[i], 0, sizeof blocks[i]);
            memcpy(blocks[i], data + k, len);
            uint8_t e[32];
            rsRemainder(data + k, len, div, ecc, e);
            k += len;
            memcpy(blocks[i] + shortLen + 1 - ecc, e, ecc);
        }
        int o = 0;
        for (int i = 0; i < shortLen + 1; i++)
            for (int j = 0; j < nb; j++)
                if (i != shortLen - ecc || j >= nShort) out[o++] = blocks[j][i];
    }
    void mask(int m) {
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                if (fixed[y][x]) continue;
                bool inv;
                switch (m) {
                case 0: inv = (x + y) % 2 == 0; break;
                case 1: inv = y % 2 == 0; break;
                case 2: inv = x % 3 == 0; break;
                case 3: inv = (x + y) % 3 == 0; break;
                case 4: inv = (x / 3 + y / 2) % 2 == 0; break;
                case 5: inv = x * y % 2 + x * y % 3 == 0; break;
                case 6: inv = (x * y % 2 + x * y % 3) % 2 == 0; break;
                default: inv = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                }
                if (inv) dark[y][x] = !dark[y][x];
            }
    }
    long penalty() {
        long s = 0;
        for (int pass = 0; pass < 2; pass++)
            for (int a = 0; a < size; a++) {
                int run = 0;
                bool last = false;
                for (int b = 0; b < size; b++) {
                    bool d = pass == 0 ? dark[a][b] : dark[b][a];
                    if (b > 0 && d == last) {
                        run++;
                        if (run == 5) s += 3;
                        else if (run > 5) s++;
                    } else {
                        run = 1;
                        last = d;
                    }
                }
            }
        int nDark = 0;
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                if (dark[y][x]) nDark++;
                if (x + 1 < size && y + 1 < size) {
                    bool d = dark[y][x];
                    if (d == dark[y][x + 1] && d == dark[y + 1][x] && d == dark[y + 1][x + 1]) s += 3;
                }
            }
        int total = size * size, diff = nDark * 20 - total * 10;
        if (diff < 0) diff = -diff;
        int k = (diff + total - 1) / total - 1;
        if (k > 0) s += k * 10;
        return s;
    }
    void build(const uint8_t* text, int n) {
        size = ver * 4 + 17;
        memset(dark, 0, sizeof dark);
        memset(fixed, 0, sizeof fixed);
        // the bit stream
        int cap = dataCodewords(ver);
        uint8_t cw[300];
        memset(cw, 0, sizeof cw);
        int pos = 0;
        put(cw, pos, 4, 4);
        put(cw, pos, n, ver <= 9 ? 8 : 16);
        for (int i = 0; i < n; i++) put(cw, pos, text[i], 8);
        int left = cap * 8 - pos;
        put(cw, pos, 0, left < 4 ? left : 4);
        pos = (pos + 7) / 8 * 8;
        for (int i = pos / 8, p = 0; i < cap; i++, p ^= 1) cw[i] = p == 0 ? 0xEC : 0x11;
        // function patterns
        for (int i = 0; i < size; i++) {
            set(6, i, i % 2 == 0);
            set(i, 6, i % 2 == 0);
        }
        finder(3, 3);
        finder(size - 4, 3);
        finder(3, size - 4);
        int al[8];
        int na = alignPositions(al);
        for (int i = 0; i < na; i++)
            for (int j = 0; j < na; j++) {
                if ((i == 0 && j == 0) || (i == 0 && j == na - 1) || (i == na - 1 && j == 0)) continue;
                for (int dy = -2; dy <= 2; dy++)
                    for (int dx = -2; dx <= 2; dx++) {
                        int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
                        set(al[i] + dx, al[j] + dy, (ax > ay ? ax : ay) != 1);
                    }
            }
        format(0);  // reserves the area; drawn again with the chosen mask
        version();
        // codewords, zigzag
        uint8_t all[400];
        interleave(cw, all);
        int total = rawModules(ver) / 8, i = 0;
        for (int right = size - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;
            for (int vert = 0; vert < size; vert++)
                for (int j = 0; j < 2; j++) {
                    int x = right - j;
                    bool up = ((right + 1) & 2) == 0;
                    int y = up ? size - 1 - vert : vert;
                    if (!fixed[y][x] && i < total * 8) {
                        dark[y][x] = ((all[i >> 3] >> (7 - (i & 7))) & 1) != 0;
                        i++;
                    }
                }
        }
        // the mask with the lowest penalty
        int best = 0;
        long bestScore = 0x7fffffffL;
        for (int m = 0; m < 8; m++) {
            mask(m);
            format(m);
            long sc = penalty();
            if (sc < bestScore) {
                bestScore = sc;
                best = m;
            }
            mask(m);  // undo (xor)
        }
        mask(best);
        format(best);
    }
};

}  // namespace

char* qrEncode(const char* text) {
    int n = (int)strlen(text);
    for (int v = 1; v <= 10; v++) {
        int need = 4 + (v <= 9 ? 8 : 16) + n * 8;
        if (need > Qr::dataCodewords(v) * 8) continue;
        Qr* q = (Qr*)malloc(sizeof(Qr));
        if (!q) return nullptr;
        q->ver = v;
        q->build((const uint8_t*)text, n);
        int s = q->size;
        char* out = (char*)malloc((size_t)s * s + 8);
        if (out) {
            int p = snprintf(out, 8, "%d:", s);
            for (int y = 0; y < s; y++)
                for (int x = 0; x < s; x++) out[p++] = q->dark[y][x] ? '1' : '0';
            out[p] = 0;
        }
        free(q);
        return out;
    }
    return nullptr;
}
