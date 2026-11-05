#include "../src/skinpack.h"

namespace {
int failures = 0, cases = 0;
void check(bool ok, const char* name) {
    ++cases;
    printf("%s ", ok ? "PASS" : "FAIL");
    for (const char* p = name; *p; ++p) {
        if (*p == '\n') printf("\\n");
        else if (*p == '\r') printf("\\r");
        else if (uint8_t(*p) >= 128) printf("\\x%02x", unsigned(uint8_t(*p)));
        else putchar(*p);
    }
    putchar('\n');
    if (!ok) ++failures;
}
uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | (unsigned(p[1]) << 8)); }
uint32_t u32(const uint8_t* p) { return u16(p) | (uint32_t(u16(p + 2)) << 16); }
void set16(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void set32(uint8_t* p, uint32_t v) { set16(p, v); set16(p + 2, v >> 16); }
uint32_t crc(const uint8_t* p, uint32_t n) {
    uint32_t v = 0xffffffff;
    for (uint32_t i = 0; i < n; ++i) {
        v ^= p[i];
        for (int j = 0; j < 8; ++j) v = v & 1 ? (v >> 1) ^ 0xedb88320 : v >> 1;
    }
    return v ^ 0xffffffff;
}
struct Buffer {
    uint8_t* p = nullptr;
    uint32_t n = 0, capacity = 0;
    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    ~Buffer() { free(p); }
    void add(const void* src, uint32_t len) {
        if (len > capacity - n) {
            uint32_t cap = (n + len) * 2 + 1024;
            uint8_t* next = static_cast<uint8_t*>(malloc(cap));
            if (!next) { fprintf(stderr, "test allocation failed\n"); exit(99); }
            if (n) memcpy(next, p, n);
            free(p); p = next; capacity = cap;
        }
        if (len) memcpy(p + n, src, len);
        n += len;
    }
    void word(uint32_t v) { uint8_t tmp[2]; set16(tmp, v); add(tmp, 2); }
    void dword(uint32_t v) { uint8_t tmp[4]; set32(tmp, v); add(tmp, 4); }
};
struct Item {
    char name[256];
    const uint8_t* data;
    uint32_t packed, raw, checksum;
    uint16_t method, flags;
};
Item item(const char* name, const uint8_t* p, uint32_t n) {
    Item a = {};
    snprintf(a.name, sizeof(a.name), "%s", name);
    a.data = p; a.packed = a.raw = n; a.checksum = crc(p, n);
    return a;
}
Item textItem(const char* name, const char* text) { return item(name, reinterpret_cast<const uint8_t*>(text), uint32_t(strlen(text))); }
void zip(Buffer& out, const Item* items, int n, const char* prefix = "", bool descriptors = false, const char* comment = "") {
    out.n = 0;
    uint32_t offsets[512];
    for (int i = 0; i < n; ++i) {
        const Item& a = items[i];
        offsets[i] = out.n;
        uint32_t nameLen = uint32_t(strlen(prefix) + strlen(a.name));
        uint16_t flags = uint16_t(a.flags | (descriptors ? 8 : 0));
        out.dword(0x04034b50); out.word(20); out.word(flags); out.word(a.method);
        out.dword(0); out.dword(descriptors ? 0 : a.checksum);
        out.dword(descriptors ? 0 : a.packed); out.dword(descriptors ? 0 : a.raw);
        out.word(nameLen); out.word(0);
        out.add(prefix, uint32_t(strlen(prefix))); out.add(a.name, uint32_t(strlen(a.name)));
        out.add(a.data, a.packed);
        if (descriptors) { out.dword(0x08074b50); out.dword(a.checksum); out.dword(a.packed); out.dword(a.raw); }
    }
    uint32_t cd = out.n;
    for (int i = 0; i < n; ++i) {
        const Item& a = items[i];
        out.dword(0x02014b50); out.word(20); out.word(20);
        out.word(a.flags | (descriptors ? 8 : 0)); out.word(a.method); out.dword(0);
        out.dword(a.checksum); out.dword(a.packed); out.dword(a.raw);
        out.word(uint32_t(strlen(prefix) + strlen(a.name))); out.word(0); out.word(0);
        out.word(0); out.word(0); out.dword(0); out.dword(offsets[i]);
        out.add(prefix, uint32_t(strlen(prefix))); out.add(a.name, uint32_t(strlen(a.name)));
    }
    uint32_t length = out.n - cd;
    out.dword(0x06054b50); out.word(0); out.word(0); out.word(n); out.word(n);
    out.dword(length); out.dword(cd); out.word(uint32_t(strlen(comment))); out.add(comment, uint32_t(strlen(comment)));
}
bool readFile(Buffer& b, const char* path) {
#ifdef _WIN32
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0) return false;
#else
    FILE* f = fopen(path, "rb");
#endif
    if (!f) return false;
    uint8_t bytes[4096]; size_t n;
    while ((n = fread(bytes, 1, sizeof(bytes), f)) != 0) b.add(bytes, uint32_t(n));
    bool ok = !ferror(f); fclose(f); return ok;
}
int readItems(const Buffer& z, Item* items) {
    if (z.n < 22) return 0;
    const uint8_t* end = z.p + z.n - 22;
    int count = u16(end + 10);
    uint32_t pos = u32(end + 16);
    if (count > 32) return 0;
    for (int i = 0; i < count; ++i) {
        const uint8_t* p = z.p + pos;
        Item& a = items[i]; a = {};
        uint16_t names = u16(p + 28), extra = u16(p + 30), comment = u16(p + 32);
        if (names >= sizeof(a.name)) return 0;
        memcpy(a.name, p + 46, names);
        a.method = u16(p + 10); a.flags = u16(p + 8) & ~8;
        a.checksum = u32(p + 16); a.packed = u32(p + 20); a.raw = u32(p + 24);
        const uint8_t* local = z.p + u32(p + 42);
        a.data = local + 30 + u16(local + 26) + u16(local + 28);
        pos += 46 + names + extra + comment;
    }
    return count;
}
const char* const filenames[SK_FILE_COUNT] = {
    "click.png", "click_mh.png", "drag.png", "drag_mh.png", "flick.png", "flick_mh.png",
    "hold.png", "hold_mh.png", "hit_fx.png", "click.ogg", "drag.ogg", "flick.ogg"
};
bool same(const SkinPack& a, const SkinPack& b) {
    SkinPack aa = a, bb = b;
    for (int i = 0; i < SK_FILE_COUNT; ++i) {
        const SkinBlob& x = a.files[i]; const SkinBlob& y = b.files[i];
        if (x.size != y.size || x.w != y.w || x.h != y.h || bool(x.data) != bool(y.data) ||
            (x.data && memcmp(x.data, y.data, x.size))) return false;
        aa.files[i].data = bb.files[i].data = nullptr;
    }
    return !memcmp(&aa, &bb, sizeof(aa));
}
bool empty(const SkinPack& p) { SkinPack zero = {}; return !memcmp(&zero, &p, sizeof(p)); }
void expect(const Buffer& b, SkinError error, const char* name, const char* detail = nullptr) {
    SkinPack p = {}; SkinResult r = {};
    bool ok = skinPackLoad(b.p, b.n, &p, &r);
    bool matched = ok == (error == SKE_OK) && r.err == error && (!detail || strstr(r.detail, detail)) && (ok || empty(p));
    if (!matched) printf("  got %s: %s\n", skinErrorName(r.err), r.detail);
    skinPackFree(&p); skinPackFree(&p);
    check(matched && empty(p), name);
}
const char* const sampleInfo =
    "name: \"Phigros \xe5\xae\x98\xe6\x96\xb9\xe8\xb5\x84\xe6\xba\x90\xe5\x8c\x85\"\n"
    "author: Pigeon Games vs. My_Eric\n"
    "description: \xe5\x9b\xbe\xe7\x89\x87\xe3\x80\x81\xe9\x9f\xb3\xe6\x95\x88\xe8\xb5\x84\xe6\xba\x90\xe6\x9d\xa5\xe8\x87\xaaPhigros\xef\xbc\x8c\xe7\x94\xb1My_Eric\xe8\xbf\x9b\xe8\xa1\x8c\xe6\x95\xb4\xe5\x90\x88\n"
    "hitFx: [5, 6]\nholdAtlas: [50, 50]\nholdAtlasMH: [50, 95]\nhitFxScale: 1.02\n";

