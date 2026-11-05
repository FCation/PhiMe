#include "skinpack.h"

namespace {

static uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
static uint32_t le32(const uint8_t* p) { return le16(p) | (uint32_t(le16(p + 2)) << 16); }
static uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
static uint16_t be16(const uint8_t* p) { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }
static bool fits(uint32_t pos, uint32_t n, uint32_t end) { return pos <= end && n <= end - pos; }
static bool fail(SkinResult* r, SkinError e, const char* detail) {
    if (r) { r->err = e; snprintf(r->detail, sizeof(r->detail), "%s", detail); }
    return false;
}
// Something was skipped or adjusted but the pack still loads: the notes are kept in detail ("; " separated).
static void warn(SkinResult* r, const char* text) {
    if (!r) return;
    size_t n = strlen(r->detail);
    if (n + 8 >= sizeof(r->detail)) return;
    snprintf(r->detail + n, sizeof(r->detail) - n, "%s%s", n ? "; " : "", text);
}
static uint32_t crc32(const uint8_t* p, uint32_t n) {
    uint32_t table[256];
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t v = i;
        for (int j = 0; j < 8; ++j) v = (v >> 1) ^ ((0u - (v & 1)) & 0xedb88320u);
        table[i] = v;
    }
    uint32_t v = ~0u;
    for (uint32_t i = 0; i < n; ++i) v = table[(v ^ p[i]) & 255] ^ (v >> 8);
    return ~v;
}

struct Bits {
    const uint8_t* p;
    uint32_t size, pos, value;
    int count;
    bool ok;
    uint32_t get(int n) {
        while (count < n) {
            if (pos == size) { ok = false; return 0; }
            value |= uint32_t(p[pos++]) << count;
            count += 8;
        }
        uint32_t v = value & ((1u << n) - 1);
        value >>= n;
        count -= n;
        return v;
    }
};
struct Huffman {
    uint16_t count[16], symbols[288];
    bool build(const uint8_t* lengths, int n, bool codeTree = false) {
        memset(count, 0, sizeof(count));
        for (int i = 0; i < n; ++i) {
            if (lengths[i] > 15) return false;
            ++count[lengths[i]];
        }
        int left = 1;
        for (int i = 1; i <= 15; ++i) {
            left = left * 2 - count[i];
            if (left < 0) return false;
        }
        // Only a single one-bit symbol (or an unused distance tree) may be incomplete.
        if (left && (codeTree || n - count[0] > 1 || (count[1] != 1 && count[0] != n))) return false;
        uint16_t offsets[16] = {};
        for (int i = 1; i < 15; ++i) offsets[i + 1] = offsets[i] + count[i];
        for (int i = 0; i < n; ++i) if (lengths[i]) symbols[offsets[lengths[i]]++] = uint16_t(i);
        return true;
    }
    int decode(Bits& b) const {
        unsigned code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            code |= b.get(1);
            if (!b.ok) return -1;
            if (code >= first && code - first < count[len]) return symbols[index + code - first];
            index += count[len];
            first = (first + count[len]) << 1;
            code <<= 1;
        }
        return -1;
    }
};

