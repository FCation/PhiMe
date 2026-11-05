#include "perf.h"

struct TimeSpec {
    long sec, nsec;
};
static int (*g_clock)(int, TimeSpec*) = nullptr;
static bool g_clockTried = false;
#if defined(__APPLE__)
static const int kMonotonic = 6;  // CLOCK_MONOTONIC on Darwin
#else
static const int kMonotonic = 1;  // CLOCK_MONOTONIC on Linux / Android
#endif

uint64_t perfNow() {
    if (!g_clockTried) {
        g_clockTried = true;
        g_clock = (int (*)(int, TimeSpec*))dlsym(PM_RTLD_DEFAULT, "clock_gettime");  // no link stub needed
    }
    TimeSpec t{0, 0};
    if (!g_clock || g_clock(kMonotonic, &t) != 0) return 0;
    return (uint64_t)t.sec * 1000000000ull + (uint64_t)t.nsec;
}

static uint64_t g_cur[PF_COUNT], g_sum[PF_COUNT], g_max[PF_COUNT];
static uint64_t g_winStart = 0, g_lastFrame = 0, g_worst = 0;
static int g_frames = 0;

void perfAdd(int slot, uint64_t ns) {
    if (slot >= 0 && slot < PF_COUNT && ns < 10000000000ull) g_cur[slot] += ns;
}

void perfFrame() {
    uint64_t now = perfNow();
    if (!now) return;
    if (g_lastFrame && now - g_lastFrame > g_worst) g_worst = now - g_lastFrame;
    g_lastFrame = now;
    for (int i = 0; i < PF_COUNT; i++) {
        g_sum[i] += g_cur[i];
        if (g_cur[i] > g_max[i]) g_max[i] = g_cur[i];
        g_cur[i] = 0;
    }
    g_frames++;
    if (!g_winStart) g_winStart = now;
    if (now - g_winStart < 10000000000ull) return;
    // only while a song runs (the game's note update took time in this window)
    if (g_sum[PF_GAME_NOTES] > 0 && g_frames > 0) {
        static const char* const names[PF_COUNT] = {"game notes", "pass2", "critical", "skin scan", "hit fx",
                                                     "mask", "hud", "instantiate", "callbacks"};
        char b[600];
        double secs = (now - g_winStart) / 1e9;
        int n = snprintf(b, sizeof b, "perf: %.1f s, %d frames (%.1f fps, worst %.0f ms); avg/max us per frame:", secs,
                         g_frames, g_frames / secs, g_worst / 1e6);
        for (int i = 0; i < PF_COUNT && n < (int)sizeof b - 40; i++)
            n += snprintf(b + n, sizeof b - n, " %s %d/%d%s", names[i], (int)(g_sum[i] / g_frames / 1000),
                          (int)(g_max[i] / 1000), i + 1 < PF_COUNT ? "," : "");
        pm_log("%s", b);
    }
    for (int i = 0; i < PF_COUNT; i++) g_sum[i] = g_max[i] = 0;
    g_frames = 0;
    g_worst = 0;
    g_winStart = now;
}