struct Mini {
    uint8_t png[33] = {137,80,78,71,13,10,26,10,0,0,0,13,'I','H','D','R',0,0,0,64,0,0,7,208,8,6,0,0,0,0,0,0,0};
    Item items[32]; int count = 6;
    Mini() {
        items[0] = textItem("info.yml", "# defaults\n");
        const char* image[5] = {"click.png", "drag.png", "flick.png", "hold.png", "hit_fx.png"};
        for (int i = 0; i < 5; ++i) items[i + 1] = item(image[i], png, sizeof(png));
    }
    void info(const char* s) { items[0] = textItem("info.yml", s); }
    void run(SkinError e, const char* name, const char* detail = nullptr) { Buffer b; zip(b, items, count); expect(b, e, name, detail); }
};
struct Writer {
    Buffer data; unsigned bits = 0;
    void put(uint32_t v, int n) {
        for (int i = 0; i < n; ++i) {
            if (!(bits & 7)) { uint8_t zero = 0; data.add(&zero, 1); }
            data.p[bits / 8] |= uint8_t(((v >> i) & 1) << (bits & 7)); ++bits;
        }
    }
    void symbol(int s) {
        unsigned code; int n;
        if (s < 144) { code = unsigned(s) + 48; n = 8; }
        else if (s < 256) { code = unsigned(s) - 144 + 400; n = 9; }
        else if (s < 280) { code = unsigned(s) - 256; n = 7; }
        else { code = unsigned(s) - 280 + 192; n = 8; }
        for (int i = n - 1; i >= 0; --i) put((code >> i) & 1, 1);
    }
};
void compressed(Item& a, const Buffer& bytes, const char* text) {
    a = textItem("info.yml", text); a.data = bytes.p; a.packed = bytes.n; a.method = 8;
}

