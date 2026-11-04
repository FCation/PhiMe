#include "skinpack.h"

namespace {

static uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
static uint32_t le32(const uint8_t* p) { return le16(p) | (uint32_t(le16(p + 2)) << 16); }
static uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
static bool fits(uint32_t pos, uint32_t n, uint32_t end) { return pos <= end && n <= end - pos; }
static bool fail(SkinResult* r, SkinError e, const char* detail) {
    if (r) { r->err = e; snprintf(r->detail, sizeof(r->detail), "%s", detail); }
    return false;
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
static bool packName(const Entry& e, uint32_t& base) {
    base = 0;
    if (!e.nameSize || pathChar(e.name[0]) == '/' || pathChar(e.name[e.nameSize - 1]) == '/') return false;
    for (unsigned i = 0; i < e.nameSize; ++i) {
        char c = pathChar(e.name[i]);
        if (!c || (i && e.name[i - 1] == '.' && c == '.')) return false;
        if (c == '/') {
            if (base) return false;
            base = i + 1;
        }
    }
    if (base == 9) {
        const char* mac = "__macosx/";
        unsigned i = 0;
        while (i < base && pathChar(e.name[i]) == mac[i]) ++i;
        if (i == base) return false;
    }
    return true;
}
static bool extraCheck(const uint8_t* p, uint32_t n, SkinResult* r) {
    while (n) {
        if (n < 4 || le16(p + 2) > n - 4) return fail(r, SKE_ZIP_CORRUPT, "ZIP extra field truncated");
        if (le16(p) == 1) return fail(r, SKE_ZIP_UNSUPPORTED, "ZIP64 extra field");
        uint32_t len = 4u + le16(p + 2);
        p += len;
        n -= len;
    }
    return true;
}
static bool directory(const uint8_t* z, uint32_t size, Entry* entries, int& count,
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
    if (count > 512) return fail(r, SKE_TOO_BIG, "ZIP has more than 512 entries");
    if (!fits(cd, len, end) || cd + len != end) return fail(r, SKE_ZIP_CORRUPT, "ZIP directory bounds");
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
        if (!fits(dp, 12, cd) || le32(z + dp) != a.crc || le32(z + dp + 4) != a.packed || le32(z + dp + 8) != a.size)
            return fail(r, SKE_ZIP_CORRUPT, name);
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
    if (equalCI(s, "true") || equalCI(s, "yes")) { v = true; return true; }
    if (equalCI(s, "false") || equalCI(s, "no")) { v = false; return true; }
    return false;
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
            error = SKE_INFO_VALUE; why = "wrong type or out of range";
            char* text = scalars[0].text;
            uint32_t a = 0, b = 0;
            bool boolValue = false;
            double num = 0;
            if (!strcmp(key, "name") || !strcmp(key, "author") || !strcmp(key, "description")) {
                if (list || (!scalars[0].quoted && (boolean(text, boolValue) || number(text, num) || integer(text, a)))) break;
                if (!strcmp(key, "name")) copyText(pack.name, sizeof(pack.name), text);
                else if (!strcmp(key, "author")) copyText(pack.author, sizeof(pack.author), text);
                else copyText(pack.description, sizeof(pack.description), text);
            } else if (!strcmp(key, "hitFx") || !strcmp(key, "holdAtlas") || !strcmp(key, "holdAtlasMH")) {
                if (!list || n != 2 || scalars[0].quoted || scalars[1].quoted ||
                    !integer(text, a) || !integer(scalars[1].text, b)) break;
                if (!strcmp(key, "hitFx")) {
                    if (!a || !b || a > 64 || b > 64) break;
                    pack.hitFxCols = int(a); pack.hitFxRows = int(b);
                } else {
                    if (a > 4096 || b > 4096) break;
                    if (!strcmp(key, "holdAtlasMH")) { pack.holdTopMH = int(a); pack.holdBottomMH = int(b); mh = true; }
                    else { pack.holdTop = int(a); pack.holdBottom = int(b); }
                }
            } else if (!strcmp(key, "hitFxDuration") || !strcmp(key, "hitFxScale")) {
                if (list || scalars[0].quoted || !number(text, num) || !(num > 0 && num <= 10) || float(num) == 0) break;
                if (!strcmp(key, "hitFxDuration")) pack.hitFxDuration = float(num);
                else pack.hitFxScale = float(num);
            } else if (!strcmp(key, "colorPerfect") || !strcmp(key, "colorGood")) {
                if (list || scalars[0].quoted || !integer(text, a)) break;
                if (!strcmp(key, "colorPerfect")) pack.colorPerfect = a;
                else pack.colorGood = a;
            } else {
                bool* target = nullptr;
                if (!strcmp(key, "hitFxRotate")) target = &pack.hitFxRotate;
                else if (!strcmp(key, "hitFxTinted")) target = &pack.hitFxTinted;
                else if (!strcmp(key, "hideParticles")) target = &pack.hideParticles;
                else if (!strcmp(key, "holdKeepHead")) target = &pack.holdKeepHead;
                else if (!strcmp(key, "holdRepeat")) target = &pack.holdRepeat;
                else if (!strcmp(key, "holdCompact")) target = &pack.holdCompact;
                if (target) {
                    if (list || scalars[0].quoted || !boolean(text, boolValue)) break;
                    *target = boolValue;
                }
            }
            error = SKE_OK;
        } while (false);
        if (error != SKE_OK) {
            char detail[256];
            snprintf(detail, sizeof(detail), "info.yml line %u key %s: %s", line, key, why);
            ok = fail(r, error, detail);
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
static bool load(const uint8_t* z, uint32_t size, SkinPack& p, SkinResult* r) {
    if (size > 64u * 1024 * 1024) return fail(r, SKE_TOO_BIG, "ZIP input exceeds 64 MiB");
    Entry entries[512];
    int count = 0; uint32_t cd = 0;
    if (!directory(z, size, entries, count, cd, r)) return false;
    int chosen[SK_FILE_COUNT + 1], priority[3] = {3,3,3};
    for (int& c : chosen) c = -1;
    uint32_t prefixLen = 0;
    for (int i = 0; i < count; ++i) {
        uint32_t base;
        if (packName(entries[i], base) && nameEquals(entries[i], base, "info.yml")) {
            chosen[SK_FILE_COUNT] = i;
            prefixLen = base;
            break;
        }
    }
    if (chosen[SK_FILE_COUNT] < 0) return fail(r, SKE_NO_INFO, "info.yml missing at root or in one folder");
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
            static const char* const extensions[3] = {"ogg", "wav", "mp3"};
            for (int ext = 0; ext < priority[s]; ++ext) {
                char sound[32]; snprintf(sound, sizeof(sound), "%s.%s", stems[s], extensions[ext]);
                if (nameEquals(entries[i], base, sound)) { chosen[SK_SND_CLICK + s] = i; priority[s] = ext; break; }
            }
        }
    }
    uint32_t total = 0;
    for (int i = 0; i <= SK_FILE_COUNT; ++i) if (chosen[i] >= 0) {
        uint32_t n = entries[chosen[i]].size;
        if (n > 32u * 1024 * 1024 || n > 96u * 1024 * 1024 - total) return fail(r, SKE_TOO_BIG, names[i]);
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
    for (int i = 0; i < SK_FILE_COUNT; ++i) if (chosen[i] >= 0) {
        char name[32];
        const Entry& e = entries[chosen[i]];
        uint32_t n = e.nameSize - prefixLen;
        memcpy(name, e.name + prefixLen, n); name[n] = 0;
        if (!extract(z, cd, entries[chosen[i]], p.files[i], name, r)) return false;
        SkinBlob& b = p.files[i];
        if (i < SK_SND_CLICK) {
            static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
            if (b.size < 33 || memcmp(b.data, sig, 8) || be32(b.data + 8) != 13 || memcmp(b.data + 12, "IHDR", 4))
                return fail(r, SKE_BAD_IMAGE, name);
            uint32_t w = be32(b.data + 16), h = be32(b.data + 20);
            if (!w || !h || w > 4096 || h > 4096) return fail(r, SKE_BAD_IMAGE, name);
            b.w = int(w); b.h = int(h);
            if ((i == SK_HOLD && p.holdTop + p.holdBottom >= b.h) ||
                (i == SK_HOLD_MH && p.holdTopMH + p.holdBottomMH >= b.h) ||
                (i == SK_HIT_FX && (b.w < p.hitFxCols || b.h < p.hitFxRows))) return fail(r, SKE_BAD_IMAGE, name);
        } else {
            int ext = priority[i - SK_SND_CLICK];
            bool valid = ext == 0 ? b.size >= 4 && !memcmp(b.data, "OggS", 4) :
                ext == 1 ? b.size >= 12 && !memcmp(b.data, "RIFF", 4) && !memcmp(b.data + 8, "WAVE", 4) :
                (b.size >= 3 && !memcmp(b.data, "ID3", 3)) || (b.size >= 2 && b.data[0] == 255 && (b.data[1] & 224) == 224);
            if (!valid) return fail(r, SKE_BAD_SOUND, name);
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
