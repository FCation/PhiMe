#include "audiodec.h"

extern "C" {
void qsort(void* base, size_t count, size_t width, int (*compare)(const void*, const void*));
double exp(double x);
double log(double x);
double pow(double x, double y);
double ldexp(double x, int exponent);
double sin(double x);
double cos(double x);
}

namespace {

// Ogg CRC without a shared, lazily initialized table.
static void crc32_init() {}
static uint32_t crc32_update(uint32_t crc, uint8_t byte) {
    crc ^= uint32_t(byte) << 24;
    for (int i = 0; i < 8; ++i)
        crc = (crc << 1) ^ ((0u - (crc >> 31)) & 0x04c11db7u);
    return crc;
}

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION
#define STB_VORBIS_NO_CRT_HEADERS
#define STB_VORBIS_EXTERNAL_CRC
#define STB_VORBIS_MAX_CHANNELS 8
#define alloca __builtin_alloca
#define assert(x) ((void)0)
#define floor __builtin_floor
#define abs __builtin_abs
#ifndef NULL
#define NULL 0
#define AUDIODEC_DEFINED_NULL
#endif
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wtautological-compare"
#include "third_party/stb_vorbis.c"
#pragma clang diagnostic pop
#undef floor
#undef abs
#undef assert
#undef alloca
#ifdef AUDIODEC_DEFINED_NULL
#undef NULL
#undef AUDIODEC_DEFINED_NULL
#endif

static const uint32_t kMaxInput = 16u * 1024 * 1024;
static uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
static uint32_t le32(const uint8_t* p) { return le16(p) | (uint32_t(le16(p + 2)) << 16); }
static uint64_t le64(const uint8_t* p) { return le32(p) | (uint64_t(le32(p + 4)) << 32); }
static bool fits(uint32_t pos, uint32_t n, uint32_t end) { return pos <= end && n <= end - pos; }
static bool fail(AudioError* err, char* detail, int cap, AudioError e, const char* why) {
    if (err) *err = e;
    if (detail && cap > 0) snprintf(detail, size_t(cap), "%s", why);
    return false;
}
static int16_t pcm16(double x) {
    if (x != x) return 0;
    if (x >= 32767.0 / 32768.0) return 32767;
    if (x <= -1.0) return -32768;
    double scaled = x * 32768.0;
    return int16_t(scaled >= 0 ? scaled + 0.5 : scaled - 0.5);
}
static void mix(const double* input, int channels, int16_t* output) {
    if (channels <= 2) {
        for (int c = 0; c < channels; ++c) output[c] = pcm16(input[c]);
        return;
    }
    double left = 0, right = 0;
    for (int c = 0; c < channels; ++c) {
        if (c & 1) right += input[c];
        else left += input[c];
    }
    output[0] = pcm16(left / ((channels + 1) / 2));
    output[1] = pcm16(right / (channels / 2));
}

static bool decodeWav(const uint8_t* data, uint32_t size, PcmAudio* out,
                      AudioError* err, char* detail, int cap) {
    if (size < 12 || memcmp(data + 8, "WAVE", 4))
        return fail(err, detail, cap, AUE_CORRUPT, "Invalid RIFF/WAVE header");
    uint32_t riffSize = le32(data + 4);
    if (riffSize < 4)
        return fail(err, detail, cap, AUE_CORRUPT, "Invalid RIFF size");
    // Parse available headers first: a long claimed duration must not allocate PCM.
    uint32_t end = riffSize <= size - 8 ? riffSize + 8 : size;
    uint32_t payload = 0, bytes = 0, rate = 0, byteRate = 0;
    int format = 0, channels = 0, bits = 0, block = 0;
    bool haveFmt = false, haveData = false, truncated = riffSize > size - 8;
    for (uint32_t pos = 12; pos < end;) {
        if (!fits(pos, 8, end))
            return fail(err, detail, cap, AUE_CORRUPT, "Truncated WAV chunk header");
        const uint8_t* chunk = data + pos;
        uint32_t n = le32(chunk + 4);
        pos += 8;
        if (!memcmp(chunk, "fmt ", 4)) {
            if (haveFmt || n < 16 || !fits(pos, n, end))
                return fail(err, detail, cap, AUE_CORRUPT, "Invalid WAV format chunk");
            format = le16(data + pos);
            channels = le16(data + pos + 2);
            rate = le32(data + pos + 4);
            byteRate = le32(data + pos + 8);
            block = le16(data + pos + 12);
            bits = le16(data + pos + 14);
            haveFmt = true;
        } else if (!memcmp(chunk, "data", 4)) {
            if (haveData)
                return fail(err, detail, cap, AUE_CORRUPT, "Duplicate WAV data chunk");
            payload = pos;
            bytes = n;
            haveData = true;
        }
        if (!fits(pos, n, end)) { truncated = true; break; }
        pos += n;
        if (n & 1) {
            if (pos == end)
                return fail(err, detail, cap, AUE_CORRUPT, "Missing WAV chunk padding");
            ++pos;
        }
    }
    if (!haveFmt || !haveData)
        return fail(err, detail, cap, AUE_CORRUPT, "Missing WAV fmt or data chunk");
    if ((format != 1 && format != 3) || channels < 1 || channels > 8 ||
        rate < 8000 || rate > 192000 ||
        (format == 3 ? bits != 32 : (bits != 8 && bits != 16 && bits != 24 && bits != 32)))
        return fail(err, detail, cap, AUE_UNSUPPORTED, "Unsupported WAV encoding, channels or rate");
    if (block != channels * (bits / 8) || byteRate != rate * uint32_t(block) || bytes % block)
        return fail(err, detail, cap, AUE_CORRUPT, "Inconsistent WAV sample layout");
    uint32_t frames = bytes / block;
    if (frames > rate * 20)
        return fail(err, detail, cap, AUE_TOO_LONG, "WAV exceeds 20 seconds");
    if (truncated || !fits(payload, bytes, end))
        return fail(err, detail, cap, AUE_CORRUPT, "Truncated WAV data or RIFF container");
    if (!frames)
        return fail(err, detail, cap, AUE_CORRUPT, "WAV contains no samples");
    int outChannels = channels > 2 ? 2 : channels;
    int16_t* pcm = static_cast<int16_t*>(malloc(size_t(frames) * outChannels * sizeof(int16_t)));
    if (!pcm) return fail(err, detail, cap, AUE_NO_MEMORY, "Cannot allocate WAV PCM");
    const uint8_t* p = data + payload;
    for (uint32_t f = 0; f < frames; ++f) {
        double input[8];
        for (int c = 0; c < channels; ++c, p += bits / 8) {
            if (format == 3) {
                uint32_t raw = le32(p);
                float value;
                memcpy(&value, &raw, sizeof(value));
                input[c] = value;
            } else if (bits == 8) input[c] = (int(p[0]) - 128) / 128.0;
            else if (bits == 16) input[c] = (int(le16(p)) - (p[1] & 128 ? 65536 : 0)) / 32768.0;
            else if (bits == 24) {
                int value = int(le16(p) | (uint32_t(p[2]) << 16));
                input[c] = (value - (p[2] & 128 ? 16777216 : 0)) / 8388608.0;
            } else {
                uint32_t raw = le32(p);
                input[c] = (double(raw) - (raw & 0x80000000u ? 4294967296.0 : 0)) / 2147483648.0;
            }
        }
        mix(input, channels, pcm + size_t(f) * outChannels);
    }
    *out = {pcm, frames, outChannels, int(rate)};
    return true;
}

static bool decodeOgg(const uint8_t* data, uint32_t size, PcmAudio* out,
                      AudioError* err, char* detail, int cap) {
    uint32_t serial = 0, sequence = 0, rate = 0;
    int channels = 0;
    uint64_t frames = 0, lastGranule = 0;
    bool continued = false, ended = false;
    // Validate every page, including CRC and EOS; stb accepts some truncated files.
    for (uint32_t pos = 0; pos < size;) {
        if (!fits(pos, 27, size) || memcmp(data + pos, "OggS", 4))
            return fail(err, detail, cap, AUE_CORRUPT, "Truncated or invalid Ogg page");
        const uint8_t* p = data + pos;
        uint32_t header = 27u + p[26], body = 0;
        if (p[4] || (p[5] & ~7) || !fits(pos, header, size))
            return fail(err, detail, cap, AUE_CORRUPT, "Invalid Ogg page header");
        for (unsigned i = 0; i < p[26]; ++i) body += p[27 + i];
        uint32_t length = header + body;
        if (!fits(pos, length, size))
            return fail(err, detail, cap, AUE_CORRUPT, "Truncated Ogg page body");
        uint32_t crc = 0;
        for (uint32_t i = 0; i < length; ++i)
            crc = crc32_update(crc, i >= 22 && i < 26 ? 0 : p[i]);
        if (crc != le32(p + 22))
            return fail(err, detail, cap, AUE_CORRUPT, "Ogg page checksum mismatch");
        if (pos == 0) {
            if (p[5] != 2 || le32(p + 18) || p[26] != 1 || body != 30 ||
                p[header] != 1 || memcmp(p + header + 1, "vorbis", 6))
                return fail(err, detail, cap, AUE_CORRUPT, "Invalid Vorbis identification packet");
            const uint8_t* id = p + header;
            if (le32(id + 7) || !(id[29] & 1) || (id[28] & 15) < 6 ||
                (id[28] >> 4) > 13 || (id[28] & 15) > (id[28] >> 4))
                return fail(err, detail, cap, AUE_CORRUPT, "Invalid Vorbis version or block sizes");
            channels = id[11];
            rate = le32(id + 12);
            if (channels < 1 || channels > 8 || rate < 8000 || rate > 192000)
                return fail(err, detail, cap, AUE_UNSUPPORTED, "Unsupported Vorbis channels or rate");
            serial = le32(p + 14);
        } else if (ended || le32(p + 14) != serial || (p[5] & 2)) {
            return fail(err, detail, cap, AUE_UNSUPPORTED, "Chained or multiplexed Ogg streams are unsupported");
        }
        if (le32(p + 18) != sequence++ || bool(p[5] & 1) != continued || !p[26])
            return fail(err, detail, cap, AUE_CORRUPT, "Invalid Ogg sequence or packet continuation");
        continued = p[header - 1] == 255;
        uint64_t granule = le64(p + 6);
        if (granule != ~uint64_t(0)) {
            if (granule < lastGranule)
                return fail(err, detail, cap, AUE_CORRUPT, "Ogg granule position goes backwards");
            lastGranule = granule;
        }
        ended = (p[5] & 4) != 0;
        if (ended) {
            if (continued || granule == ~uint64_t(0))
                return fail(err, detail, cap, AUE_CORRUPT, "Invalid final Ogg packet or granule");
            frames = granule;
        }
        pos += length;
    }
    if (!ended || !frames)
        return fail(err, detail, cap, AUE_CORRUPT, "Missing Ogg end page or samples");
    if (frames > uint64_t(rate) * 20)
        return fail(err, detail, cap, AUE_TOO_LONG, "Vorbis exceeds 20 seconds");
    int code = 0;
    stb_vorbis* decoder = stb_vorbis_open_memory(data, int(size), &code, nullptr);
    if (!decoder)
        return fail(err, detail, cap, code == VORBIS_outofmem ? AUE_NO_MEMORY : AUE_CORRUPT,
                    "Cannot open Vorbis stream");
    int outChannels = channels > 2 ? 2 : channels;
    int16_t* pcm = static_cast<int16_t*>(malloc(size_t(frames) * outChannels * sizeof(int16_t)));
    if (!pcm) {
        stb_vorbis_close(decoder);
        return fail(err, detail, cap, AUE_NO_MEMORY, "Cannot allocate Vorbis PCM");
    }
    float buffer[8][1024];
    float* planes[8];
    for (int c = 0; c < channels; ++c) planes[c] = buffer[c];
    uint32_t used = 0;
    bool valid = true;
    for (;;) {
        int n = stb_vorbis_get_samples_float(decoder, channels, planes, 1024);
        code = stb_vorbis_get_error(decoder);
        if (code || uint64_t(used) + n > frames) { valid = false; break; }
        if (!n) break;
        for (int i = 0; i < n; ++i) {
            double input[8];
            for (int c = 0; c < channels; ++c) input[c] = buffer[c][i];
            mix(input, channels, pcm + size_t(used + i) * outChannels);
        }
        used += n;
    }
    stb_vorbis_close(decoder);
    if (!valid || used != frames) {
        free(pcm);
        return fail(err, detail, cap, code == VORBIS_outofmem ? AUE_NO_MEMORY : AUE_CORRUPT,
                    "Invalid Vorbis audio packets or sample count");
    }
    *out = {pcm, used, outChannels, int(rate)};
    return true;
}

} // namespace