void sampleTests(Buffer& sample, SkinPack& pack) {
    SkinResult r = {};
    bool loaded = skinPackLoad(sample.p, sample.n, &pack, &r);
    if (!loaded) printf("  %s: %s\n", skinErrorName(r.err), r.detail);
    check(loaded, "PhiPack.zip loads");
    if (!loaded) return;
    check(!strcmp(pack.name, "Phigros \xe5\xae\x98\xe6\x96\xb9\xe8\xb5\x84\xe6\xba\x90\xe5\x8c\x85") &&
          !strcmp(pack.author, "Pigeon Games vs. My_Eric") && pack.hitFxCols == 5 && pack.hitFxRows == 6 &&
          pack.holdTop == 50 && pack.holdBottom == 50 && pack.holdTopMH == 50 && pack.holdBottomMH == 95 &&
          pack.hitFxScale == 1.02f && pack.hitFxDuration == 0.5f && !pack.hitFxRotate && pack.hitFxTinted &&
          !pack.hideParticles && !pack.holdKeepHead && !pack.holdRepeat && !pack.holdCompact &&
          pack.colorPerfect == 0xe1ffec9f && pack.colorGood == 0xebb4e1ff, "sample metadata and defaults");
    check(pack.files[SK_CLICK].w == 989 && pack.files[SK_CLICK].h == 100 && pack.files[SK_CLICK_MH].w == 1089 &&
          pack.files[SK_CLICK_MH].h == 200 && pack.files[SK_HOLD].w == 989 && pack.files[SK_HOLD].h == 2000 &&
          pack.files[SK_HOLD_MH].w == 1062 && pack.files[SK_HOLD_MH].h == 2048 &&
          pack.files[SK_HIT_FX].w == 1280 && pack.files[SK_HIT_FX].h == 1536, "sample PNG dimensions");
    Item original[32]; int count = readItems(sample, original);
    bool correct = count > 0;
    for (int i = 0; i < SK_FILE_COUNT; ++i) {
        bool found = false;
        for (int j = 0; j < count; ++j) if (!strcmp(original[j].name, filenames[i])) {
            const SkinBlob& b = pack.files[i];
            found = b.data && b.size == original[j].raw && crc(b.data, b.size) == original[j].checksum;
            if (i >= SK_SND_CLICK) found = found && b.w == 0 && b.h == 0 && b.size >= 4 && !memcmp(b.data, "OggS", 4);
        }
        correct = correct && found;
    }
    check(correct, "every sample blob matches independent CRC of original entry; all Ogg sounds present");
    for (int variant = 0; variant < 3; ++variant) {
        Buffer b;
        if (variant < 2) zip(b, original, count, variant ? "PACK\\" : "Pack/", variant == 1, "EOCD comment");
        else {
            Item stored[SK_FILE_COUNT + 1]; stored[0] = textItem("info.yml", sampleInfo);
            for (int i = 0; i < SK_FILE_COUNT; ++i) stored[i + 1] = item(filenames[i], pack.files[i].data, pack.files[i].size);
            zip(b, stored, SK_FILE_COUNT + 1);
        }
        SkinPack other = {}; bool ok = skinPackLoad(b.p, b.n, &other, &r);
        if (!ok) printf("  %s: %s\n", skinErrorName(r.err), r.detail);
        check(ok && same(pack, other), variant == 0 ? "sample compressed in one folder" : variant == 1 ?
              "sample backslash folder, data descriptors and ZIP comment" : "sample re-zipped stored matches all bytes");
        skinPackFree(&other);
    }
    Buffer bad; bad.add(sample.p, sample.n / 2); expect(bad, SKE_ZIP_CORRUPT, "truncated sample");
    bad.n = 0; bad.add(sample.p, sample.n);
    // Mutate the first real DEFLATE stream without changing its headers.
    uint32_t where = uint32_t(original[0].data - sample.p) + original[0].packed / 2;
    bad.p[where] ^= 32; expect(bad, SKE_ZIP_CORRUPT, "flipped DEFLATE byte");
    Item lying[32]; memcpy(lying, original, sizeof(original));
    for (int i = 0; i < count; ++i) if (!strcmp(lying[i].name, "click.png")) --lying[i].raw;
    zip(bad, lying, count); expect(bad, SKE_ZIP_CORRUPT, "inflate exceeds declared size without buffer overwrite");
}

