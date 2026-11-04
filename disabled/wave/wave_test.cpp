// Desktop check of wave_decode.cpp: decode one bundle (in an APK, or a plain file) and draw the bins.
//   zig c++ -O2 -DPM_WAVE_TEST -I../src wave_test.cpp ../src/wave_decode.cpp -o wave_test.exe
//   wave_test <apk|bundle> [zip entry] <out.pgm>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>
#include "wave_decode.h"

static bool readAt(void* ctx, uint64_t off, void* dst, size_t n) {
    FILE* f = (FILE*)ctx;
    if (_fseeki64(f, (long long)off, SEEK_SET)) return false;
    return fread(dst, 1, n, f) == n;
}

struct Acc {
    int rate = 0;
    uint32_t samples = 0;
    std::vector<float> peak, rms;
};
static bool onBegin(void* u, int rate, uint32_t samples) {
    Acc* a = (Acc*)u;
    a->rate = rate;
    a->samples = samples;
    return true;
}
static bool onBin(void* u, uint32_t i, float p, float r) {
    Acc* a = (Acc*)u;
    if (a->peak.size() <= i) a->peak.resize(i + 1), a->rms.resize(i + 1);
    a->peak[i] = p;
    a->rms[i] = r;
    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const char* entry = argc > 3 ? argv[2] : nullptr;
    const char* out = argv[argc - 1];
    FILE* f = fopen(argv[1], "rb");
    if (!f) return 3;
    _fseeki64(f, 0, SEEK_END);
    WaveFile wf{f, (uint64_t)_ftelli64(f), readAt};
    char why[200];
    clock_t t0 = clock();
    size_t n = 0;
    uint8_t* b = waveReadBundle(&wf, entry, &n, why, sizeof why);
    if (!b) {
        printf("read failed: %s\n", why);
        return 4;
    }
    clock_t t1 = clock();
    Acc a;
    WaveSink s{&a, onBegin, onBin};
    bool ok = waveDecode(b, n, &s, why, sizeof why);
    clock_t t2 = clock();
    printf("read %zu bytes in %.2fs, decode %s in %.2fs (%s): %d Hz, %u samples (%.1f s), %zu bins\n", n,
           (t1 - t0) / (double)CLOCKS_PER_SEC, ok ? "ok" : "FAILED", (t2 - t1) / (double)CLOCKS_PER_SEC, why, a.rate,
           a.samples, a.rate ? a.samples / (double)a.rate : 0, a.peak.size());
    if (!ok) return 5;
    // whole song, 2000 columns: peak max + rms power mean, linear
    const int W = 2000, H = 240;
    std::vector<unsigned char> img(W * H, 255);
    size_t nb = a.peak.size();
    for (int x = 0; x < W; x++) {
        size_t b0 = nb * x / W, b1 = nb * (x + 1) / W;
        if (b1 <= b0) b1 = b0 + 1;
        float p = 0, e = 0;
        for (size_t i = b0; i < b1; i++) {
            if (a.peak[i] > p) p = a.peak[i];
            e += a.rms[i] * a.rms[i];
        }
        float r = sqrtf(e / (b1 - b0));
        int hp = (int)(p * H / 2), hr = (int)(r * H / 2);
        for (int y = 0; y < H; y++) {
            int d = y - H / 2;
            d = d < 0 ? -d : d;
            if (d <= hr) img[y * W + x] = 60;
            else if (d <= hp) img[y * W + x] = 170;
        }
    }
    FILE* o = fopen(out, "wb");
    fprintf(o, "P5 %d %d 255\n", W, H);
    fwrite(img.data(), 1, img.size(), o);
    fclose(o);

    // what the practice editor draws (same code): whole song, 20 s, 4 s, 1 s, on the timeline's colour
    std::vector<uint16_t> bins(nb * 2);
    for (size_t i = 0; i < nb; i++) {
        bins[2 * i] = (uint16_t)(a.peak[i] > 1 ? 65535 : a.peak[i] * 65535);
        bins[2 * i + 1] = (uint16_t)(a.rms[i] > 1 ? 65535 : a.rms[i] * 65535);
    }
    WaveData d{bins.data(), (uint32_t)nb, a.rate, waveRef(bins.data(), (uint32_t)nb)};
    double len = a.samples / (double)a.rate, mid = len * 0.55;
    double spans[4][2] = {{0, len}, {mid - 10, mid + 10}, {mid - 2, mid + 2}, {mid - 0.5, mid + 0.5}};
    const int UW = 1620, UH = 80, PAD = 8, ROWH = UH + 2 * PAD;
    std::vector<unsigned char> ppm((size_t)UW * ROWH * 4 * 3, 0);
    std::vector<uint8_t> rgba((size_t)UW * UH * 4);
    for (int r = 0; r < 4; r++) {
        waveRender(d, spans[r][0], spans[r][1], rgba.data(), UW, UH, 0x8A8578, 0.95f, 0xBDB9AD, 0.95f);
        for (int y = 0; y < ROWH; y++)
            for (int x = 0; x < UW; x++) {
                unsigned char* px = &ppm[(((size_t)r * ROWH + y) * UW + x) * 3];
                px[0] = 0xED, px[1] = 0xEB, px[2] = 0xE3;
                int wy = y - PAD;
                if (wy < 0 || wy >= UH) continue;
                const uint8_t* s = &rgba[((size_t)wy * UW + x) * 4];
                float al = s[3] / 255.f;
                for (int c = 0; c < 3; c++) px[c] = (unsigned char)(px[c] * (1 - al) + s[c] * al + 0.5f);
            }
    }
    char ui[600];
    snprintf(ui, sizeof ui, "%s.ui.ppm", out);
    o = fopen(ui, "wb");
    fprintf(o, "P6 %d %d 255\n", UW, ROWH * 4);
    fwrite(ppm.data(), 1, (size_t)UW * ROWH * 4 * 3, o);
    fclose(o);
    printf("ref %.3f, ui preview %s\n", d.ref, ui);
    return 0;
}