bool audioDecode(const uint8_t* data, uint32_t size, PcmAudio* out,
                 AudioError* err, char* detail, int detailCap) {
    if (out) memset(out, 0, sizeof(*out));
    if (err) *err = AUE_OK;
    if (detail && detailCap > 0) detail[0] = 0;
    if (!out || !data || !size)
        return fail(err, detail, detailCap, AUE_CORRUPT, "Missing input or output");
    if (size > kMaxInput)
        return fail(err, detail, detailCap, AUE_TOO_LONG, "Input exceeds 16 MiB");
    if (size >= 4 && !memcmp(data, "RIFF", 4))
        return decodeWav(data, size, out, err, detail, detailCap);
    if (size >= 4 && !memcmp(data, "OggS", 4))
        return decodeOgg(data, size, out, err, detail, detailCap);
    return fail(err, detail, detailCap, AUE_UNKNOWN_FORMAT, "Expected Ogg Vorbis or RIFF/WAVE");
}

void audioFree(PcmAudio* a) {
    if (!a) return;
    free(a->samples);
    memset(a, 0, sizeof(*a));
}

const char* audioErrorName(AudioError e) {
    switch (e) {
    case AUE_OK: return "AUE_OK";
    case AUE_UNKNOWN_FORMAT: return "AUE_UNKNOWN_FORMAT";
    case AUE_CORRUPT: return "AUE_CORRUPT";
    case AUE_UNSUPPORTED: return "AUE_UNSUPPORTED";
    case AUE_TOO_LONG: return "AUE_TOO_LONG";
    case AUE_NO_MEMORY: return "AUE_NO_MEMORY";
    }
    return "AUE_UNKNOWN_FORMAT";
}