void errorTests() {
    Buffer z; expect(z, SKE_NOT_ZIP, "empty buffer");
    z.add("not a zip at all", 16); expect(z, SKE_NOT_ZIP, "random bytes");
    {
        Mini m; m.run(SKE_OK, "minimal stored pack and absent optional files");
        zip(z, m.items, m.count); SkinPack p = {}; SkinResult r = {};
        bool ok = skinPackLoad(z.p, z.n, &p, &r);
        check(ok && !p.files[SK_CLICK_MH].data && !p.files[SK_HOLD_MH].data && !p.files[SK_SND_CLICK].data &&
              p.hitFxCols == 16 && p.hitFxRows == 1 && p.hitFxScale == 1 && p.holdTopMH == 0, "defaults and optional blobs remain null");
        skinPackFree(&p);
    }
    { Mini m; ++m.items[1].checksum; m.run(SKE_ZIP_CORRUPT, "CRC mismatch", "click.png"); }
    { Mini m; m.items[1].flags = 1; m.run(SKE_ZIP_UNSUPPORTED, "encrypted flag"); }
    { Mini m; m.items[1].method = 12; m.run(SKE_ZIP_UNSUPPORTED, "method 12"); }
    { Mini m; zip(z, m.items + 1, m.count - 1); expect(z, SKE_NO_INFO, "missing info.yml"); }
    { Mini m; m.items[4] = m.items[5]; --m.count; m.run(SKE_MISSING_FILE, "missing hold.png", "hold.png"); }
    { Mini m; m.info("# comment\nhitFx: [5, 6\n"); m.run(SKE_OK, "unterminated list skipped with line number", "line 2"); }
    { Mini m; m.info("hitFx: [0, 6]\n"); m.run(SKE_OK, "hitFx out of range skipped", "hitFx"); }
    { Mini m; m.info("holdAtlas: [1000, 1500]\n"); m.run(SKE_OK, "hold atlas taller than the PNG is shrunk", "hold.png"); }
    { Mini m; m.items[1] = textItem("click.png", "not PNG"); m.run(SKE_BAD_IMAGE, "bad PNG signature", "click.png"); }
    { Mini m; m.items[m.count++] = textItem("click.ogg", "invalid sound"); m.run(SKE_OK, "bad Ogg dropped, pack still loads", "click.ogg"); }
    { Mini m; m.items[1].raw = 1u << 30; m.run(SKE_TOO_BIG, "1 GiB declaration rejected before allocation"); }
    { Mini m; snprintf(m.items[0].name, 256, "../info.yml"); m.run(SKE_NO_INFO, "path traversal info ignored"); }
    { Mini m; snprintf(m.items[0].name, 256, "__MACOSX/info.yml"); m.run(SKE_NO_INFO, "Mac metadata ignored"); }
    { Mini m; for (int i = 0; i < m.count; ++i) { char n[256]; snprintf(n, 256, "a/b/%s", m.items[i].name); snprintf(m.items[i].name, 256, "%s", n); } m.run(SKE_OK, "pack two folder levels deep"); }
    { Mini m; snprintf(m.items[4].name, 256, "other/hold.png"); m.run(SKE_MISSING_FILE, "assets cannot come from another folder"); }
    { Mini m; m.items[0].raw = ~0u; m.run(SKE_ZIP_UNSUPPORTED, "ZIP64 size marker"); }
    { Mini m; zip(z, m.items, m.count); set16(z.p + z.n - 14, 4097); set16(z.p + z.n - 12, 4097); expect(z, SKE_TOO_BIG, "entry count cap"); }
    { Mini m; for (int i = 1; i <= 4; ++i) m.items[i].raw = 25u * 1024 * 1024; m.run(SKE_TOO_BIG, "total needed size cap before allocation"); }
    { Mini m; zip(z, m.items, m.count); uint32_t cd = u32(z.p + z.n - 6); set32(z.p + cd + 42, z.n - 2); expect(z, SKE_ZIP_CORRUPT, "local header offset out of range"); }
    { Mini m; zip(z, m.items, m.count); set32(z.p + z.n - 6, z.n); expect(z, SKE_ZIP_CORRUPT, "central offset out of range"); }
    { Mini m; zip(z, m.items, m.count); z.p[0] = 0; expect(z, SKE_ZIP_CORRUPT, "bad local signature"); }
    { Mini m; zip(z, m.items, m.count, "", true); z.p[30 + strlen(m.items[0].name) + m.items[0].packed + 4] ^= 1;
      expect(z, SKE_ZIP_CORRUPT, "corrupt data descriptor"); }
    { Mini m; m.png[19] = 0; for (int i = 1; i < 6; ++i) m.items[i].checksum = crc(m.png, 33); m.run(SKE_BAD_IMAGE, "zero PNG width"); }
    { Mini m; m.info("hitFx: [64, 1]\n"); m.png[19] = 63; for (int i = 1; i < 6; ++i) m.items[i].checksum = crc(m.png, 33);
      m.run(SKE_OK, "hit FX grid larger than the image is shrunk", "hitFx grid"); }
    { Mini m; m.items[m.count++] = item("hold_mh.png", m.png, 33); m.info("holdAtlasMH: [1000, 1000]\n"); m.run(SKE_OK, "multi-highlight atlas shrunk", "hold_mh.png"); }
    SkinPack p = {}; SkinResult r = {};
    bool ok = !skinPackLoad(nullptr, 64u * 1024 * 1024 + 1, &p, &r) && r.err == SKE_TOO_BIG;
    skinPackFree(&p); check(ok, "input size cap before reading memory");
}