static bool inflate(const uint8_t* src, uint32_t size, uint8_t* dst, uint32_t cap) {
    Bits b = {src, size, 0, 0, 0, true};
    uint32_t used = 0;
    bool final;
    do {
        final = b.get(1) != 0;
        unsigned type = b.get(2);
        if (!b.ok || type == 3) return false;
        if (type == 0) {
            b.get(b.count & 7);
            uint32_t n = b.get(16), inv = b.get(16);
            if (!b.ok || (n ^ inv) != 65535 || n > cap - used || !fits(b.pos, n, size)) return false;
            memcpy(dst + used, src + b.pos, n);
            b.pos += n;
            used += n;
            continue;
        }
        Huffman lit, dist;
        uint8_t lengths[320] = {};
        if (type == 1) {
            for (int i = 0; i < 288; ++i) lengths[i] = uint8_t(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
            if (!lit.build(lengths, 288)) return false;
            for (int i = 0; i < 32; ++i) lengths[i] = 5;
            if (!dist.build(lengths, 32)) return false;
        } else {
            int nl = int(b.get(5)) + 257, nd = int(b.get(5)) + 1, nc = int(b.get(4)) + 4;
            if (!b.ok || nl > 286) return false;
            static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            uint8_t codes[19] = {};
            for (int i = 0; i < nc; ++i) codes[order[i]] = uint8_t(b.get(3));
            Huffman tree;
            if (!b.ok || !tree.build(codes, 19, true)) return false;
            int i = 0;
            while (i < nl + nd) {
                int c = tree.decode(b), repeat = 1;
                uint8_t value;
                if (c < 0) return false;
                if (c <= 15) value = uint8_t(c);
                else {
                    if (c == 16 && i == 0) return false;
                    value = c == 16 ? lengths[i - 1] : 0;
                    repeat = c == 16 ? int(b.get(2)) + 3 : c == 17 ? int(b.get(3)) + 3 : int(b.get(7)) + 11;
                }
                if (!b.ok || repeat > nl + nd - i) return false;
                while (repeat--) lengths[i++] = value;
            }
            if (!lengths[256] || !lit.build(lengths, nl) || !dist.build(lengths + nl, nd)) return false;
        }
        static const uint16_t bases[29] = {
            3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
        };
        static const uint8_t extras[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
        static const uint16_t db[30] = {
            1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
        };
        for (;;) {
            int c = lit.decode(b);
            if (c < 0 || c > 285) return false;
            if (c == 256) break;
            if (c < 256) {
                if (used == cap) return false;
                dst[used++] = uint8_t(c);
            } else {
                uint32_t len = bases[c - 257] + b.get(extras[c - 257]);
                int d = dist.decode(b);
                if (d < 0 || d >= 30) return false;
                int ex = d < 4 ? 0 : d / 2 - 1;
                uint32_t distance = db[d] + b.get(ex);
                if (!b.ok || distance > used || len > cap - used) return false;
                for (uint32_t j = 0; j < len; ++j) { dst[used] = dst[used - distance]; ++used; }
            }
        }
    } while (!final);
    return b.ok && used == cap && b.pos == size;
}

struct Entry {
    const uint8_t* name;
    uint16_t nameSize, flags, method;
    uint32_t crc, packed, size, offset;
};
static char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; }
static char pathChar(uint8_t c) { return c == '\\' ? '/' : lower(char(c)); }
static bool nameEquals(const Entry& e, uint32_t start, const char* text) {
    uint32_t n = uint32_t(strlen(text));
    if (start > e.nameSize || n != e.nameSize - start) return false;
    for (uint32_t i = 0; i < n; ++i) if (pathChar(e.name[start + i]) != text[i]) return false;
    return true;
}
// A file of the pack: any folder depth (base = start of the file name, depth = folder count).
// Rejects "..", Mac metadata (__MACOSX/ and "._" files) and directories.
static bool packName(const Entry& e, uint32_t& base, int* depth = nullptr) {
    base = 0;
    int d = 0;
    if (!e.nameSize || pathChar(e.name[0]) == '/' || pathChar(e.name[e.nameSize - 1]) == '/') return false;
    uint32_t seg = 0;
    for (unsigned i = 0; i < e.nameSize; ++i) {
        char c = pathChar(e.name[i]);
        if (!c || (i && e.name[i - 1] == '.' && c == '.')) return false;
        if (c == '/') {
            if (i - seg == 8) {
                const char* mac = "__macosx";
                unsigned k = 0;
                while (k < 8 && pathChar(e.name[seg + k]) == mac[k]) ++k;
                if (k == 8) return false;
            }
            base = seg = i + 1;
            ++d;
        }
    }
    if (e.nameSize - base >= 2 && e.name[base] == '.' && e.name[base + 1] == '_') return false;
    if (depth) *depth = d;
    return true;
}
static bool extraCheck(const uint8_t* p, uint32_t n, SkinResult* r) {
    // ZIP64 extra fields (id 1) are fine while the 32-bit sizes are real (not 0xFFFFFFFF, checked by the callers)
    while (n) {
        if (n < 4 || le16(p + 2) > n - 4) return fail(r, SKE_ZIP_CORRUPT, "ZIP extra field truncated");
        uint32_t len = 4u + le16(p + 2);
        p += len;
        n -= len;
    }
    return true;
}
static bool directory(const uint8_t* z, uint32_t size, Entry*& entries, int& count,
                      uint32_t& cd, SkinResult* r) {
    if (!z || size < 4) return fail(r, SKE_NOT_ZIP, "ZIP signature missing");
    uint32_t end = size >= 22 ? size - 22 : 0;
    uint32_t start = size > 65557 ? size - 65557 : 0;
    bool found = false;
    if (size >= 22) for (uint32_t i = end;; --i) {
        if (le32(z + i) == 0x06054b50 && fits(i, 22, size) && le16(z + i + 20) == size - i - 22) {
            end = i; found = true; break;
        }
        if (i == start) break;
    }
    if (!found) {
        uint32_t sig = le32(z);
        return fail(r, sig == 0x04034b50 || sig == 0x06054b50 || sig == 0x02014b50 || sig == 0x06064b50
                    ? SKE_ZIP_CORRUPT : SKE_NOT_ZIP, "ZIP end of central directory missing");
    }
    const uint8_t* e = z + end;
    count = le16(e + 10);
    cd = le32(e + 16);
    uint32_t len = le32(e + 12);
    if (count == 65535 || cd == ~0u || len == ~0u || (end >= 20 && le32(e - 20) == 0x07064b50))
        return fail(r, SKE_ZIP_UNSUPPORTED, "ZIP64 directory");
    if (le16(e + 4) || le16(e + 6) || le16(e + 8) != count)
        return fail(r, SKE_ZIP_UNSUPPORTED, "Multi-disk ZIP");
    if (count > 4096) return fail(r, SKE_TOO_BIG, "ZIP has more than 4096 entries");
    if (!fits(cd, len, end) || cd + len != end) return fail(r, SKE_ZIP_CORRUPT, "ZIP directory bounds");
    entries = static_cast<Entry*>(malloc(sizeof(Entry) * size_t(count ? count : 1)));
    if (!entries) return fail(r, SKE_NO_MEMORY, "ZIP directory");
    uint32_t pos = cd;
    for (int i = 0; i < count; ++i) {
        if (!fits(pos, 46, end) || le32(z + pos) != 0x02014b50) return fail(r, SKE_ZIP_CORRUPT, "ZIP central header");
        const uint8_t* p = z + pos;
        Entry& a = entries[i];
        a.flags = le16(p + 8); a.method = le16(p + 10); a.crc = le32(p + 16);
        a.packed = le32(p + 20); a.size = le32(p + 24); a.nameSize = le16(p + 28);
        a.offset = le32(p + 42); a.name = p + 46;
        uint32_t extra = le16(p + 30), comment = le16(p + 32);
        if (!fits(pos + 46, a.nameSize + extra + comment, end)) return fail(r, SKE_ZIP_CORRUPT, "ZIP central name/extra bounds");
        if ((a.flags & (1 | 64)) || (a.method != 0 && a.method != 8) || le16(p + 34) ||
            a.packed == ~0u || a.size == ~0u || a.offset == ~0u)
            return fail(r, SKE_ZIP_UNSUPPORTED, "ZIP encrypted, ZIP64, split or unsupported method");
        if (!extraCheck(a.name + a.nameSize, extra, r)) return false;
        pos += 46 + a.nameSize + extra + comment;
    }
    return pos == end || fail(r, SKE_ZIP_CORRUPT, "ZIP directory size mismatch");
}
static bool extract(const uint8_t* z, uint32_t cd, const Entry& a, SkinBlob& out,
                    const char* name, SkinResult* r) {
    if (!fits(a.offset, 30, cd) || le32(z + a.offset) != 0x04034b50) return fail(r, SKE_ZIP_CORRUPT, name);
    const uint8_t* p = z + a.offset;
    uint16_t flags = le16(p + 6), method = le16(p + 8), ns = le16(p + 26), es = le16(p + 28);
    if ((flags & (1 | 64)) || (method != 0 && method != 8)) return fail(r, SKE_ZIP_UNSUPPORTED, name);
    if (le32(p + 18) == ~0u || le32(p + 22) == ~0u) return fail(r, SKE_ZIP_UNSUPPORTED, "ZIP64 local header");
    uint32_t pos = a.offset + 30;
    if (!fits(pos, uint32_t(ns) + es, cd) || ns != a.nameSize || memcmp(p + 30, a.name, ns) ||
        flags != a.flags || method != a.method) return fail(r, SKE_ZIP_CORRUPT, name);
    if (!extraCheck(p + 30 + ns, es, r)) return false;
    pos += ns + es;
    if (!fits(pos, a.packed, cd) || (method == 0 && a.packed != a.size) ||
        (!(flags & 8) && (le32(p + 14) != a.crc || le32(p + 18) != a.packed || le32(p + 22) != a.size)))
        return fail(r, SKE_ZIP_CORRUPT, name);
    if (flags & 8) {
        uint32_t dp = pos + a.packed;
        if (fits(dp, 4, cd) && le32(z + dp) == 0x08074b50) dp += 4;
        bool small = fits(dp, 12, cd) && le32(z + dp) == a.crc && le32(z + dp + 4) == a.packed && le32(z + dp + 8) == a.size;
        bool big = fits(dp, 20, cd) && le32(z + dp) == a.crc && le32(z + dp + 4) == a.packed && !le32(z + dp + 8) &&
                   le32(z + dp + 12) == a.size && !le32(z + dp + 16);  // ZIP64 descriptor: 8-byte sizes
        if (!small && !big) return fail(r, SKE_ZIP_CORRUPT, name);
    }
    out.data = static_cast<uint8_t*>(malloc(a.size ? a.size : 1));
    if (!out.data) return fail(r, SKE_NO_MEMORY, name);
    out.size = a.size;
    if (method == 0) memcpy(out.data, z + pos, a.size);
    else if (!inflate(z + pos, a.packed, out.data, a.size)) return fail(r, SKE_ZIP_CORRUPT, name);
    return crc32(out.data, out.size) == a.crc || fail(r, SKE_ZIP_CORRUPT, name);
}

static bool space(char c) { return c == ' ' || c == '\t'; }
static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
static bool utf8(const uint8_t* s, uint32_t n) {
    for (uint32_t i = 0; i < n;) {
        uint32_t c = s[i++], minimum = 0;
        int tail;
        if (c < 128) { if (!c || (c < 32 && c != 9)) return false; continue; }
        if (c >= 194 && c <= 223) { tail = 1; c &= 31; minimum = 128; }
        else if (c >= 224 && c <= 239) { tail = 2; c &= 15; minimum = 2048; }
        else if (c >= 240 && c <= 244) { tail = 3; c &= 7; minimum = 65536; }
        else return false;
        if (uint32_t(tail) > n - i) return false;
        while (tail--) { uint8_t b = s[i++]; if ((b & 192) != 128) return false; c = (c << 6) | (b & 63); }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}
static void copyText(char* dst, uint32_t cap, const char* s) {
    uint32_t n = 0, complete = 0;
    int remaining = 0;
    while (*s && n < cap - 1) {
        uint8_t c = uint8_t(*s++);
        dst[n++] = char(c);
        if (c < 128) remaining = 0;
        else if (c >= 240) remaining = 3;
        else if (c >= 224) remaining = 2;
        else if (c >= 192) remaining = 1;
        else --remaining;
        if (!remaining) complete = n;
    }
    dst[complete] = 0;
}
static bool equalCI(const char* a, const char* b) {
    while (*a && lower(*a) == lower(*b)) { ++a; ++b; }
    return *a == *b;
}
static bool boolean(const char* s, bool& v) {
    if (equalCI(s, "true") || equalCI(s, "yes") || equalCI(s, "on") || !strcmp(s, "1")) { v = true; return true; }
    if (equalCI(s, "false") || equalCI(s, "no") || equalCI(s, "off") || !strcmp(s, "0")) { v = false; return true; }
    return false;
}
// "hitFx", "HitFx", "hit_fx" and "hit-fx" are the same key
static bool keyIs(const char* key, const char* want) {
    for (;;) {
        while (*key == '_' || *key == '-') ++key;
        if (!*key || !*want) return !*key && !*want;
        if (lower(*key++) != lower(*want++)) return false;
    }
}
static bool integer(const char* s, uint32_t& v) {
    v = 0;
    if (*s == '+') ++s;
    unsigned base = 10;
    if (s[0] == '0' && lower(s[1]) == 'x') { base = 16; s += 2; }
    if (!*s) return false;
    for (; *s; ++s) {
        int d = hex(*s);
        if (d < 0 || unsigned(d) >= base || v > (~0u - unsigned(d)) / base) return false;
        v = v * base + unsigned(d);
    }
    return true;
}
static bool number(const char* s, double& value) {
    bool neg = *s == '-';
    if (*s == '-' || *s == '+') ++s;
    // Accumulate a bounded significand; account for discarded digits in the exponent.
    double mantissa = 0;
    int digits = 0, stored = 0, exponent = 0;
    bool dot = false, nonzero = false;
    while ((*s >= '0' && *s <= '9') || *s == '.') {
        if (*s == '.') { if (dot) return false; dot = true; ++s; continue; }
        int d = *s++ - '0'; ++digits;
        if (dot && exponent > -100000) --exponent;
        if (d || nonzero) {
            nonzero = true;
            if (stored < 18) { mantissa = mantissa * 10 + d; ++stored; }
            else if (exponent < 100000) ++exponent;
        }
    }
    if (!digits) return false;
    if (*s == 'e' || *s == 'E') {
        ++s;
        bool eneg = *s == '-';
        if (*s == '+' || *s == '-') ++s;
        int e = 0, ed = 0;
        while (*s >= '0' && *s <= '9') { if (e < 100000) e = e * 10 + *s - '0'; ++s; ++ed; }
        if (!ed) return false;
        exponent += eneg ? -e : e;
    }
    if (*s || (nonzero && (exponent > 308 || exponent < -340))) return false;
    if (!nonzero) { value = 0; return true; }
    while (exponent > 0) { mantissa *= 10; --exponent; }
    while (exponent < 0) { mantissa *= 0.1; ++exponent; }
    value = neg ? -mantissa : mantissa;
    return value <= 1.7976931348623157e308 && value >= -1.7976931348623157e308;
}
// A whole count: 50, 0x32 or 50.0
static bool whole(const char* s, uint32_t& v) {
    if (integer(s, v)) return true;
    double d;
    if (!number(s, d) || d < 0 || d > 1e6 || double(uint32_t(d)) != d) return false;
    v = uint32_t(d);
    return true;
}
// 0xAARRGGBB / decimal as Phira writes it; also "#RRGGBB" (opaque) and "#AARRGGBB"
static bool color(const char* s, uint32_t& v) {
    if (integer(s, v)) return true;
    if (s[0] == '-' && integer(s + 1, v) && v && v <= 0x80000000u) { v = 0u - v; return true; }  // signed ARGB
    if (*s != '#') return false;
    uint32_t n = uint32_t(strlen(s + 1));
    if (n != 6 && n != 8) return false;
    v = 0;
    for (uint32_t i = 1; i <= n; ++i) { int d = hex(s[i]); if (d < 0) return false; v = (v << 4) | unsigned(d); }
    if (n == 6) v |= 0xff000000u;
    return true;
}

struct Scalar { char* text; bool quoted; };
struct Yaml {
    const char* p;
    const char* end;
    char* write;
    void ws() { while (p < end && space(*p)) ++p; }
    bool unicode(uint32_t& cp) {
        if (end - p < 4) return false;
        cp = 0;
        for (int i = 0; i < 4; ++i) { int d = hex(*p++); if (d < 0) return false; cp = (cp << 4) | unsigned(d); }
        return true;
    }
    void emit(uint32_t cp) {
        if (cp < 128) *write++ = char(cp);
        else if (cp < 2048) { *write++ = char(192 | (cp >> 6)); *write++ = char(128 | (cp & 63)); }
        else {
            if (cp >= 65536) { *write++ = char(240 | (cp >> 18)); *write++ = char(128 | ((cp >> 12) & 63)); }
            else *write++ = char(224 | (cp >> 12));
            *write++ = char(128 | ((cp >> 6) & 63)); *write++ = char(128 | (cp & 63));
        }
    }
    bool scalar(Scalar& out, bool list) {
        ws(); out.text = write; out.quoted = false;
        if (p == end || *p == '#' || (list && (*p == ',' || *p == ']'))) { *write++ = 0; return true; }
        if (*p == '\'' || *p == '"') {
            char quote = *p++; out.quoted = true;
            bool closed = false;
            while (p < end) {
                char c = *p++;
                if (c == quote) {
                    if (quote == '\'' && p < end && *p == '\'') { ++p; *write++ = '\''; continue; }
                    closed = true; break;
                }
                if (quote == '"' && c == '\\') {
                    if (p == end) return false;
                    c = *p++;
                    if (c == 'n') c = '\n';
                    else if (c == 't') c = '\t';
                    else if (c == 'u') {
                        uint32_t cp;
                        if (!unicode(cp) || !cp) return false;
                        if (cp >= 0xd800 && cp <= 0xdbff) {
                            if (end - p < 6 || p[0] != '\\' || p[1] != 'u') return false;
                            p += 2; uint32_t low;
                            if (!unicode(low) || low < 0xdc00 || low > 0xdfff) return false;
                            cp = 65536 + ((cp - 0xd800) << 10) + low - 0xdc00;
                        } else if (cp >= 0xdc00 && cp <= 0xdfff) return false;
                        emit(cp); continue;
                    } else if (c != '"' && c != '\\') return false;
                }
                *write++ = c;
            }
            if (!closed) return false;
        } else {
            char first = *p;
            if (first == '|' || first == '>' || first == '&' || first == '*' || first == '!' ||
                first == '@' || first == '`' || first == '%' || first == '?' ||
                (first == '-' && (p + 1 == end || space(p[1])))) return false;
            while (p < end && *p != '#' && (!list || (*p != ',' && *p != ']'))) {
                char c = *p++;
                if (c == '[' || c == ']' || c == '{' || c == '}' ||
                    (c == ':' && (p == end || space(*p)))) return false;
                *write++ = c;
            }
            while (write > out.text && space(write[-1])) --write;
        }
        *write++ = 0; ws();
        return p == end || *p == '#' || (list && (*p == ',' || *p == ']'));
    }
};

static bool parseInfo(const SkinBlob& info, SkinPack& pack, SkinResult* r) {
    // One scratch buffer, reused for each line; quoted escapes never expand the input.
    char* scratch = static_cast<char*>(malloc(size_t(info.size) + 4));
    if (!scratch) return fail(r, SKE_NO_MEMORY, "info.yml");
    const char* data = reinterpret_cast<const char*>(info.data);
    uint32_t pos = 0, line = 0;
    bool mh = false, ok = true;
    if (info.size >= 3 && !memcmp(data, "\xef\xbb\xbf", 3)) pos = 3;
    while (pos < info.size && ok) {
        ++line;
        uint32_t begin = pos;
        while (pos < info.size && data[pos] != '\n') ++pos;
        uint32_t finish = pos;
        if (pos < info.size) ++pos;
        if (finish > begin && data[finish - 1] == '\r') --finish;
        char key[128] = {};
        SkinError error = SKE_INFO_SYNTAX;
        const char* why = "unsupported YAML";
        do {
            if (!utf8(reinterpret_cast<const uint8_t*>(data + begin), finish - begin)) { why = "invalid UTF-8"; break; }
            Yaml y = {data + begin, data + finish, scratch};
            y.ws();
            if (y.p == y.end || *y.p == '#') { error = SKE_OK; break; }
            if (y.p != data + begin) break;
            unsigned k = 0;
            bool validKey = true;
            while (y.p < y.end && *y.p != ':' && !space(*y.p)) {
                char c = *y.p++;
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-') || k == sizeof(key) - 1) { validKey = false; break; }
                key[k++] = c;
            }
            y.ws();
            if (!validKey || !k || y.p == y.end || *y.p++ != ':') break;
            y.ws();
            bool list = y.p < y.end && *y.p == '[';
            Scalar scalars[2] = {};
            int n = 0;
            bool syntax = true;
            if (list) {
                ++y.p; y.ws();
                if (y.p < y.end && *y.p != ']') for (;;) {
                    Scalar s;
                    if (!y.scalar(s, true) || !s.text[0]) { syntax = false; break; }
                    if (n < 2) scalars[n] = s;
                    ++n;
                    if (y.p == y.end || *y.p != ',') break;
                    ++y.p; y.ws();
                    if (y.p == y.end || *y.p == ']') { syntax = false; break; }
                }
                if (!syntax || y.p == y.end || *y.p++ != ']') break;
                y.ws();
            } else {
                if (!y.scalar(scalars[0], false)) break;
                n = 1;
            }
            if (y.p != y.end && *y.p != '#') break;
            // Values are read loosely (quoted numbers, 50.0, #RRGGBB); a bad one is skipped and the default kept
            error = SKE_INFO_VALUE; why = "wrong type or out of range";
            char* text = scalars[0].text;
            uint32_t a = 0, b = 0;
            bool boolValue = false;
            double num = 0;
            if (keyIs(key, "name") || keyIs(key, "author") || keyIs(key, "description")) {
                if (list) break;
                if (keyIs(key, "name")) copyText(pack.name, sizeof(pack.name), text);
                else if (keyIs(key, "author")) copyText(pack.author, sizeof(pack.author), text);
                else copyText(pack.description, sizeof(pack.description), text);
            } else if (keyIs(key, "hitFx") || keyIs(key, "holdAtlas") || keyIs(key, "holdAtlasMH")) {
                if (!list || n != 2 || !whole(text, a) || !whole(scalars[1].text, b)) break;
                if (keyIs(key, "hitFx")) {
                    if (!a || !b || a > 64 || b > 64) break;
                    pack.hitFxCols = int(a); pack.hitFxRows = int(b);
                } else {
                    if (a > 4096 || b > 4096) break;
                    if (keyIs(key, "holdAtlasMH")) { pack.holdTopMH = int(a); pack.holdBottomMH = int(b); mh = true; }
                    else { pack.holdTop = int(a); pack.holdBottom = int(b); }
                }
            } else if (keyIs(key, "hitFxDuration") || keyIs(key, "hitFxScale")) {
                if (list || !number(text, num) || !(num > 0 && num <= 10) || float(num) == 0) break;
                if (keyIs(key, "hitFxDuration")) pack.hitFxDuration = float(num);
                else pack.hitFxScale = float(num);
            } else if (keyIs(key, "colorPerfect") || keyIs(key, "colorGood")) {
                if (list || !color(text, a)) break;
                if (keyIs(key, "colorPerfect")) pack.colorPerfect = a;
                else pack.colorGood = a;
            } else {
                bool* target = nullptr;
                if (keyIs(key, "hitFxRotate")) target = &pack.hitFxRotate;
                else if (keyIs(key, "hitFxTinted")) target = &pack.hitFxTinted;
                else if (keyIs(key, "hideParticles")) target = &pack.hideParticles;
                else if (keyIs(key, "holdKeepHead")) target = &pack.holdKeepHead;
                else if (keyIs(key, "holdRepeat")) target = &pack.holdRepeat;
                else if (keyIs(key, "holdCompact")) target = &pack.holdCompact;
                if (target) {
                    if (list || !boolean(text, boolValue)) break;
                    *target = boolValue;
                }
            }
            error = SKE_OK;
        } while (false);
        if (error != SKE_OK) {
            // Phira itself would refuse the pack; we skip the line so the pack still loads, and say so in the log
            char detail[160];
            snprintf(detail, sizeof(detail), "info.yml line %u key %s skipped: %s", line, key, why);
            warn(r, detail);
        }
    }
    if (!mh) { pack.holdTopMH = pack.holdTop; pack.holdBottomMH = pack.holdBottom; }
    free(scratch);
    return ok;
}

static const char* const names[SK_FILE_COUNT + 1] = {
    "click.png", "click_mh.png", "drag.png", "drag_mh.png", "flick.png", "flick_mh.png",
    "hold.png", "hold_mh.png", "hit_fx.png", "click.ogg", "drag.ogg", "flick.ogg", "info.yml"
};
// Image size from the content: PNG, or JPEG (Unity's LoadImage reads both, whatever the file is called)
static bool imageSize(const uint8_t* d, uint32_t n, uint32_t& w, uint32_t& h) {
    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    if (n >= 33 && !memcmp(d, sig, 8) && be32(d + 8) == 13 && !memcmp(d + 12, "IHDR", 4)) {
        w = be32(d + 16); h = be32(d + 20);
        return true;
    }
    if (n < 4 || d[0] != 0xff || d[1] != 0xd8) return false;
    for (uint32_t i = 2; i + 4 <= n;) {
        if (d[i] != 0xff) return false;
        uint8_t m = d[i + 1];
        if (m == 0xff) { ++i; continue; }
        if (m == 0x01 || (m >= 0xd0 && m <= 0xd8)) { i += 2; continue; }
        if (m == 0xd9 || m == 0xda) return false;
        uint32_t len = be16(d + i + 2);
        if (len < 2 || !fits(i + 2, len, n)) return false;
        if (m >= 0xc0 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc) {  // start of frame
            if (len < 7) return false;
            h = be16(d + i + 5); w = be16(d + i + 7);
            return true;
        }
        i += 2 + len;
    }
    return false;
}
// Sound kind from the content, whatever the file is called: 1 Ogg, 2 WAV, 3 MP3, 4 FLAC (all decoded by
// audiodec), 0 unknown
static int soundKind(const SkinBlob& b) {
    if (b.size >= 4 && !memcmp(b.data, "OggS", 4)) return 1;
    if (b.size >= 12 && !memcmp(b.data, "RIFF", 4) && !memcmp(b.data + 8, "WAVE", 4)) return 2;
    if (b.size >= 4 && !memcmp(b.data, "fLaC", 4)) return 4;
    if (b.size >= 3 && !memcmp(b.data, "ID3", 3)) {  // a tag, then FLAC or MP3
        if (b.size < 10) return 3;
        uint32_t s = 10 + ((uint32_t(b.data[6] & 127) << 21) | (uint32_t(b.data[7] & 127) << 14) |
                           (uint32_t(b.data[8] & 127) << 7) | (b.data[9] & 127));
        return s + 4 <= b.size && !memcmp(b.data + s, "fLaC", 4) ? 4 : 3;
    }
    if (b.size >= 2 && b.data[0] == 255 && (b.data[1] & 224) == 224) return 3;
    return 0;
}
struct FreeLater { void* p; ~FreeLater() { free(p); } };

static bool load(const uint8_t* z, uint32_t size, SkinPack& p, SkinResult* r) {
    if (size > 64u * 1024 * 1024) return fail(r, SKE_TOO_BIG, "ZIP input exceeds 64 MiB");
    Entry* entries = nullptr;
    int count = 0; uint32_t cd = 0;
    bool dirOk = directory(z, size, entries, count, cd, r);
    FreeLater keep = {entries};
    if (!dirOk) return false;
    // sounds: up to one candidate per extension (ogg, wav, flac, mp3), picked later by what the bytes really are
    int chosen[SK_FILE_COUNT + 1], sound[3][4];
    for (int& c : chosen) c = -1;
    for (auto& s : sound) for (int& c : s) c = -1;
    // info.yml: the shallowest one wins (a pack zipped inside one or more folders still works)
    uint32_t prefixLen = 0;
    int bestDepth = 1 << 30;
    for (int i = 0; i < count; ++i) {
        uint32_t base; int depth;
        if (packName(entries[i], base, &depth) && nameEquals(entries[i], base, "info.yml") && depth < bestDepth) {
            chosen[SK_FILE_COUNT] = i;
            prefixLen = base;
            bestDepth = depth;
        }
    }
    if (chosen[SK_FILE_COUNT] < 0) return fail(r, SKE_NO_INFO, "info.yml missing");
    for (int i = 0; i < count; ++i) {
        uint32_t base;
        if (!packName(entries[i], base) || base != prefixLen) continue;
        const Entry& info = entries[chosen[SK_FILE_COUNT]];
        uint32_t c = 0;
        while (c < prefixLen && pathChar(entries[i].name[c]) == pathChar(info.name[c])) ++c;
        if (c != prefixLen) continue;
        for (int j = 0; j < SK_SND_CLICK; ++j) if (chosen[j] < 0 && nameEquals(entries[i], base, names[j])) chosen[j] = i;
        for (int s = 0; s < 3; ++s) {
            static const char* const stems[3] = {"click", "drag", "flick"};
            static const char* const extensions[4] = {"ogg", "wav", "flac", "mp3"};
            for (int ext = 0; ext < 4; ++ext) {
                char file[32]; snprintf(file, sizeof(file), "%s.%s", stems[s], extensions[ext]);
                if (sound[s][ext] < 0 && nameEquals(entries[i], base, file)) sound[s][ext] = i;
            }
        }
    }
    uint32_t total = 0;
    for (int i = 0; i <= SK_FILE_COUNT + 12; ++i) {
        int e = i <= SK_FILE_COUNT ? chosen[i] : sound[(i - SK_FILE_COUNT - 1) / 4][(i - SK_FILE_COUNT - 1) % 4];
        if (e < 0) continue;
        uint32_t n = entries[e].size;
        if (n > 32u * 1024 * 1024 || n > 96u * 1024 * 1024 - total)
            return fail(r, SKE_TOO_BIG, i <= SK_FILE_COUNT ? names[i] : "sound");
        total += n;
    }
    for (int i = 0; i < SK_SND_CLICK; ++i) if ((i == SK_CLICK || i == SK_DRAG || i == SK_FLICK || i == SK_HOLD || i == SK_HIT_FX) && chosen[i] < 0)
        return fail(r, SKE_MISSING_FILE, names[i]);
    p.hitFxCols = 16; p.hitFxRows = 1; p.hitFxDuration = 0.5f; p.hitFxScale = 1.0f;
    p.hitFxTinted = true; p.colorPerfect = 0xe1ffec9f; p.colorGood = 0xebb4e1ff;
    SkinBlob info = {};
    bool ok = extract(z, cd, entries[chosen[SK_FILE_COUNT]], info, "info.yml", r);
    if (ok) ok = parseInfo(info, p, r);
    free(info.data);
    if (!ok) return false;
    char note[96];
    for (int i = 0; i < SK_SND_CLICK; ++i) if (chosen[i] >= 0) {
        const char* name = names[i];
        if (!extract(z, cd, entries[chosen[i]], p.files[i], name, r)) return false;
        SkinBlob& b = p.files[i];
        uint32_t w = 0, h = 0;
        bool good = imageSize(b.data, b.size, w, h) && w && h && w <= 4096 && h <= 4096;
        bool optional = i == SK_CLICK_MH || i == SK_DRAG_MH || i == SK_FLICK_MH || i == SK_HOLD_MH;
        if (!good && !optional) return fail(r, SKE_BAD_IMAGE, name);
        if (!good) {  // a broken multi-hit image: fall back to the plain one
            free(b.data); b = SkinBlob{};
            snprintf(note, sizeof note, "%s skipped: not a PNG/JPEG up to 4096 px", name); warn(r, note);
            continue;
        }
        b.w = int(w); b.h = int(h);
        // atlas cuts that do not fit: shrink them in proportion (Phira would refuse the pack)
        int* top = i == SK_HOLD ? &p.holdTop : i == SK_HOLD_MH ? &p.holdTopMH : nullptr;
        int* bot = i == SK_HOLD ? &p.holdBottom : i == SK_HOLD_MH ? &p.holdBottomMH : nullptr;
        if (top && *top + *bot >= b.h) {
            int sum = *top + *bot, room = b.h - 1;
            *top = int((long long)*top * room / sum); *bot = int((long long)*bot * room / sum);
            snprintf(note, sizeof note, "%s atlas cut shrunk to [%d, %d]", name, *top, *bot); warn(r, note);
        }
        if (i == SK_HIT_FX && (b.w < p.hitFxCols || b.h < p.hitFxRows)) {
            if (b.w < p.hitFxCols) p.hitFxCols = b.w;
            if (b.h < p.hitFxRows) p.hitFxRows = b.h;
            warn(r, "hitFx grid shrunk to the image size");
        }
    }
    // sounds: the first candidate (ogg, wav, flac, mp3 order) whose bytes are a known format; none = the game's sound
    for (int s = 0; s < 3; ++s) {
        SkinBlob& b = p.files[SK_SND_CLICK + s];
        for (int ext = 0; ext < 4 && !b.data; ++ext) {
            int e = sound[s][ext];
            if (e < 0) continue;
            char name[32];
            uint32_t n = entries[e].nameSize - prefixLen;
            if (n >= sizeof name) n = sizeof name - 1;
            memcpy(name, entries[e].name + prefixLen, n); name[n] = 0;
            if (!extract(z, cd, entries[e], b, name, r)) return false;
            if (soundKind(b)) break;
            free(b.data); b = SkinBlob{};
            snprintf(note, sizeof note, "%s skipped: not Ogg, WAV, FLAC or MP3", name);
            warn(r, note);
        }
    }
    return true;
}

} // namespace

bool skinPackLoad(const uint8_t* zip, uint32_t size, SkinPack* out, SkinResult* res) {
    if (res) { res->err = SKE_OK; res->detail[0] = 0; }
    if (!out) return fail(res, SKE_INFO_VALUE, "out is null");
    memset(out, 0, sizeof(*out));
    if (load(zip, size, *out, res)) return true;
    skinPackFree(out);
    return false;
}
void skinPackFree(SkinPack* p) {
    if (!p) return;
    for (SkinBlob& b : p->files) free(b.data);
    memset(p, 0, sizeof(*p));
}
const char* skinErrorName(SkinError e) {
    switch (e) {
#define SK_NAME(x) case x: return #x
        SK_NAME(SKE_OK); SK_NAME(SKE_NOT_ZIP); SK_NAME(SKE_ZIP_CORRUPT); SK_NAME(SKE_ZIP_UNSUPPORTED);
        SK_NAME(SKE_TOO_BIG); SK_NAME(SKE_NO_INFO); SK_NAME(SKE_INFO_SYNTAX); SK_NAME(SKE_INFO_VALUE);
        SK_NAME(SKE_MISSING_FILE); SK_NAME(SKE_BAD_IMAGE); SK_NAME(SKE_BAD_SOUND); SK_NAME(SKE_NO_MEMORY);
#undef SK_NAME
        default: return "SKE_UNKNOWN";
    }
}
