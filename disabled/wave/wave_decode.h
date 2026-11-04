// Music waveform decoding (wave_decode.cpp): no Unity calls, safe on a worker thread.
#pragma once

#define WAVE_BIN 128  // samples per bin

struct WaveSink {
    void* user;
    bool (*begin)(void* user, int rate, uint32_t samples);    // false = stop
    bool (*bin)(void* user, uint32_t i, float peak, float rms);  // both 0..1 of full scale; false = stop
};
// Decode a whole bundle (its bytes) into bins. why = the reason when it returns false.
bool waveDecode(const uint8_t* bundle, size_t n, WaveSink* sink, char* why, int whyCap);

// Bins as stored: bins[2i] = peak, bins[2i+1] = RMS, 0..65535 of full scale.
struct WaveData {
    const uint16_t* bins;
    uint32_t n;  // bins ready (decoding fills them from the start)
    int rate;
    float ref;   // loudness that reaches the "loud" height: 99th percentile of the RMS bins (waveRef)
};
float waveRef(const uint16_t* bins, uint32_t n);
// Draw clip time [t0, t1) into rgba (w x h, transparent background), mirrored around the middle row:
// a solid RMS body in `core` with a softer peak outline in `edge` (0xRRGGBB). Columns past the decoded part
// stay empty. The heights are fixed per song (not per view), so zooming never changes how loud a part looks.
void waveRender(const WaveData& d, double t0, double t1, uint8_t* rgba, int w, int h, uint32_t core, float coreA,
                uint32_t edge, float edgeA);

struct WaveFile {
    void* ctx;
    uint64_t size;
    bool (*readAt)(void* ctx, uint64_t off, void* dst, size_t n);
};
// The bundle's bytes (malloc'd): the whole file, or zipEntry inside it (an APK; the entry must be stored).
uint8_t* waveReadBundle(WaveFile* f, const char* zipEntry, size_t* outN, char* why, int whyCap);