void yamlTests() {
    Mini m; Buffer z;
    m.info("\xef\xbb\xbfname: \"A # B \\\" \\\\ \\n \\t \\u4e2d \\uD83D\\uDE00\" # tail\r\n"
           "author: 'it''s fine'\r\ndescription: plain text # comment\r\n"
           "hitFx: [0x10, 2]\r\nhitFxDuration: 1e-3\r\nhitFxScale: +1.02\r\n"
           "hitFxRotate: YeS\r\nhitFxTinted: NO\r\nhideParticles: TRUE\r\n"
           "holdKeepHead: yes\r\nholdRepeat: true\r\nholdCompact: YES\r\n"
           "colorPerfect: 0xffffffff\r\ncolorGood: 4294967295\r\n"
           "holdAtlas: [4, 5]\r\nunknown: [1, 2]\r\n");
    zip(z, m.items, m.count); SkinPack p = {}; SkinResult r = {};
    bool ok = skinPackLoad(z.p, z.n, &p, &r);
    if (!ok) printf("  %s %s\n", skinErrorName(r.err), r.detail);
    check(ok && !strcmp(p.name, "A # B \" \\ \n \t \xe4\xb8\xad \xf0\x9f\x98\x80") && !strcmp(p.author, "it's fine") &&
          !strcmp(p.description, "plain text") && p.hitFxCols == 16 && p.hitFxRows == 2 && p.hitFxDuration == 0.001f &&
          p.hitFxScale == 1.02f && p.hitFxRotate && !p.hitFxTinted && p.hideParticles && p.holdKeepHead && p.holdRepeat &&
          p.holdCompact && p.colorPerfect == ~0u && p.colorGood == ~0u && p.holdTopMH == 4 && p.holdBottomMH == 5,
          "BOM, CRLF, comments, quotes, escapes, Unicode, numbers, booleans and MH inheritance");
    skinPackFree(&p);
    const char* values[] = {
        "hitFx: [1]\n", "hitFx: [1, 2, 3]\n", "hitFx: [1.5, 2]\n", "hitFx: true\n", "hitFx: ['1', 2]\n",
        "hitFxDuration: 0\n", "hitFxDuration: -0.5\n", "hitFxDuration: 10.01\n", "hitFxScale: 1e9999\n",
        "hitFxScale: 1e-9999\n", "hitFxScale: NaN\n", "hitFxScale: '1'\n", "holdAtlas: [0, 4097]\n",
        "colorGood: 4294967296\n", "colorGood: 0x100000000\n", "colorGood: -1\n", "hitFxTinted: 1\n",
        "holdRepeat: 'true'\n", "name: [1, 2]\n", "author: true\n"
    };
    for (const char* s : values) { Mini v; v.info(s); char name[128]; snprintf(name, sizeof(name), "YAML bad value tolerated: %s", s); v.run(SKE_OK, name); }
    const char* syntax[] = {
        "hitFx: [1, 2\n", "name: \"unterminated\n", "name: 'unterminated\n", "name: \"\\q\"\n",
        "name: \"\\uD800\"\n", "name: \"\\uDC00\"\n", "name: \"\\u0000\"\n", "name: \"x\" extra\n",
        "name: {a: b}\n", "name: |\n", " name: nested\n", "no colon\n", "unknown: [[1], 2]\n",
        "unknown: [1,]\n", "name: \xc0\xaf\n", "unknown: &alias x\n", "name@: value\n", "name: bad\rcarriage\n"
    };
    for (const char* s : syntax) { Mini v; v.info(s); char name[128]; snprintf(name, sizeof(name), "YAML bad line skipped: %s", s); v.run(SKE_OK, name, "line 1"); }
    char longName[512]; memcpy(longName, "name: ", 6);
    for (int i = 0; i < 100; ++i) memcpy(longName + 6 + i * 3, "\xe4\xb8\xad", 3);
    longName[306] = '\n'; longName[307] = 0;
    Mini t; t.info(longName); zip(z, t.items, t.count);
    ok = skinPackLoad(z.p, z.n, &p, &r);
    check(ok && strlen(p.name) == 126 && !memcmp(p.name + 123, "\xe4\xb8\xad", 3), "UTF-8 truncation preserves character boundary"); skinPackFree(&p);
    { Mini v; v.info("holdAtlasMH: [6, 7]\nholdAtlas: [4, 5]\n"); zip(z, v.items, v.count);
      ok = skinPackLoad(z.p, z.n, &p, &r); check(ok && p.holdTopMH == 6 && p.holdBottomMH == 7, "explicit MH atlas independent of YAML key order"); skinPackFree(&p); }
    { Mini v; v.info("hitFxScale: 10\nhitFxDuration: .5\nunknown: plain\nunknownList: []\n"); v.run(SKE_OK, "integer float and fractional float accepted; unknown keys ignored"); }
    { Mini v; v.info("name:\nauthor: ''\ndescription: \"\"\n"); v.run(SKE_OK, "empty plain and quoted strings"); }
}

