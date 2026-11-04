#include "../src/audiodec.h"
#include <math.h>

namespace {

int failures = 0, cases = 0;
void check(bool ok, const char* name) {
    ++cases;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
void set16(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void set32(uint8_t* p, uint32_t v) { set16(p, v); set16(p + 2, v >> 16); }
uint32_t get32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
struct Buffer {
    uint8_t* data = nullptr;
    uint32_t size = 0;
    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    ~Buffer() { free(data); }
    void resize(uint32_t n) {
        free(data);
        data = static_cast<uint8_t*>(calloc(n ? n : 1, 1));
        if (!data) { fprintf(stderr, "test allocation failed\n"); exit(99); }
        size = n;
    }
    void copy(const Buffer& b) { resize(b.size); memcpy(data, b.data, size); }
};
void wav(Buffer& b, int format, int bits, int channels, int rate, uint32_t frames) {
    uint32_t bytes = frames * channels * (bits / 8);
    b.resize(44 + bytes + (bytes & 1));
    memcpy(b.data, "RIFF", 4); set32(b.data + 4, b.size - 8);
    memcpy(b.data + 8, "WAVEfmt ", 8); set32(b.data + 16, 16);
    set16(b.data + 20, format); set16(b.data + 22, channels);
    set32(b.data + 24, rate); set32(b.data + 28, rate * channels * (bits / 8));
    set16(b.data + 32, channels * (bits / 8)); set16(b.data + 34, bits);
    memcpy(b.data + 36, "data", 4); set32(b.data + 40, bytes);
}
bool zero(const PcmAudio& a) { return !a.samples && !a.frames && !a.channels && !a.rate; }
bool errorCase(const uint8_t* p, uint32_t n, AudioError expected) {
    PcmAudio a = {nullptr, 99, 8, 123};
    AudioError err = AUE_OK;
    char detail[80] = {};
    bool ok = !audioDecode(p, n, &a, &err, detail, sizeof(detail)) && err == expected &&
              zero(a) && detail[0] && detail[sizeof(detail) - 1] == 0;
    if (!ok) printf("  wanted %s, got %s (%s)\n", audioErrorName(expected), audioErrorName(err), detail);
    audioFree(&a); audioFree(&a);
    return ok && zero(a);
}
bool matches(const Buffer& b, const int16_t* expected, uint32_t frames, int channels, int rate, int tolerance = 0) {
    PcmAudio a = {};
    AudioError err = AUE_CORRUPT;
    char detail[128];
    bool ok = audioDecode(b.data, b.size, &a, &err, detail, sizeof(detail));
    ok = ok && err == AUE_OK && !detail[0] && a.frames == frames && a.channels == channels && a.rate == rate;
    if (ok) {
        for (uint32_t i = 0; i < frames * channels; ++i) {
            int delta = int(a.samples[i]) - expected[i];
            if (delta < -tolerance || delta > tolerance) {
                printf("  sample %u: wanted %d, got %d\n", i, expected[i], a.samples[i]);
                ok = false; break;
            }
        }
    } else printf("  %s: %s (frames=%u, channels=%d, rate=%d)\n", audioErrorName(err), detail, a.frames, a.channels, a.rate);
    audioFree(&a); audioFree(&a);
    return ok && zero(a);
}

void wavTests() {
    Buffer b;
    const uint32_t frames = 4410;
    int16_t sine[frames];
    wav(b, 1, 16, 1, 44100, frames);
    for (uint32_t i = 0; i < frames; ++i) {
        sine[i] = int16_t(20000 * sin(i * 2 * 3.141592653589793 * 440 / 44100));
        set16(b.data + 44 + i * 2, uint16_t(sine[i]));
    }
    check(matches(b, sine, frames, 1, 44100), "WAV 16-bit mono 44.1 kHz sine");

    wav(b, 1, 8, 2, 48000, 3);
    const uint8_t eight[] = {0, 128, 255, 64, 192, 1};
    const int16_t eightExpected[] = {-32768, 0, 32512, -16384, 16384, -32512};
    memcpy(b.data + 44, eight, sizeof(eight));
    check(matches(b, eightExpected, 3, 2, 48000), "WAV unsigned 8-bit stereo");

    wav(b, 1, 24, 1, 8000, 5);
    const uint32_t raw24[] = {0x800000, 0xffffff, 0, 0x123400, 0x7fffff};
    const int16_t expected24[] = {-32768, 0, 0, 0x1234, 32767};
    for (unsigned i = 0; i < 5; ++i) {
        set16(b.data + 44 + i * 3, raw24[i]);
        b.data[46 + i * 3] = uint8_t(raw24[i] >> 16);
    }
    check(matches(b, expected24, 5, 1, 8000), "WAV signed 24-bit and odd chunk padding");

    wav(b, 1, 32, 2, 192000, 3);
    const uint32_t raw32[] = {0x80000000u, 0xffffffffu, 0, 0x12340000, 0x7fffffffu, 0xc0000000u};
    const int16_t expected32[] = {-32768, 0, 0, 0x1234, 32767, -16384};
    for (unsigned i = 0; i < 6; ++i) set32(b.data + 44 + i * 4, raw32[i]);
    check(matches(b, expected32, 3, 2, 192000), "WAV signed 32-bit stereo at maximum rate");

    wav(b, 3, 32, 1, 44100, 10);
    const float floating[] = {-1.0f, -0.5f, 0, 0.5f, 1.0f, 2.0f, -2.0f, NAN, INFINITY, -INFINITY};
    const int16_t floatExpected[] = {-32768, -16384, 0, 16384, 32767, 32767, -32768, 0, 32767, -32768};
    for (unsigned i = 0; i < 10; ++i) {
        uint32_t raw; memcpy(&raw, floating + i, 4); set32(b.data + 44 + i * 4, raw);
    }
    check(matches(b, floatExpected, 10, 1, 44100), "WAV 32-bit float, clipping, NaN and infinity");

    wav(b, 1, 16, 6, 48000, 2);
    const int16_t six[] = {3000, -3000, 6000, -6000, 9000, -9000, 32767, -32768, 32767, -32768, 32767, -32768};
    const int16_t sixExpected[] = {6000, -6000, 32767, -32768};
    for (unsigned i = 0; i < 12; ++i) set16(b.data + 44 + i * 2, uint16_t(six[i]));
    check(matches(b, sixExpected, 2, 2, 48000), "WAV six channels average to stereo");
    for (int channels = 3; channels <= 8; ++channels) {
        wav(b, 1, 16, channels, 44100, 1);
        for (int c = 0; c < channels; ++c) set16(b.data + 44 + c * 2, uint16_t(c & 1 ? -9000 : 9000));
        const int16_t expected[] = {9000, -9000};
        char name[80]; snprintf(name, sizeof(name), "WAV %d-channel stereo lane normalization", channels);
        check(matches(b, expected, 1, 2, 44100), name);
    }

    wav(b, 1, 8, 1, 8000, 160000);
    memset(b.data + 44, 128, 160000);
    PcmAudio a = {};
    check(audioDecode(b.data, b.size, &a, nullptr, nullptr, 0) && a.frames == 160000,
          "Exactly 20 seconds is accepted");
    audioFree(&a);
    wav(b, 1, 8, 1, 8000, 160001);
    check(errorCase(b.data, b.size, AUE_TOO_LONG), "20 seconds plus one frame is rejected");

    wav(b, 1, 16, 1, 44100, 1);
    set32(b.data + 40, 400);
    check(errorCase(b.data, b.size, AUE_CORRUPT), "WAV lying data size");
    wav(b, 1, 16, 1, 44100, 1);
    set32(b.data + 40, 44100u * 3600 * 2);
    set32(b.data + 4, 36 + 44100u * 3600 * 2);
    check(errorCase(b.data, b.size, AUE_TOO_LONG), "WAV claiming one hour rejects before PCM allocation");
    wav(b, 2, 16, 1, 44100, 1);
    check(errorCase(b.data, b.size, AUE_UNSUPPORTED), "WAV ADPCM is unsupported");
    wav(b, 1, 16, 1, 7999, 1);
    check(errorCase(b.data, b.size, AUE_UNSUPPORTED), "WAV rate below minimum");
    wav(b, 1, 16, 1, 192001, 1);
    check(errorCase(b.data, b.size, AUE_UNSUPPORTED), "WAV rate above maximum");
    wav(b, 1, 16, 9, 44100, 1);
    check(errorCase(b.data, b.size, AUE_UNSUPPORTED), "WAV more than eight channels");
    wav(b, 1, 16, 1, 44100, 1);
    set16(b.data + 32, 0);
    check(errorCase(b.data, b.size, AUE_CORRUPT), "WAV zero block alignment");
    wav(b, 1, 16, 1, 44100, 1);
    set32(b.data + 28, 0);
    check(errorCase(b.data, b.size, AUE_CORRUPT), "WAV inconsistent byte rate");
    wav(b, 1, 16, 1, 44100, 1);
    set32(b.data + 40, 1);
    check(errorCase(b.data, b.size, AUE_CORRUPT), "WAV partial sample frame");
    wav(b, 1, 16, 1, 44100, 1);
    bool truncated = true;
    for (uint32_t n = 4; n < b.size; ++n) truncated &= errorCase(b.data, n, AUE_CORRUPT);
    check(truncated, "Every truncated prefix of a small WAV fails cleanly");

    // Unknown odd-sized chunk, then data before fmt.
    Buffer original; wav(original, 1, 16, 1, 44100, 1); set16(original.data + 44, 1234);
    b.resize(original.size + 10);
    memcpy(b.data, original.data, 12); set32(b.data + 4, b.size - 8);
    memcpy(b.data + 12, "JUNK", 4); set32(b.data + 16, 1); b.data[20] = 42;
    memcpy(b.data + 22, original.data + 36, 10);
    memcpy(b.data + 32, original.data + 12, 24);
    const int16_t one[] = {1234};
    check(matches(b, one, 1, 1, 44100), "WAV unknown padded chunk and data before fmt");
}

bool load(Buffer& b, const char* directory, const char* name) {
    char path[2048]; snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long n = ftell(file);
    if (!ok || n <= 0 || n > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) { fclose(file); return false; }
    b.resize(uint32_t(n));
    ok = fread(b.data, 1, b.size, file) == b.size;
    fclose(file);
    return ok;
}
uint32_t pageSize(const uint8_t* p) {
    uint32_t n = 27 + p[26];
    for (unsigned i = 0; i < p[26]; ++i) n += p[27 + i];
    return n;
}
void fixCrc(uint8_t* page) {
    uint32_t crc = 0, n = pageSize(page);
    set32(page + 22, 0);
    for (uint32_t i = 0; i < n; ++i) {
        crc ^= uint32_t(page[i]) << 24;
        for (int bit = 0; bit < 8; ++bit) crc = (crc << 1) ^ ((crc & 0x80000000u) ? 0x04c11db7u : 0);
    }
    set32(page + 22, crc);
}
void oggTests(const char* directory) {
    const char* names[] = {"click.ogg", "drag.ogg", "flick.ogg"};
    Buffer click;
    for (const char* name : names) {
        Buffer b;
        bool loaded = load(b, directory, name);
        check(loaded, name);
        if (!loaded) continue;
        PcmAudio a = {};
        AudioError err;
        char detail[128];
        bool ok = audioDecode(b.data, b.size, &a, &err, detail, sizeof(detail));
        if (!ok) printf("  %s: %s\n", audioErrorName(err), detail);
        ok = ok && err == AUE_OK && (a.rate == 44100 || a.rate == 48000) && a.channels >= 1 && a.channels <= 2 &&
             a.frames > a.rate * 0.01 && a.frames < unsigned(a.rate * 5);
        bool nonzero = false;
        if (a.samples) for (uint32_t i = 0; i < a.frames * a.channels; ++i) nonzero |= a.samples[i] != 0;
        char label[100]; snprintf(label, sizeof(label), "%s decodes valid, nonzero PCM", name);
        check(ok && nonzero, label);
        audioFree(&a); check(zero(a), "audioFree clears decoded Ogg");
        if (!strcmp(name, "click.ogg")) click.copy(b);
    }
    if (!click.data) return;
    check(errorCase(click.data, click.size / 2, AUE_CORRUPT), "Half of click.ogg is corrupt");
    check(errorCase(click.data, click.size - 1, AUE_CORRUPT), "Ogg missing final byte is corrupt");
    check(errorCase(click.data, pageSize(click.data), AUE_CORRUPT), "Ogg truncated exactly at a page boundary");
    bool prefixes = true;
    for (uint32_t n = 4; n < 64 && n < click.size; ++n) prefixes &= errorCase(click.data, n, AUE_CORRUPT);
    check(prefixes, "Truncated Ogg headers fail cleanly");
    Buffer b; b.copy(click); b.data[b.size - 1] ^= 1;
    check(errorCase(b.data, b.size, AUE_CORRUPT), "Ogg packet corruption is detected by CRC");
    b.copy(click); uint32_t last = 0;
    for (uint32_t pos = 0; pos < b.size; pos += pageSize(b.data + pos)) last = pos;
    uint32_t rate = get32(b.data + 28 + 12);
    set32(b.data + last + 6, rate * 21); set32(b.data + last + 10, 0); fixCrc(b.data + last);
    check(errorCase(b.data, b.size, AUE_TOO_LONG), "Ogg excessive granule duration rejected before decoding");
    b.copy(click); set32(b.data + 28 + 12, 7999); fixCrc(b.data);
    check(errorCase(b.data, b.size, AUE_UNSUPPORTED), "Ogg unsupported sample rate with valid CRC");
    b.copy(click); b.data[28 + 11] = 9; fixCrc(b.data);
    check(errorCase(b.data, b.size, AUE_UNSUPPORTED), "Ogg unsupported channels with valid CRC");
    b.copy(click); uint32_t second = pageSize(b.data); b.data[second + 27 + b.data[second + 26]] ^= 0xff;
    fixCrc(b.data + second);
    check(errorCase(b.data, b.size, AUE_CORRUPT), "Ogg malformed Vorbis header with valid page CRC");
    bool repeat = true;
    for (int i = 0; i < 32; ++i) {
        PcmAudio a = {};
        repeat &= audioDecode(click.data, click.size, &a, nullptr, nullptr, 0);
        audioFree(&a);
    }
    check(repeat, "Repeated Ogg decoding and cleanup");
}

} // namespace

int main(int argc, char** argv) {
    const uint8_t garbage[] = {17, 91, 0, 255, 31, 2, 18};
    check(errorCase(nullptr, 0, AUE_CORRUPT), "Empty input");
    check(errorCase(garbage, sizeof(garbage), AUE_UNKNOWN_FORMAT), "Random bytes");
    const uint8_t oggGarbage[] = {'O', 'g', 'g', 'S', 0, 1, 2, 3};
    check(errorCase(oggGarbage, sizeof(oggGarbage), AUE_CORRUPT), "OggS followed by garbage");
    check(errorCase(garbage, 16u * 1024 * 1024 + 1, AUE_TOO_LONG), "Oversized input rejected before reading bytes");
    AudioError err;
    char shortDetail[2] = {'x', 'y'};
    check(!audioDecode(garbage, sizeof(garbage), nullptr, &err, shortDetail, 1) &&
          err == AUE_CORRUPT && !shortDetail[0] && shortDetail[1] == 'y', "Null output and bounded detail");
    PcmAudio a = {};
    shortDetail[0] = 'x';
    check(!audioDecode(garbage, sizeof(garbage), &a, nullptr, shortDetail, -1) && shortDetail[0] == 'x',
          "Optional error and nonpositive detail capacity");
    audioFree(nullptr);
    const char* errorNames[] = {"AUE_OK", "AUE_UNKNOWN_FORMAT", "AUE_CORRUPT", "AUE_UNSUPPORTED", "AUE_TOO_LONG", "AUE_NO_MEMORY"};
    bool names = true;
    for (int i = 0; i < 6; ++i) names &= !strcmp(audioErrorName(AudioError(i)), errorNames[i]);
    check(names, "Audio error names");
    wavTests();
    check(argc == 2, "Ogg fixture directory provided");
    if (argc == 2) oggTests(argv[1]);
    printf("%d cases, %d failures\n", cases, failures);
    return failures;
}
