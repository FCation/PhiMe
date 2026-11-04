#include "pitch.h"

namespace pitch {

static const int kRing = 1 << 17;    // per-channel history (~2.7 s at 48 kHz): enough for 1/0.05 = 20x reads
static const int kGrain = 2048;       // output grain length (~43 ms)
static const int kMaxCh = 2;

static float g_ring[kMaxCh][kRing];
static long g_write = 0;               // frames written so far
static double g_grainStart[2];         // input position where each grain started reading
static int g_grainT[2] = {0, kGrain / 2};
static volatile float g_ratio = 1.f;   // read rate = 1 / speed
static volatile int g_resetReq = 1;  // first buffer initialises the grains
static float g_window[kGrain];
static bool g_init = false;

static void init() {
    for (int i = 0; i < kGrain; i++) {  // Hann: two half-overlapping grains sum to 1
        float x = (float)i / kGrain;
        // sin^2(pi x) without libm: use a 5th-order approximation of sin on [0, pi]
        float t = 3.14159265f * x;
        float t2 = t * t;
        float s = t * (1 - t2 / 6 * (1 - t2 / 20 * (1 - t2 / 42 * (1 - t2 / 72))));
        g_window[i] = s * s;
    }
    g_init = true;
}

void setSpeed(float speed) {
    if (speed < 0.05f) speed = 0.05f;
    g_ratio = 1.f / speed;
}

void reset() { g_resetReq = 1; }

static inline float readAt(int ch, double pos) {
    long i = (long)pos;
    float f = (float)(pos - i);
    float a = g_ring[ch][i & (kRing - 1)], b = g_ring[ch][(i + 1) & (kRing - 1)];
    return a + (b - a) * f;
}

// Latest start that keeps the whole grain inside data we already have when it is read.
static inline double newGrainStart(float ratio) {
    double lag = ratio > 1 ? (double)kGrain * (ratio - 1) : 0;
    return (double)g_write - lag - 4;
}

void process(float* data, int frames, int channels) {
    if (!g_init) init();
    if (channels > kMaxCh) return;  // unexpected layout: leave the audio untouched
    float ratio = g_ratio;
    if (g_resetReq) {
        g_resetReq = 0;
        g_grainT[0] = 0;
        g_grainT[1] = kGrain / 2;
        g_grainStart[0] = g_grainStart[1] = newGrainStart(ratio);
    }
    bool bypass = ratio > 0.99f && ratio < 1.01f;
    for (int f = 0; f < frames; f++) {
        for (int c = 0; c < channels; c++) g_ring[c][g_write & (kRing - 1)] = data[f * channels + c];
        g_write++;
        if (bypass) continue;
        float out[kMaxCh] = {0, 0};
        for (int g = 0; g < 2; g++) {
            if (g_grainT[g] >= kGrain) {
                g_grainT[g] = 0;
                g_grainStart[g] = newGrainStart(ratio);
            }
            double pos = g_grainStart[g] + (double)g_grainT[g] * ratio;
            if (pos > (double)g_write - 2) pos = (double)g_write - 2;  // never read the future
            float w = g_window[g_grainT[g]];
            for (int c = 0; c < channels; c++) out[c] += w * readAt(c, pos);
            g_grainT[g]++;
        }
        for (int c = 0; c < channels; c++) data[f * channels + c] = out[c];
    }
}

}  // namespace pitch