void soundAndInflateTests() {
    { Mini m; m.items[m.count++] = textItem("click.mp3", "ID3"); m.run(SKE_OK, "MP3 ID3 signature"); }
    { Mini m; const uint8_t sync[2] = {255,224}; m.items[m.count++] = item("click.mp3", sync, 2); m.run(SKE_OK, "MP3 MPEG sync"); }
    { Mini m; m.items[m.count++] = textItem("drag.wav", "RIFFabcdWAVE"); m.run(SKE_OK, "WAV RIFF WAVE signature"); }
    { Mini m; m.items[m.count++] = textItem("click.wav", "RIFFabcdNOPE"); m.run(SKE_OK, "WAV without WAVE dropped", "click.wav"); }
    { Mini m; m.items[m.count++] = textItem("click.mp3", "bad"); m.items[m.count++] = textItem("click.wav", "bad");
      m.items[m.count++] = textItem("click.ogg", "OggS"); m.run(SKE_OK, "Ogg wins over earlier WAV and MP3"); }
    { Mini m; m.items[m.count++] = textItem("click.mp3", "bad"); m.items[m.count++] = textItem("click.wav", "RIFFabcdWAVE");
      m.run(SKE_OK, "WAV wins over earlier MP3"); }
    { Mini m; m.items[m.count++] = textItem("click.ogg", "bad"); m.items[m.count++] = textItem("click.wav", "RIFFabcdWAVE");
      m.run(SKE_OK, "invalid preferred Ogg falls back to the WAV", "click.ogg");
      Buffer z; zip(z, m.items, m.count); SkinPack p = {}; SkinResult r = {};
      check(skinPackLoad(z.p, z.n, &p, &r) && p.files[SK_SND_CLICK].data && !memcmp(p.files[SK_SND_CLICK].data, "RIFF", 4), "fallback WAV kept"); skinPackFree(&p); }
    { Mini m; m.items[m.count++] = textItem("click.ogg", "RIFFabcdWAVE");
      Buffer z; zip(z, m.items, m.count); SkinPack p = {}; SkinResult r = {};
      check(skinPackLoad(z.p, z.n, &p, &r) && p.files[SK_SND_CLICK].data && !r.detail[0], "WAV data named .ogg is kept (format by content)"); skinPackFree(&p); }
    { Mini m; m.items[m.count++] = textItem("click.mp3", "ID3");
      Buffer z; zip(z, m.items, m.count); SkinPack p = {}; SkinResult r = {};
      check(skinPackLoad(z.p, z.n, &p, &r) && p.files[SK_SND_CLICK].data && !r.detail[0], "MP3 kept (decoded by audiodec)"); skinPackFree(&p); }
    { Mini m; m.items[m.count++] = textItem("drag.flac", "fLaC....");
      Buffer z; zip(z, m.items, m.count); SkinPack p = {}; SkinResult r = {};
      check(skinPackLoad(z.p, z.n, &p, &r) && p.files[SK_SND_DRAG].data && !r.detail[0], "FLAC kept"); skinPackFree(&p); }
    { Mini m; m.items[m.count++] = textItem("flick.mp3", "not audio at all");
      Buffer z; zip(z, m.items, m.count); SkinPack p = {}; SkinResult r = {};
      check(skinPackLoad(z.p, z.n, &p, &r) && !p.files[SK_SND_FLICK].data && strstr(r.detail, "flick.mp3"), "garbage .mp3 dropped with a note"); skinPackFree(&p); }
    { Mini m; static const uint8_t jpg[] = {0xff,0xd8,0xff,0xe0,0,4,0,0,0xff,0xc0,0,11,8,0,100,3,0xe8,3,1,0x11,0,0xff,0xd9};
      m.items[1] = item("click.png", jpg, sizeof jpg);
      Buffer z; zip(z, m.items, m.count); SkinPack p = {}; SkinResult r = {};
      check(skinPackLoad(z.p, z.n, &p, &r) && p.files[SK_CLICK].w == 1000 && p.files[SK_CLICK].h == 100, "JPEG named .png accepted with its size"); skinPackFree(&p); }
    { Mini m; m.items[m.count++] = textItem("click_mh.png", "broken");
      Buffer z; zip(z, m.items, m.count); SkinPack p = {}; SkinResult r = {};
      check(skinPackLoad(z.p, z.n, &p, &r) && !p.files[SK_CLICK_MH].data && strstr(r.detail, "click_mh.png"), "broken optional _mh image dropped"); skinPackFree(&p); }
    { Mini m; m.info("Hit_FX: ['5', 6.0]\ncolor_perfect: \"#ff0000\"\nColorGood: '#80112233'\nhitFxTinted: 'false'\n");
      Buffer z; zip(z, m.items, m.count); SkinPack p = {}; SkinResult r = {};
      bool ok = skinPackLoad(z.p, z.n, &p, &r);
      check(ok && p.hitFxCols == 5 && p.hitFxRows == 6 && p.colorPerfect == 0xffff0000u && p.colorGood == 0x80112233u && !p.hitFxTinted && !r.detail[0],
            "loose keys, quoted numbers and #RRGGBB colours"); skinPackFree(&p); }
    const char* text = "name: fixed Huffman\n";
    {
        Mini m; Writer w; w.put(1, 1); w.put(1, 2);
        for (const char* p = text; *p; ++p) w.symbol(uint8_t(*p));
        w.symbol(256); compressed(m.items[0], w.data, text); m.run(SKE_OK, "fixed Huffman DEFLATE block");
        --m.items[0].packed; m.run(SKE_ZIP_CORRUPT, "truncated fixed Huffman block");
    }
    {
        Mini m; Writer w; w.put(1, 1); w.put(1, 2);
        for (const char* p = "name: A"; *p; ++p) w.symbol(uint8_t(*p));
        w.symbol(264); w.put(0, 5); w.symbol(256);
        compressed(m.items[0], w.data, "name: AAAAAAAAAAA"); m.run(SKE_OK, "overlapping DEFLATE length/distance copy");
    }
    {
        Mini m; Buffer bytes; uint8_t header = 1; bytes.add(&header, 1);
        bytes.word(uint32_t(strlen(text))); bytes.word(65535u ^ uint32_t(strlen(text))); bytes.add(text, uint32_t(strlen(text)));
        compressed(m.items[0], bytes, text); m.run(SKE_OK, "uncompressed DEFLATE block");
        bytes.p[3] ^= 1; m.run(SKE_ZIP_CORRUPT, "stored DEFLATE LEN/NLEN mismatch");
    }
    { Mini m; uint8_t reserved = 7; m.items[0].data = &reserved; m.items[0].packed = 1; m.items[0].method = 8;
      m.run(SKE_ZIP_CORRUPT, "reserved DEFLATE block type"); }
    {
        Mini m; Writer w; w.put(1, 1); w.put(1, 2); w.symbol(257); w.put(0, 5); w.symbol(256);
        compressed(m.items[0], w.data, "abc"); m.run(SKE_ZIP_CORRUPT, "DEFLATE distance before start of output");
    }
    {
        Mini m; Writer w; w.put(1, 1); w.put(2, 2); w.put(0, 5); w.put(0, 5); w.put(0, 4);
        for (int i = 0; i < 4; ++i) w.put(1, 3);
        compressed(m.items[0], w.data, "x"); m.run(SKE_ZIP_CORRUPT, "oversubscribed dynamic Huffman code tree");
    }
    {
        Mini m; Buffer z; for (int i = 0; i < m.count; ++i) for (char* c = m.items[i].name; *c; ++c) if (*c >= 'a' && *c <= 'z') *c -= 32;
        zip(z, m.items, m.count, "PaCk\\"); expect(z, SKE_OK, "case insensitive filenames and backslash paths");
    }
    {
        Mini m; Buffer z; char prefix[601]; memset(prefix, 'a', 599); prefix[599] = '/'; prefix[600] = 0;
        zip(z, m.items, m.count, prefix); expect(z, SKE_OK, "long top-level folder names use ZIP name bounds");
    }
}
void mutationTests() {
    Mini m; Buffer original, changed; zip(original, m.items, m.count);
    bool safe = true; uint32_t rng = 0x12345678;
    for (int i = 0; i < 512; ++i) {
        changed.n = 0; changed.add(original.p, original.n);
        rng = rng * 1664525u + 1013904223u; uint32_t pos = rng % changed.n;
        changed.p[pos] ^= uint8_t(1u << ((rng >> 16) & 7));
        SkinPack p = {}; SkinResult r = {};
        bool ok = skinPackLoad(changed.p, changed.n, &p, &r);
        safe = safe && ((ok && r.err == SKE_OK) || (!ok && r.err != SKE_OK && empty(p)));
        skinPackFree(&p);
    }
    check(safe, "512 deterministic ZIP mutations leave valid ownership/error state");
    for (uint32_t n = 0; n < original.n; ++n) {
        SkinPack p = {}; SkinResult r = {};
        bool ok = skinPackLoad(original.p, n, &p, &r);
        safe = safe && !ok && empty(p); skinPackFree(&p);
    }
    check(safe, "every truncation of minimal ZIP fails safely");
}
} // namespace

int main() {
    Buffer sample; SkinPack p = {};
    bool found = readFile(sample, "data/PhiPack.zip"); check(found, "sample fixture is readable");
    if (found) sampleTests(sample, p);
    skinPackFree(&p); skinPackFree(&p);
    errorTests(); yamlTests(); soundAndInflateTests(); mutationTests();
    check(!strcmp(skinErrorName(SKE_NO_MEMORY), "SKE_NO_MEMORY") && !strcmp(skinErrorName(SKE_OK), "SKE_OK"), "error names for logs");
    printf("%d cases, %d failures\n", cases, failures);
    return failures;
}
