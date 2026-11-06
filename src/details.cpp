// 成绩详情: accuracy to 4 decimals and judgement counts in song select, plus a details card.
//
// What the game saves (4.0.x), per song and level, is LevelRecord {s: score, a: accuracy in percent (float),
// c: full combo}. LevelRecord.UpdateRecord keeps each one's best on its own (max score, max accuracy, FC once
// ever), and only that goes into the save and the cloud. So:
//   - the 4-decimal accuracy comes straight from the record (a float holds it to about 0.00001 %), cloud
//     records included; we never write the record
//   - judgement counts are not saved by the game at all. The result screen's LevelResultInfo has them (perfect,
//     good, bad, miss, early, late, maxCombo); we keep the best play of each chart (highest score, then
//     accuracy, then Criticals) in our own JSON file on this device, Criticals too when 区分大小 P was on
// Song select: SongSelector.SetText writes score (D7) and acc (a.ToString("F2") + "%") from
// SongItems[nowSong].records[nowLevel]. Every frame we put the 4-decimal accuracy in place of the game's acc
// string (only when the game's string is that record's value), keep a small judgement line under it, and the
// score / acc open the details card when tapped.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

// ======================= storage: records.json =======================
#if defined(__APPLE__)
static const char* const kFile = "PhiMeMod.records.json";  // <app>/Documents, next to PhiMeMod.log
#else
static const char* const kFile = "records.json";  // /sdcard/Android/media/<pkg>/PhiMeMod, next to the log
#endif

struct Detail {
    char key[224];  // "<song id>|<level>", "|course" appended for course-mode runs
    int score;
    double acc;  // percent
    bool fc;
    int perfect, good, bad, miss, early, late, maxCombo;
    int critical;  // Criticals among the Perfects, -1 = 区分大小 P was off
    int plays;     // results recorded for this chart on this device
    long long time;  // unix time of the best play
};
static Detail* g_det = nullptr;
static int g_detN = 0, g_detCap = 0, g_detVersion = 0;
static bool g_detLoaded = false;

static bool dataPath(char* out, int cap) {
#if defined(__APPLE__)
    const char* home = getenv("HOME");
    if (!home) return false;
    snprintf(out, cap, "%s/Documents/%s", home, kFile);
#else
    char pkg[160] = {0};
    int fd = open("/proc/self/cmdline", 0);
    if (fd < 0) return false;
    ssize_t n = read(fd, pkg, sizeof(pkg) - 1);
    close(fd);
    if (n <= 0) return false;
    for (int i = 0; pkg[i]; i++)
        if (pkg[i] == ':') pkg[i] = 0;
    snprintf(out, cap, "/sdcard/Android/media/%s", pkg);
    mkdir(out, 0770);
    snprintf(out, cap, "/sdcard/Android/media/%s/PhiMeMod", pkg);
    mkdir(out, 0770);
    snprintf(out, cap, "/sdcard/Android/media/%s/PhiMeMod/%s", pkg, kFile);
#endif
    return true;
}

// growing text buffer
struct Buf {
    char* p = nullptr;
    int n = 0, cap = 0;
    bool grow(int more) {
        if (n + more + 1 <= cap) return true;
        int c = cap ? cap : 4096;
        while (c < n + more + 1) c *= 2;
        char* q = (char*)malloc(c);
        if (!q) return false;
        if (p) {
            memcpy(q, p, n);
            free(p);
        }
        p = q;
        cap = c;
        return true;
    }
    void add(const char* s, int len) {
        if (len <= 0 || !grow(len)) return;
        memcpy(p + n, s, len);
        n += len;
        p[n] = 0;
    }
    void addf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char tmp[512];
        __builtin_va_list ap;
        __builtin_va_start(ap, fmt);
        int len = vsnprintf(tmp, sizeof tmp, fmt, ap);
        __builtin_va_end(ap);
        if (len >= (int)sizeof tmp) len = (int)sizeof tmp - 1;
        add(tmp, len);
    }
    void addJsonString(const char* s) {
        add("\"", 1);
        for (; *s; s++) {
            unsigned char c = (unsigned char)*s;
            if (c == '"' || c == '\\') {
                char t[2] = {'\\', (char)c};
                add(t, 2);
            } else if (c < 0x20) {
                char t[8];
                add(t, snprintf(t, sizeof t, "\\u%04x", c));
            } else {
                add(s, 1);
            }
        }
        add("\"", 1);
    }
};

static Detail* detFind(const char* key, bool create) {
    for (int i = 0; i < g_detN; i++)
        if (strcmp(g_det[i].key, key) == 0) return &g_det[i];
    if (!create) return nullptr;
    if (g_detN == g_detCap) {
        int c = g_detCap ? g_detCap * 2 : 256;
        Detail* q = (Detail*)malloc(sizeof(Detail) * c);
        if (!q) return nullptr;
        if (g_det) {
            memcpy(q, g_det, sizeof(Detail) * g_detN);
            free(g_det);
        }
        g_det = q;
        g_detCap = c;
    }
    Detail* d = &g_det[g_detN++];
    memset(d, 0, sizeof *d);
    d->critical = -1;
    snprintf(d->key, sizeof d->key, "%s", key);
    g_detVersion++;
    return d;
}

// ---- a small JSON reader (enough for our own file, tolerant of extra fields) ----
struct JP {
    const char *s, *e;
};
static void jws(JP& p) {
    while (p.s < p.e && (*p.s == ' ' || *p.s == '\n' || *p.s == '\r' || *p.s == '\t')) p.s++;
}
static bool jlit(JP& p, char c) {
    jws(p);
    if (p.s < p.e && *p.s == c) {
        p.s++;
        return true;
    }
    return false;
}
static bool jword(JP& p, const char* w) {
    jws(p);
    int L = (int)strlen(w);
    if (p.e - p.s >= L && memcmp(p.s, w, L) == 0) {
        p.s += L;
        return true;
    }
    return false;
}
static bool jstr(JP& p, char* out, int cap) {
    jws(p);
    if (p.s >= p.e || *p.s != '"') return false;
    p.s++;
    int n = 0;
    auto put = [&](char c) {
        if (out && n < cap - 1) out[n++] = c;
    };
    while (p.s < p.e && *p.s != '"') {
        char c = *p.s++;
        if (c != '\\') {
            put(c);
            continue;
        }
        if (p.s >= p.e) return false;
        char e = *p.s++;
        if (e == 'u') {
            if (p.e - p.s < 4) return false;
            unsigned v = 0;
            for (int i = 0; i < 4; i++) {
                char h = *p.s++;
                v <<= 4;
                if (h >= '0' && h <= '9') v |= h - '0';
                else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
                else return false;
            }
            if (v < 0x80) put((char)v);
            else if (v < 0x800) {
                put((char)(0xC0 | (v >> 6)));
                put((char)(0x80 | (v & 63)));
            } else {
                put((char)(0xE0 | (v >> 12)));
                put((char)(0x80 | ((v >> 6) & 63)));
                put((char)(0x80 | (v & 63)));
            }
            continue;
        }
        put(e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == 'b' ? '\b' : e == 'f' ? '\f' : e);
    }
    if (p.s >= p.e) return false;
    p.s++;
    if (out && cap > 0) out[n] = 0;
    return true;
}
static bool jnum(JP& p, double* v) {
    jws(p);
    const char* s = p.s;
    double sign = 1, x = 0;
    if (s < p.e && *s == '-') {
        sign = -1;
        s++;
    }
    if (s >= p.e || *s < '0' || *s > '9') return false;
    while (s < p.e && *s >= '0' && *s <= '9') x = x * 10 + (*s++ - '0');
    if (s < p.e && *s == '.') {
        s++;
        double f = 0.1;
        while (s < p.e && *s >= '0' && *s <= '9') {
            x += (*s++ - '0') * f;
            f *= 0.1;
        }
    }
    if (s < p.e && (*s == 'e' || *s == 'E')) {
        s++;
        bool neg = false;
        if (s < p.e && (*s == '+' || *s == '-')) neg = *s++ == '-';
        int ex = 0;
        while (s < p.e && *s >= '0' && *s <= '9' && ex < 400) ex = ex * 10 + (*s++ - '0');
        while (ex-- > 0) x = neg ? x / 10 : x * 10;
    }
    p.s = s;
    *v = sign * x;
    return true;
}
static bool jskip(JP& p, int depth = 0) {
    jws(p);
    if (p.s >= p.e || depth > 32) return false;
    char c = *p.s;
    if (c == '"') return jstr(p, nullptr, 0);
    if (c == '{' || c == '[') {
        char closing = c == '{' ? '}' : ']';
        p.s++;
        if (jlit(p, closing)) return true;
        for (;;) {
            if (c == '{' && (!jstr(p, nullptr, 0) || !jlit(p, ':'))) return false;
            if (!jskip(p, depth + 1)) return false;
            if (jlit(p, ',')) continue;
            return jlit(p, closing);
        }
    }
    if (jword(p, "true") || jword(p, "false") || jword(p, "null")) return true;
    double v;
    return jnum(p, &v);
}

static bool parseDetail(JP& p, Detail* d) {
    if (!jlit(p, '{')) return false;
    if (jlit(p, '}')) return true;
    for (;;) {
        char k[32];
        if (!jstr(p, k, sizeof k) || !jlit(p, ':')) return false;
        double v = 0;
        bool num = true;
        if (jword(p, "true")) v = 1;
        else if (jword(p, "false") || jword(p, "null")) v = 0;
        else if (!jnum(p, &v)) {
            num = false;
            if (!jskip(p)) return false;
        }
        if (num) {
            int iv = (int)v;
            if (!strcmp(k, "score")) d->score = iv;
            else if (!strcmp(k, "acc")) d->acc = v;
            else if (!strcmp(k, "fc")) d->fc = v != 0;
            else if (!strcmp(k, "perfect")) d->perfect = iv;
            else if (!strcmp(k, "good")) d->good = iv;
            else if (!strcmp(k, "bad")) d->bad = iv;
            else if (!strcmp(k, "miss")) d->miss = iv;
            else if (!strcmp(k, "early")) d->early = iv;
            else if (!strcmp(k, "late")) d->late = iv;
            else if (!strcmp(k, "maxCombo")) d->maxCombo = iv;
            else if (!strcmp(k, "critical")) d->critical = iv;
            else if (!strcmp(k, "plays")) d->plays = iv;
            else if (!strcmp(k, "time")) d->time = (long long)v;
        }
        if (jlit(p, ',')) continue;
        if (d->critical > d->perfect) d->critical = -1;  // the Criticals are a part of the Perfects; anything else is bad data
        return jlit(p, '}');
    }
}

static void detLoad() {
    if (g_detLoaded) return;
    g_detLoaded = true;
    char path[512];
    if (!dataPath(path, sizeof path)) return;
    int fd = open(path, 0);
    if (fd < 0) {
        LOG("details: no %s yet", path);
        return;
    }
    Buf b;
    char tmp[4096];
    for (;;) {
        ssize_t r = read(fd, tmp, sizeof tmp);
        if (r <= 0) break;
        b.add(tmp, (int)r);
    }
    close(fd);
    JP p{b.p, b.p + b.n};
    bool ok = b.p && jlit(p, '{');
    if (ok && !jlit(p, '}'))
        for (;;) {
            char k[64];
            if (!jstr(p, k, sizeof k) || !jlit(p, ':')) {
                ok = false;
                break;
            }
            if (strcmp(k, "records") == 0) {
                if (!jlit(p, '{')) {
                    ok = false;
                    break;
                }
                if (!jlit(p, '}'))
                    for (;;) {
                        Detail d;
                        memset(&d, 0, sizeof d);
                        d.critical = -1;
                        if (!jstr(p, d.key, sizeof d.key) || !jlit(p, ':') || !parseDetail(p, &d)) {
                            ok = false;
                            break;
                        }
                        Detail* x = d.key[0] ? detFind(d.key, true) : nullptr;
                        if (x) *x = d;
                        if (jlit(p, ',')) continue;
                        if (!jlit(p, '}')) ok = false;
                        break;
                    }
                if (!ok) break;
            } else if (!jskip(p)) {
                ok = false;
                break;
            }
            if (jlit(p, ',')) continue;
            if (!jlit(p, '}')) ok = false;
            break;
        }
    free(b.p);
    g_detVersion++;
    if (!ok) {
        // keep the broken file for a look, the next save writes a fresh one (with what could be read)
        char bad[540];
        snprintf(bad, sizeof bad, "%s.bad", path);
        rename(path, bad);
        LOG("details: %s could not be read completely (%d records kept), moved to .bad", path, g_detN);
        return;
    }
    LOG("details: %d records from %s", g_detN, path);
}

static void makeKey(char* out, int cap, const char* id, const char* lv, bool course);

static void writeRecords(Buf& b) {
    b.addf("\n  \"records\": {");
    for (int i = 0; i < g_detN; i++) {
        const Detail& d = g_det[i];
        b.addf("%s\n    ", i ? "," : "");
        b.addJsonString(d.key);
        // perfect = every Perfect; "critical" (the Criticals among them) only when 区分大小 P was on for that play
        b.addf(": {\"score\": %d, \"acc\": %.6f, \"fc\": %s, \"perfect\": %d, \"good\": %d, \"bad\": %d, \"miss\": %d, "
               "\"early\": %d, \"late\": %d, \"maxCombo\": %d, ",
               d.score, d.acc, d.fc ? "true" : "false", d.perfect, d.good, d.bad, d.miss, d.early, d.late, d.maxCombo);
        if (d.critical >= 0) b.addf("\"critical\": %d, ", d.critical);
        b.addf("\"plays\": %d, \"time\": %lld}", d.plays, d.time);
    }
    b.addf("\n  }");
}

// write `b` to `path` through a temporary file (a crash mid-write never leaves half a file)
static bool writeFileAtomic(const char* path, Buf& b) {
    char tmp[560];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    bool ok = false;
    int fd = open(tmp, PM_O_REPLACE, 0660);
    if (fd >= 0 && b.p) {
        ssize_t w = write(fd, b.p, b.n);
        close(fd);
        ok = w == b.n && rename(tmp, path) == 0;
    }
    free(b.p);
    b.p = nullptr;
    if (!ok) LOG("details: could not write %s", path);
    return ok;
}

static bool detSave() {
    char path[512];
    if (!dataPath(path, sizeof path)) return false;
    Buf b;
    b.addf("{\n  \"version\": 1,\n  \"about\": \"PhiMe: judgement counts of the best play of each chart on this "
           "device. Scores and accuracy themselves live in the game save.\",");
    writeRecords(b);
    b.addf("\n}\n");
    return writeFileAtomic(path, b);
}

// a is a better play than b: the higher accuracy, then more Criticals, then the higher score (the judgements kept are
// those of the best-accuracy play, the same rule as at the result screen)
static bool better(const Detail& a, const Detail& b) {
    return a.acc > b.acc + 1e-6 ||
           (a.acc > b.acc - 1e-6 && (a.critical > b.critical || (a.critical == b.critical && a.score > b.score)));
}

// ======================= the PhiMe save file (export / import) =======================
// {"format": "PhiMe", "version": 1, "records": {...as records.json...}, "course": {"<id>|<level>": {score, acc, fc}}}
struct CourseOut {
    Buf* b;
    int n;
};
static void courseOne(const char* id, const char* lv, void* u) {
    CourseOut* o = (CourseOut*)u;
    int s;
    float a;
    bool c;
    if (!courseRecGet(id, lv, &s, &a, &c)) return;
    char key[224];
    makeKey(key, sizeof key, id, lv, false);
    o->b->addf("%s\n    ", o->n++ ? "," : "");
    o->b->addJsonString(key);
    o->b->addf(": {\"score\": %d, \"acc\": %.4f, \"fc\": %s}", s, a, c ? "true" : "false");
}

bool detailsExportFile(const char* path, int* records, int* course) {
    detLoad();
    Buf b;
    b.addf("{\n  \"format\": \"PhiMe\",\n  \"version\": 1,\n  \"exported\": %lld,\n  \"about\": \"PhiMe save: judgement "
           "counts of the best play of each chart (records) and course-mode records (course).\",",
           (long long)time(nullptr));
    writeRecords(b);
    b.addf(",\n  \"course\": {");
    CourseOut o{&b, 0};
    forEachChart(courseOne, &o);
    b.addf("\n  }\n}\n");
    *records = g_detN;
    *course = o.n;
    return writeFileAtomic(path, b);
}

// merges: every chart keeps the better play (and the larger play count); course records keep each best
bool detailsImportFile(const char* path, int* records, int* course) {
    *records = *course = 0;
    detLoad();
    int fd = open(path, 0);
    if (fd < 0) return false;
    Buf b;
    char tmp[4096];
    for (;;) {
        ssize_t r = read(fd, tmp, sizeof tmp);
        if (r <= 0) break;
        b.add(tmp, (int)r);
    }
    close(fd);
    JP p{b.p, b.p + b.n};
    bool ok = b.p && jlit(p, '{'), course_ = false;
    if (ok && !jlit(p, '}'))
        for (;;) {
            char k[64];
            if (!jstr(p, k, sizeof k) || !jlit(p, ':')) {
                ok = false;
                break;
            }
            bool isRec = strcmp(k, "records") == 0;
            course_ = strcmp(k, "course") == 0;
            if (isRec || course_) {
                if (!jlit(p, '{')) {
                    ok = false;
                    break;
                }
                if (!jlit(p, '}'))
                    for (;;) {
                        Detail d;
                        memset(&d, 0, sizeof d);
                        d.critical = -1;
                        if (!jstr(p, d.key, sizeof d.key) || !jlit(p, ':') || !parseDetail(p, &d)) {
                            ok = false;
                            break;
                        }
                        if (isRec && d.key[0]) {
                            Detail* x = detFind(d.key, false);
                            int plays = x && x->plays > d.plays ? x->plays : d.plays;
                            if (!x) x = detFind(d.key, true);
                            if (x && (x->score == 0 && x->perfect == 0 ? true : better(d, *x))) *x = d;
                            if (x) x->plays = plays;
                            (*records)++;
                        } else if (d.key[0]) {
                            // "<id>|<level>": the level is after the last '|'
                            char* bar = nullptr;
                            for (char* q = d.key; *q; q++)
                                if (*q == '|') bar = q;
                            if (bar) {
                                *bar = 0;
                                courseRecMerge(d.key, bar + 1, d.score, (float)d.acc, d.fc);
                                (*course)++;
                            }
                        }
                        if (jlit(p, ',')) continue;
                        if (!jlit(p, '}')) ok = false;
                        break;
                    }
                if (!ok) break;
            } else if (!jskip(p)) {
                ok = false;
                break;
            }
            if (jlit(p, ',')) continue;
            if (!jlit(p, '}')) ok = false;
            break;
        }
    free(b.p);
    g_detVersion++;
    if (*records) detSave();
    if (*course) prefSave();
    LOG("details: imported %s: %d records, %d course records%s", path, *records, *course, ok ? "" : " (file cut short)");
    return ok || *records || *course;
}

static void makeKey(char* out, int cap, const char* id, const char* lv, bool course) {
    snprintf(out, cap, "%s|%s%s", id, lv, course ? "|course" : "");
}

// ======================= game classes =======================
static struct {
    bool ok, ssOk;
    Il2CppClass *LOC, *GI, *Text;
    int lsiId, lsiLevel;
    int rPerfect, rGood, rBad, rMiss, rEarly, rLate, rCombo;
    const MethodInfo *rScore, *rPercent, *rFC;
    const MethodInfo *ssItems, *txtOverflow;
    int ssNowSong, ssNowLevel, ssScore, ssAcc, ssTopBar, ssStartAnim, ssExit, itemCover;
    int ssLevelBar, lbInfos, liMain, ssRating, rdObjects;  // the level bar and the rank badge, to centre the score
    int itemSong, itemRecords, songId, songName, songComposer, songLevels, songDiff;
    int recS, recA, recC;
    bool recCBool;
} D;

static FieldInfo* field(Il2CppClass* k, const char* name) { return k ? il2cpp_class_get_field_from_name(k, name) : nullptr; }
static int fieldType(Il2CppClass* k, const char* name) {
    FieldInfo* f = field(k, name);
    return f ? il2cpp_type_get_type(il2cpp_field_get_type(f)) : -1;
}
static Obj* objField(Il2CppClass* k, Obj* inst, const char* name) {
    FieldInfo* f = field(k, name);
    if (!f) return nullptr;
    if (il2cpp_field_get_flags(f) & 0x10) {  // static
        Obj* v = nullptr;
        il2cpp_field_static_get_value(f, &v);
        return v;
    }
    return inst ? fld<Obj*>(inst, (int)il2cpp_field_get_offset(f)) : nullptr;
}

// elements of a List<T> or a T[] (offsets cached per class)
static int seq(Obj* o, void*** out) {
    *out = nullptr;
    if (!o) return 0;
    static Il2CppClass* ck[8];
    static int cItems[8], cSize[8], cn = 0;
    Il2CppClass* k = il2cpp_object_get_class(o);
    int i = 0;
    while (i < cn && ck[i] != k) i++;
    int itemsOff = -1, sizeOff = -1;
    if (i < cn) {
        itemsOff = cItems[i];
        sizeOff = cSize[i];
    } else {
        FieldInfo* fi = il2cpp_class_get_field_from_name(k, "_items");
        FieldInfo* fs = il2cpp_class_get_field_from_name(k, "_size");
        if (fi && fs) {
            itemsOff = (int)il2cpp_field_get_offset(fi);
            sizeOff = (int)il2cpp_field_get_offset(fs);
        }
        if (cn < 8) {
            ck[cn] = k;
            cItems[cn] = itemsOff;
            cSize[cn] = sizeOff;
            cn++;
        }
    }
    Il2CppArray* a;
    int n;
    if (itemsOff >= 0) {
        a = fld<Il2CppArray*>(o, itemsOff);
        n = fld<int>(o, sizeOff);
        if (!a || n < 0 || (uintptr_t)n > a->max_length) return 0;
    } else {
        const char* nm = il2cpp_class_get_name(k);
        size_t L = strlen(nm);
        if (L < 2 || nm[L - 2] != '[' || nm[L - 1] != ']') return 0;
        a = (Il2CppArray*)o;
        n = (int)a->max_length;
    }
    *out = arrayData(a);
    return n;
}

static Obj* gameInfo() { return D.GI ? objField(D.GI, nullptr, "_main") : nullptr; }

static int recScore(Obj* r) { return fld<int>(r, D.recS); }
static float recAcc(Obj* r) { return fld<float>(r, D.recA); }
static bool recFC(Obj* r) { return D.recCBool ? fld<bool>(r, D.recC) : fld<int>(r, D.recC) != 0; }

// 98.76543 -> "98.7654%": cut (not rounded) to 4 decimals, never 100.0000% below 100. The record is a float
// (about 0.000004 % off at most), so a hair is added before cutting.
static void fmtAcc(char* b, int cap, double a) {
    long k = (long)(a * 10000 + 0.05);
    if (a < 100 && k >= 1000000) k = 999999;
    if (k < 0) k = 0;
    snprintf(b, cap, "%ld.%04ld%%", k / 10000, k % 10000);
}

static double rks(double acc, float constant) {
    if (acc < 70 || constant <= 0) return 0;
    double f = (acc - 55) / 45;
    return f * f * constant;
}

// ======================= results: remember this play =======================
static Handle g_resLoc;
static bool g_resPending = false, g_resCourse = false;
static float g_resT0 = 0;

static bool songOf(Obj* lsi, char* id, int idCap, char* lv, int lvCap) {
    id[0] = lv[0] = 0;
    if (!lsi || D.lsiId < 0 || D.lsiLevel < 0) return false;
    utf8(fld<Il2CppString*>(lsi, D.lsiId), id, idCap);
    utf8(fld<Il2CppString*>(lsi, D.lsiLevel), lv, lvCap);
    return id[0] && lv[0];
}

static int intField(Obj* o, int off) { return off >= 0 ? fld<int>(o, off) : 0; }

// LevelOverControl.Start is a coroutine: its _levelStartInfo / levelResultInfo appear a frame or two later
static bool takeResult(Obj* loc, bool lastTry) {
    Obj* gi = gameInfo();
    Obj* lsi = objField(D.LOC, loc, "_levelStartInfo");
    Obj* lri = objField(D.LOC, loc, "levelResultInfo");
    if ((!lsi || !lri) && !lastTry) return false;
    if (!lsi && gi) lsi = objField(D.GI, gi, "levelStartInfo");
    if (!lri && gi) lri = objField(D.GI, gi, "levelResultInfo");
    char id[160], lv[16];
    if (!lri || !songOf(lsi, id, sizeof id, lv, sizeof lv)) {
        LOG("details: result without song info (result %p, start %p)", (void*)lri, (void*)lsi);
        return true;
    }
    Detail pl;
    memset(&pl, 0, sizeof pl);
    pl.score = callm<int>(D.rScore, lri);
    pl.acc = callm<float>(D.rPercent, lri);
    pl.fc = callm<bool>(D.rFC, lri);
    pl.perfect = intField(lri, D.rPerfect);
    pl.good = intField(lri, D.rGood);
    pl.bad = intField(lri, D.rBad);
    pl.miss = intField(lri, D.rMiss);
    pl.early = intField(lri, D.rEarly);
    pl.late = intField(lri, D.rLate);
    pl.maxCombo = intField(lri, D.rCombo);
    pl.critical = critResultCount(pl.perfect);
    pl.time = time(nullptr);
    if (pl.perfect + pl.good + pl.bad + pl.miss <= 0) {
        LOG("details: %s %s result has no judgements, not kept", id, lv);
        return true;
    }
    detLoad();
    char key[224];
    makeKey(key, sizeof key, id, lv, g_resCourse);
    Detail* d = detFind(key, false);
    // kept only when its accuracy is the best yet: better than our kept play AND than the save's record from before
    // this play (a better play may never have been seen here: played before PhiMe, or on another device)
    float bestAcc = d ? d->acc : -1, saveAcc = -1;
    if (resAccOldBest(&saveAcc) && saveAcc > bestAcc) bestAcc = saveAcc;
    bool better = pl.acc > bestAcc + 1e-6 ||
                  (pl.acc > bestAcc - 1e-6 &&
                   (!d || pl.critical > d->critical || (pl.critical == d->critical && pl.score > d->score)));
    if (!d && !better) {
        LOG("details: %s -> %.4f%%, below the save's best %.4f%%: not kept", key, pl.acc, saveAcc);
        return true;
    }
    int plays = (d ? d->plays : 0) + 1;
    if (!d) d = detFind(key, true);
    if (!d) return true;
    if (better) {
        snprintf(pl.key, sizeof pl.key, "%s", key);
        *d = pl;
    }
    d->plays = plays;
    g_detVersion++;
    bool saved = detSave();
    LOG("details: %s -> %07d %.4f%% P%d G%d B%d M%d crit %d, %s, play %d%s", key, pl.score, pl.acc, pl.perfect,
        pl.good, pl.bad, pl.miss, pl.critical, better ? "new best kept" : "best unchanged", plays,
        saved ? "" : " (NOT saved)");
    return true;
}

static void resultTick() {
    if (!g_resPending) return;
    Obj* loc = (Obj*)g_resLoc.get();
    if (!loc || !alive(loc)) {
        g_resPending = false;
        g_resLoc.clear();
        return;
    }
    if (takeResult(loc, timeSinceLevelLoad() - g_resT0 > 1.5f)) {
        g_resPending = false;
        g_resLoc.clear();
    }
}

static void detailsFrame(void*);

bool detailsResultPending() { return g_resPending; }

void detailsResultStart(Obj* loc) {
    g_resLoc.clear();
    g_resPending = false;
    // autoplay / practice runs are never results of yours; the game's own challenge uses other windows
    if (!D.ok || autoplayLevelActive() || g_cfg.practice || gameChallengeLevel()) return;
    g_resLoc.set(loc);
    g_resPending = true;
    g_resCourse = courseLevelActive();
    g_resT0 = timeSinceLevelLoad();
    onEveryFrame(detailsFrame);
}

// ======================= song select =======================
struct Sel {
    Obj *item, *song, *rec;
    int level;
};
static Handle g_ss, g_line, g_accStr, g_lineStr;
static bool g_ssBuilt = false, g_mismatchLogged = false;
static int g_accBits = 0;
// the selection the cached detail belongs to
static Obj *g_selItem = nullptr, *g_selRec = nullptr;
static int g_selLevel = -1, g_selVersion = -1, g_selCourse = -1, g_selDet = -1;
static int g_lineKey[6];

static bool currentSel(Obj* ss, Sel* s) {
    void** items;
    int n = seq(callm<Obj*>(D.ssItems, ss), &items);
    int i = fld<int>(ss, D.ssNowSong);
    if (i < 0 || i >= n || !items[i]) return false;
    s->item = (Obj*)items[i];
    s->song = fld<Obj*>(s->item, D.itemSong);
    s->level = fld<int>(ss, D.ssNowLevel);
    void** recs;
    int nr = seq(fld<Obj*>(s->item, D.itemRecords), &recs);
    s->rec = s->level >= 0 && s->level < nr ? (Obj*)recs[s->level] : nullptr;
    return s->song != nullptr;
}

static int levelName(Obj* song, int l, char* out, int cap) {
    out[0] = 0;
    void** lv;
    int n = song ? seq(fld<Obj*>(song, D.songLevels), &lv) : 0;
    return l >= 0 && l < n && lv[l] ? utf8((Il2CppString*)lv[l], out, cap) : 0;
}

// our detail for the selection (index into g_det, -1 none), looked up again only when something changed
static int selDetail(const Sel& s) {
    int course = g_cfg.course ? 1 : 0;
    if (s.item == g_selItem && s.level == g_selLevel && s.rec == g_selRec && g_selVersion == g_detVersion &&
        g_selCourse == course)
        return g_selDet;
    g_selItem = s.item;
    g_selLevel = s.level;
    g_selRec = s.rec;
    g_selVersion = g_detVersion;
    g_selCourse = course;
    g_selDet = -1;
    char id[160], lv[16], key[224];
    if (utf8(fld<Il2CppString*>(s.song, D.songId), id, sizeof id) <= 0 || levelName(s.song, s.level, lv, sizeof lv) <= 0)
        return -1;
    makeKey(key, sizeof key, id, lv, course);
    Detail* d = detFind(key, false);
    g_selDet = d ? (int)(d - g_det) : -1;
    return g_selDet;
}


static bool parsePct(const char* s, double* v) {
    double x = 0, f = 0;
    int digits = 0;
    for (; *s >= '0' && *s <= '9'; s++, digits++) x = x * 10 + (*s - '0');
    if (*s == '.') {
        s++;
        f = 0.1;
        for (; *s >= '0' && *s <= '9'; s++, digits++) {
            x += (*s - '0') * f;
            f *= 0.1;
        }
    }
    *v = x;
    return digits > 0 && s[0] == '%' && s[1] == 0;
}

static void openCard(void*);

// The game's score / acc pair (SongSelector.score / .acc) stays where it is but invisible: the game keeps writing
// and animating it, and we read it every frame. On our own overlay canvas instead:
//   - the score (the game's string) centred where the pair was, with the judgement counts under it
//   - the accuracy (4 decimals) in big digits, the game's font at the score's size, right above the current
//     song's cover, with a 详情 button beside it (tapping the score opens the card too)
// Everything is placed from screen pixels (screenBounds), whatever kind of canvas the game draws on.
static Handle g_ui, g_uiGroup, g_score, g_scoreHit, g_acc, g_accBg, g_btn, g_btnFill, g_btnLabel;
static Vec2 g_accPos{0, 0};
static bool g_accPlaced = false, g_layoutLogged = false, g_gameHidden = false;
static int g_uiSize = 0, g_failLogged = 0;
static Il2CppString *g_scoreSeen = nullptr, *g_accSeen = nullptr;

static const int C_BTN_IVORY = 0xFAF9F5, C_BTN_LINE = 0xE8E6DC, C_BTN_TEXT = 0x141413;

// Our things live in a container under the score's own canvas (drawn in the game's order, faded with it), laid
// out from screen pixels. The game's score / acc are only hidden once our copy has been placed: if anything
// below fails, the original texts stay as they were.
static void ssBuild(Obj* scoreT, Obj* accT) {
    g_ssBuilt = true;
    if (D.txtOverflow) callm<void>(D.txtOverflow, accT, 1);
    Obj* host = canvasTransform(rt(scoreT));
    if (!alive(host)) {
        LOG("details: song select: the score has no canvas, overlay not built");
        return;
    }
    Obj* root = newUI("PhiMeSongInfo", host);
    stretch(root, 0);
    ignoreLayout(gameObject(root));
    setLastSibling(root);
    g_ui.set(root);
    g_uiGroup.set(addCanvasGroup(gameObject(root)));
    Obj* font = textFont(scoreT);
    int fs = textFontSize(scoreT) > 0 ? textFontSize(scoreT) : 40;
    // score + its judgement line
    Obj* sc = text(root, "", fs, rgb(0xFFFFFF), MiddleCenter, false, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, 0},
                   Vec2{700, fs * 1.4f});
    setTextFont(sc, font);
    g_score.set(sc);
    Obj* hit = image(rt(sc), "PhiMeScoreHit", SPR_SOLID, Color{1, 1, 1, 0}, Vec2{0.5f, 0.5f}, Vec2{0, 0},
                     Vec2{300, 80}, 0, true);
    onClick(gameObject(hit), nullptr, openCard, nullptr);
    g_scoreHit.set(hit);
    Obj* line = text(rt(sc), "", 24, rgb(0xFFFFFF), UpperCenter, false, Vec2{0.5f, 0}, Vec2{0.5f, 1}, Vec2{0, -2},
                     Vec2{700, 40});
    setTextFont(line, font);
    g_line.set(line);
    g_lineKey[5] = -3;  // never a detail index: the line is filled on the first frame
    // big accuracy (Source Serif 4, drawn by the mod) + 详情, on a dark rounded backdrop that keeps it readable
    // over bright covers
    g_accBg.set(image(root, "PhiMeAccBg", SPR_ROUND, Color{0.04f, 0.04f, 0.06f, 0.55f}, Vec2{0.5f, 0.5f}, Vec2{0, 0},
                      Vec2{300, fs * 1.5f}, fs * 0.6f));
    Obj* acc = text(root, "", fs, rgb(0xFFFFFF), MiddleCenter, false, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, 0},
                    Vec2{700, fs * 1.4f});
    g_acc.set(acc);
    Obj* b = image(root, "PhiMeDetailsBtn", SPR_ROUND, rgb(C_BTN_LINE), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{120, 50},
                   25, true);
    Obj* bf = image(rt(b), "fill", SPR_ROUND, rgb(C_BTN_IVORY), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{116, 46}, 23);
    Obj* bl = text(rt(b), T("详情", "Details"), 24, rgb(C_BTN_TEXT), MiddleCenter, false, Vec2{0.5f, 0.5f},
                   Vec2{0.5f, 0.5f}, Vec2{0, 1}, Vec2{200, 50});
    onClick(gameObject(b), b, openCard, nullptr);
    g_btn.set(b);
    g_btnFill.set(bf);
    g_btnLabel.set(bl);
    setGroupAlpha((Obj*)g_uiGroup.get(), 0);  // nothing shows until the first layout worked
    g_uiSize = 0;
    g_accPlaced = g_layoutLogged = g_gameHidden = false;
    g_failLogged = 0;
    g_scoreSeen = g_accSeen = nullptr;
    LOG("details: song select overlay built (game score %d px)", fs);
}

void detailsRelabel() {  // the language changed while song select is open
    Obj* bl = (Obj*)g_btnLabel.get();
    if (alive(bl)) retext(bl, T("详情", "Details"), 24, false);
    g_uiSize = 0;  // re-measure the pill (sets the label size again)
}

static void updateLine(const Sel* s) {
    Obj* line = (Obj*)g_line.get();
    if (!alive(line)) return;
    int di = s && s->rec ? selDetail(*s) : -1;
    const Detail* d = di >= 0 && g_det[di].score == recScore(s->rec) ? &g_det[di] : nullptr;
    int key[6] = {d ? d->perfect : -1, d ? d->good : -1, d ? d->bad + d->miss : -1, d ? d->critical : -1, 0, di};
    if (!memcmp(key, g_lineKey, sizeof key) && g_lineStr.h) return;
    memcpy(g_lineKey, key, sizeof key);
    char buf[256] = "";
    // same colours as the in-level judgement line: Perfect yellow (Critical gold / Perfect orange), Good blue,
    // Bad + Miss grey (the container's alpha fades it with the game's text)
    if (d && d->critical >= 0)
        snprintf(buf, sizeof buf,
                 "<color=#FFF08A>%d</color>  <color=#FFCB7A>%d</color>  <color=#B4E1FF>%d</color>  "
                 "<color=#A0A0A0>%d</color>",
                 d->critical, d->perfect - d->critical, d->good, d->bad + d->miss);
    else if (d)
        snprintf(buf, sizeof buf, "<color=#FFECA0>%d</color>  <color=#B4E1FF>%d</color>  <color=#A0A0A0>%d</color>",
                 d->perfect, d->good, d->bad + d->miss);
    g_lineStr.set(str(buf));
    setTextStr(line, (Il2CppString*)g_lineStr.get());
}

static float accBgH(int isz) { return isz * 1.5f; }  // the ACC capsule's height

static void layoutFail(int bit, const char* why) {
    if (g_failLogged & bit) return;
    g_failLogged |= bit;
    LOG("details: song select layout: %s", why);
}

// screen x of the level bar's right end (*left, bit 1) and of the shown rank badge's left edge (*right, bit 2)
static int scoreGap(Obj* ss, float* left, float* right) {
    int got = 0;
    float x0, x1, y0, y1;
    Obj* lb = D.ssLevelBar >= 0 ? fld<Obj*>(ss, D.ssLevelBar) : nullptr;
    if (alive(lb) && D.lbInfos >= 0 && D.liMain >= 0) {
        void** infos;
        int n = seq(fld<Obj*>(lb, D.lbInfos), &infos);
        for (int i = 0; i < n; i++) {
            Obj* m = infos[i] ? fld<Obj*>((Obj*)infos[i], D.liMain) : nullptr;
            if (!alive(m) || !activeInHierarchy(gameObject(m)) || !screenBounds(m, &x0, &x1, &y0, &y1)) continue;
            if (!(got & 1) || x1 > *left) *left = x1;
            got |= 1;
        }
    }
    Obj* rd = D.ssRating >= 0 ? fld<Obj*>(ss, D.ssRating) : nullptr;
    if (alive(rd) && D.rdObjects >= 0) {
        void** objs;
        int n = seq(fld<Obj*>(rd, D.rdObjects), &objs);
        for (int i = 0; i < n; i++) {
            Obj* go = (Obj*)objs[i];
            if (!alive(go) || !activeInHierarchy(go) || !screenBounds(goTransform(go), &x0, &x1, &y0, &y1)) continue;
            if (!(got & 2) || x0 < *right) *right = x0;
            got |= 2;
        }
    }
    if ((got & 3) == 3 && *right <= *left) got &= 1;
    return got;
}

// world x of the bar under the cover, from the level tabs' left edge to the dark score bar's right end (practice.cpp
// centres the dock under it). The rank badges' rects reach well past what they show (0.15.1 put the dock ~70 px
// right of centre on the iPad), but their left edge is right (the score is centred with it): the bar ends about
// 0.16 of the tabs-to-badge distance after the badge's left edge.
// A locked song shows no rank badge (an Unlock button instead), so the bar's right end is estimated from the tabs'
// height (the bar is about kBarRatio tab heights long; the ratio seen on badge songs is remembered), and with no tabs
// either, the last span measured on this screen size is used. *exact tells which (the dock retries until exact).
static const float kBarRatio = 8.65f;  // iPad 0.23.1: 7.382 wide / 0.853 tall
static float g_barRatio = 0;
static float g_lastSpan[3];
static bool g_haveLast = false;
static int lastSpanScreen() {
    Vec2 s = screenSize();
    return (int)s.x * 10000 + (int)s.y;
}
static void loadBarMemory() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    int r = prefGetInt("PhiMeMod.barRatio", 0);
    if (r > 3000 && r < 20000) g_barRatio = r / 1000.f;
    if (prefGetInt("PhiMeMod.barScreen", 0) == lastSpanScreen() && prefGetInt("PhiMeMod.barX1", 0) != 0) {
        g_lastSpan[0] = prefGetInt("PhiMeMod.barX0", 0) / 1000.f;
        g_lastSpan[1] = prefGetInt("PhiMeMod.barX1", 0) / 1000.f;
        g_lastSpan[2] = prefGetInt("PhiMeMod.barY", 0) / 1000.f;
        g_haveLast = g_lastSpan[1] > g_lastSpan[0];
    }
}
static void rememberBar(float x0, float x1, float y, float tabH) {
    float r = tabH > 1e-4f ? (x1 - x0) / tabH : 0;
    if (r > 3 && r < 20 && (g_barRatio - r) * (g_barRatio - r) > 1e-4f) {
        g_barRatio = r;
        prefSetIntRaw("PhiMeMod.barRatio", (int)(r * 1000 + 0.5f));
    }
    float now[3] = {x0, x1, y};
    bool same = g_haveLast;
    for (int i = 0; i < 3 && same; i++) same = (now[i] - g_lastSpan[i]) * (now[i] - g_lastSpan[i]) < 1e-6f;
    if (same) return;
    memcpy(g_lastSpan, now, sizeof now);
    g_haveLast = true;
    prefSetIntRaw("PhiMeMod.barScreen", lastSpanScreen());
    prefSetIntRaw("PhiMeMod.barX0", (int)(x0 * 1000));
    prefSetIntRaw("PhiMeMod.barX1", (int)(x1 * 1000));
    prefSetIntRaw("PhiMeMod.barY", (int)(y * 1000));
}

bool songBarSpan(Obj* ss, float* x0, float* x1, float* yMid, bool* exact) {
    if (exact) *exact = false;
    if (!alive(ss)) return false;
    loadBarMemory();
    int got = 0, act = 0;
    float a0, a1, b0, b1, badge = 0, badgeAll = 0, badgeR = 0, ty0 = 0, ty1 = 0;
    Obj* lb = D.ssLevelBar >= 0 ? fld<Obj*>(ss, D.ssLevelBar) : nullptr;
    if (alive(lb) && D.lbInfos >= 0 && D.liMain >= 0) {
        void** infos;
        int n = seq(fld<Obj*>(lb, D.lbInfos), &infos);
        for (int i = 0; i < n; i++) {
            Obj* m = infos[i] ? fld<Obj*>((Obj*)infos[i], D.liMain) : nullptr;
            if (!alive(m) || !activeInHierarchy(gameObject(m)) || !worldBounds(m, &a0, &a1, &b0, &b1)) continue;
            if (!(got & 1) || a0 < *x0) *x0 = a0;
            if (!(got & 1) || b0 < ty0) ty0 = b0;
            if (!(got & 1) || b1 > ty1) ty1 = b1;
            got |= 1;
        }
    }
    Obj* rd = D.ssRating >= 0 ? fld<Obj*>(ss, D.ssRating) : nullptr;
    if (alive(rd) && D.rdObjects >= 0) {
        void** objs;
        int n = seq(fld<Obj*>(rd, D.rdObjects), &objs);
        for (int i = 0; i < n; i++) {
            Obj* go = (Obj*)objs[i];
            if (!alive(go) || !worldBounds(goTransform(go), &a0, &a1, &b0, &b1)) continue;
            if (!(got & 2) || a0 < badgeAll) badgeAll = a0;
            if (!(got & 2) || a1 > badgeR) badgeR = a1;
            got |= 2;
            if (activeInHierarchy(go) && (!act || a0 < badge)) badge = a0;
            if (activeInHierarchy(go)) act++;
        }
    }
    static int logged = 0, missLogged = 0;
    if (got != 3) {
        const char* how = "none";
        if ((got & 1) && ty1 - ty0 > 1e-4f) {  // tabs but no badge (a locked song)
            *x1 = *x0 + (ty1 - ty0) * (g_barRatio > 0 ? g_barRatio : kBarRatio);
            if (yMid) *yMid = (ty0 + ty1) / 2;
            how = g_barRatio > 0 ? "tabs x learned ratio" : "tabs x default ratio";
        } else if (g_haveLast) {
            *x0 = g_lastSpan[0];
            *x1 = g_lastSpan[1];
            if (yMid) *yMid = g_lastSpan[2];
            how = "last span";
        }
        if (missLogged++ < 3)
            LOG("details: bar span: %s%s missing -> %s%s", got & 1 ? "" : "tabs ", got & 2 ? "" : "badges", how,
                strcmp(how, "none") ? "" : ", no bar");
        return strcmp(how, "none") && *x1 > *x0;
    }
    if (!act) badge = badgeAll;
    *x1 = badge + (badge - *x0) * 0.16f;
    if (yMid) *yMid = (ty0 + ty1) / 2;
    if (logged++ < 3)
        LOG("details: bar span (world) tabs from %.3f, badge left %.3f (%d shown, all %.3f..%.3f) -> %.3f..%.3f, tabs y "
            "%.3f..%.3f",
            *x0, badge, act, badgeAll, badgeR, *x0, *x1, ty0, ty1);
    if (*x1 <= *x0) return false;
    rememberBar(*x0, *x1, (ty0 + ty1) / 2, ty1 - ty0);
    if (exact) *exact = true;
    return true;
}

static void layout(Obj* ss, Obj* scoreT, Obj* accT, const Sel* s) {
    Obj* root = (Obj*)g_ui.get();
    Obj* group = (Obj*)g_uiGroup.get();
    Obj *sc = (Obj*)g_score.get(), *acc = (Obj*)g_acc.get(), *btn = (Obj*)g_btn.get();
    if (!alive(root) || !alive(sc) || !alive(acc) || !alive(btn)) return;
    // where the game's pair is, in screen pixels
    float sx0, sx1, sy0, sy1, ax0, ax1, ay0, ay1;
    if (!screenBounds(rt(scoreT), &sx0, &sx1, &sy0, &sy1)) {
        layoutFail(1, "no screen rect for the score");
        if (alive(group)) setGroupAlpha(group, 0);
        return;
    }
    bool haveAcc = screenBounds(rt(accT), &ax0, &ax1, &ay0, &ay1);
    float ux0 = sx0, ux1 = sx1;
    if (haveAcc) {
        if (ax0 < ux0) ux0 = ax0;
        if (ax1 > ux1) ux1 = ax1;
    }
    // centred in the free part of the dark bar: from the level bar's last tab to the rank badge (B, A, ...)
    float gl, gr;
    int free = scoreGap(ss, &gl, &gr);
    float cx = (ux0 + ux1) / 2;
    if (free == 3) cx = (gl + gr) / 2;
    else if (free == 1 && ux1 > gl) cx = (gl + ux1) / 2;
    Vec2 cPx{cx, (sy0 + sy1) / 2}, c, c2;
    if (!screenToLocal(rt(root), cPx, &c) || !screenToLocal(rt(root), Vec2{cPx.x, cPx.y + 100}, &c2)) {
        layoutFail(2, "screen -> canvas conversion failed");
        if (alive(group)) setGroupAlpha(group, 0);
        return;
    }
    float k = (c2.y - c.y) / 100;  // our units per screen pixel
    if (!(k > 0.0001f)) {
        layoutFail(4, "canvas scale is zero");
        return;
    }
    // the game's font size on screen, in our units
    Rect rc{0, 0, 1, 1};
    int gfs = textFontSize(scoreT);
    float size = (float)gfs;
    if (rectOf(rt(scoreT), &rc) && rc.h > 1 && gfs > 0) size = gfs * (sy1 - sy0) / rc.h * k;
    int isz = (int)(size + 0.5f);
    if (isz < 12) isz = 12;
    if (isz != g_uiSize) {
        g_uiSize = isz;
        setTextSize(sc, isz);
        setSize(rt(sc), Vec2{isz * 9.f, isz * 1.4f});
        setTextSize(acc, isz);
        setSize(rt(acc), Vec2{isz * 9.f, isz * 1.4f});
        Obj* hit = (Obj*)g_scoreHit.get();
        if (alive(hit)) setSize(rt(hit), Vec2{isz * 0.62f * 7 + 24, isz * 1.4f + isz * 0.9f});
        Obj* line = (Obj*)g_line.get();
        int ls = (int)(isz * 0.55f + 0.5f);
        if (ls < 10) ls = 10;
        if (alive(line)) {
            setTextSize(line, ls);
            setSize(rt(line), Vec2{isz * 12.f, ls * 1.5f});
        }
        // the backdrop is a full capsule; 详情 is a pill inside its right end with the same rounding, inset by the
        // same margin all round (concentric arcs)
        float bgH = accBgH(isz), ins = bgH * 0.13f;
        setRadius((Obj*)g_accBg.get(), bgH * 0.48f);
        Obj *bf = (Obj*)g_btnFill.get(), *bl = (Obj*)g_btnLabel.get();
        float bh = bgH - 2 * ins;
        int lsz = (int)(bh * 0.42f + 0.5f);
        float bw = textWidth(T("详情", "Details"), lsz, false) + bh * 1.0f;
        setSize(rt(btn), Vec2{bw, bh});
        setRadius(btn, bh * 0.48f);
        if (alive(bf)) {
            setSize(rt(bf), Vec2{bw - 4, bh - 4});
            setRadius(bf, (bh - 4) * 0.48f);
        }
        if (alive(bl)) {
            setTextSize(bl, lsz);
            setSize(rt(bl), Vec2{bw, bh});
        }
    }
    setPos(rt(sc), c);
    Il2CppString* st = textStr(scoreT);
    if (st && st != g_scoreSeen) {
        g_scoreSeen = st;
        setTextStr(sc, st);
    }
    Color gc = getColor(scoreT);
    setColor(sc, Color{gc.r, gc.g, gc.b, 1});
    Il2CppString* at = textStr(accT);
    if (at && at != g_accSeen) {
        g_accSeen = at;
        setTextStr(acc, at);
    }

    // the accuracy: above the current cover (in pixels), the digits + 详情 centred on it, kept under the top bar
    float lineHpx = isz * 1.2f / k, gapPx = isz * 0.35f / k;
    Vec2 tPx{cPx.x, sy1 + lineHpx};  // no cover: right above the score
    Obj* cover = s && s->item && D.itemCover >= 0 ? fld<Obj*>(s->item, D.itemCover) : nullptr;
    float cx0 = 0, cx1 = 0, cy0 = 0, cy1 = 0, tx0, tx1, ty0 = 0, ty1;
    bool haveCover = alive(cover) && screenBounds(cover, &cx0, &cx1, &cy0, &cy1);
    // the cover is a parallelogram slanted like the rest of the game's UI (about 1 across per 4 up), so its top
    // edge is shifted right of the rect's middle by half the slant: centre over that edge
    if (haveCover) tPx = Vec2{(cx0 + cx1) / 2 + (cy1 - cy0) * 0.125f, cy1 + gapPx + lineHpx / 2};
    else layoutFail(8, "no cover rect, the accuracy goes above the score");
    Obj* topBar = D.ssTopBar >= 0 ? fld<Obj*>(ss, D.ssTopBar) : nullptr;
    bool haveTop = alive(topBar) && screenBounds(topBar, &tx0, &tx1, &ty0, &ty1);
    Rect br{0, 0, 120, 50};
    rectOf(rt(btn), &br);
    float accW = textPreferredWidth(acc), gap = isz * 0.4f;
    float bgH = accBgH(isz), ins = bgH * 0.13f, padL = bgH * 0.45f;
    float capW = padL + accW + gap + br.w + ins;  // [pad][digits][gap][详情][inset]
    // under the top bar only where they overlap sideways, and never down into the cover (on a phone the cover
    // reaches almost to the top bar: the capsule was half hidden behind it)
    float halfWpx = capW / k / 2;
    if (haveTop && haveCover && ty0 > cy1 && tPx.y + lineHpx / 2 > ty0 - 4 && tPx.x - halfWpx < tx1 &&
        tPx.x + halfWpx > tx0)
        tPx.y = ty0 - 4 - lineHpx / 2;
    if (haveCover && tPx.y < cy1 + gapPx * 0.5f + bgH / k / 2) tPx.y = cy1 + gapPx * 0.5f + bgH / k / 2;
    Vec2 target;
    if (!screenToLocal(rt(root), tPx, &target)) target = Vec2{c.x, c.y + isz * 1.6f};
    if (!g_accPlaced) {
        g_accPos = target;
        g_accPlaced = true;
    } else {  // follows the cover as it slides in, without jumps when the song changes
        float kk = unscaledDt() * 14;
        if (kk > 1) kk = 1;
        g_accPos.x += (target.x - g_accPos.x) * kk;
        g_accPos.y += (target.y - g_accPos.y) * kk;
    }
    float left = g_accPos.x - capW / 2;
    setPos(rt(acc), Vec2{left + padL + accW / 2, g_accPos.y});
    setPos(rt(btn), Vec2{left + capW - ins - br.w / 2, g_accPos.y});
    Obj* bg = (Obj*)g_accBg.get();
    if (alive(bg)) {  // the backdrop: around the digits and the pill, a capsule
        setSize(rt(bg), Vec2{capW, bgH});
        setPos(rt(bg), g_accPos);
    }

    // placed: now the game's own pair can go
    if (!g_gameHidden) {
        g_gameHidden = true;
        hideUi(gameObject(scoreT));
        hideUi(gameObject(accT));
    }
    // visible with the game's score text (its colour fades with the screen), hidden while leaving
    float a = activeInHierarchy(gameObject(scoreT)) ? gc.a : 0;
    if (D.ssExit >= 0 && fld<bool>(ss, D.ssExit)) a = 0;
    Obj* startAnim = D.ssStartAnim >= 0 ? fld<Obj*>(ss, D.ssStartAnim) : nullptr;
    if (alive(startAnim) && activeInHierarchy(startAnim)) a = 0;
    if (alive(group)) {
        setGroupAlpha(group, a);
        setBlocksRaycasts(group, a > 0.5f);
    }
    if (!g_layoutLogged && a > 0.5f) {
        g_layoutLogged = true;
        LOG("details: song select layout (px): score+acc %.0f..%.0f x %.0f..%.0f, cover %s %.0f..%.0f x %.0f..%.0f, "
            "top bar %s bottom %.0f; %.3f units/px, text %d; score at %.0f,%.0f, acc at %.0f,%.0f (width %.0f)",
            ux0, ux1, sy0, sy1, haveCover ? "yes" : "NO", cx0, cx1, cy0, cy1, haveTop ? "yes" : "no", ty0, k, isz,
            c.x, c.y, target.x, target.y, accW);
    }
}

static void songSelectTick() {
    if (!g_ss.h) return;
    Obj* ss = (Obj*)g_ss.get();
    if (!alive(ss)) {
        g_ss.clear();
        g_line.clear();
        return;
    }
    Obj* accT = fld<Obj*>(ss, D.ssAcc);
    Obj* scoreT = fld<Obj*>(ss, D.ssScore);
    if (!alive(accT) || !alive(scoreT)) return;
    if (!g_ssBuilt) ssBuild(scoreT, accT);
    Sel s;
    bool have = currentSel(ss, &s);
    if (have && s.rec) {
        float a = recAcc(s.rec);
        int bits;
        memcpy(&bits, &a, 4);
        if (!g_accStr.h || bits != g_accBits) {
            char b[24];
            fmtAcc(b, sizeof b, a);
            g_accStr.set(str(b));
            g_accBits = bits;
        }
        Il2CppString* cur = textStr(accT);
        if (cur && cur != g_accStr.get()) {
            char b[40];
            utf8(cur, b, sizeof b);
            double v;
            if (parsePct(b, &v) && __builtin_fabs(v - a) <= 0.0051) {
                setTextStr(accT, (Il2CppString*)g_accStr.get());
                PM_ONCE("details: song select accuracy shown with 4 decimals");
            } else if (!g_mismatchLogged) {
                g_mismatchLogged = true;  // the game shows something else (another record / a label): leave it
                LOG("details: acc text '%s' is not the record's %.4f, left alone", b, a);
            }
        }
    }
    updateLine(have ? &s : nullptr);
    layout(ss, scoreT, accT, have ? &s : nullptr);
}

void detailsSongSelect(Obj* ss) {
    if (!D.ssOk) return;
    configLoad();
    detLoad();
    Obj* old = (Obj*)g_ui.get();
    if (alive(old) && ss != g_ss.get()) destroy(gameObject(old));
    if (ss == g_ss.get() && g_ssBuilt && alive(old)) return;  // the same screen again (course toggle)
    g_ss.set(ss);
    g_ui.clear();
    g_line.clear();
    g_accStr.clear();
    g_lineStr.clear();
    g_ssBuilt = false;
    g_mismatchLogged = false;
    g_selItem = g_selRec = nullptr;
    g_selLevel = g_selVersion = g_selCourse = g_selDet = -1;
    onEveryFrame(detailsFrame);
}

// ======================= details card =======================
// palette (Claude desktop light theme, like the settings panel)
static const int C_CLAY = 0xD97757, C_IVORY = 0xFAF9F5, C_LINE = 0xE8E6DC, C_TEXT = 0x141413, C_MUTED = 0x87867F,
                 C_CHIP = 0xF0EEE6;
// judgement colours, deep enough for the ivory card
static const int J_CRIT = 0xD9A21F, J_PERF = 0xE7894A, J_GOOD = 0x4A8BD6, J_BAD = 0xCC5E52, J_MISS = 0x9C9A92;
static const float kW = 980, kH = 760, kPad = 44, kTop = kH / 2;
static const Vec2 kClawdPos{kW / 2 - 120, kTop + 2 + 34};

struct CardLevel {
    char name[16];
    float constant;
    bool played;
    int s;
    float a;
    bool c;
    bool hasDet;
    Detail det;
};
static struct {
    char title[160], composer[160];
    bool course;
    int n, sel;
    CardLevel lv[6];
} g_cd;
static Handle g_cRoot, g_cCard, g_cDim, g_cClawd, g_cPage, g_cPill[6], g_cLabel[6];
static float g_cOpenT = -1, g_cClawdT = 0;
static Handle g_cThumb;  // the level tabs' sliding capsule
static Handle g_cPageGroup;
static float g_cPageT = -1;
static Vec2 g_cAt[6];

static void closeCard(void*) {
    Obj* root = (Obj*)g_cRoot.get();
    uiFadeAway(root, (Obj*)g_cCard.get());  // fades out (the card shrinking a little), then destroyed
    g_cThumb.clear();
    g_cRoot.clear();
    g_cCard.clear();
    g_cDim.clear();
    g_cClawd.clear();
    g_cPage.clear();
    for (int i = 0; i < 6; i++) {
        g_cPill[i].clear();
        g_cLabel[i].clear();
    }
    g_cOpenT = -1;
}

bool detailsCardOpen() { return g_cRoot.h && alive((Obj*)g_cRoot.get()); }

// cut a UTF-8 string to at most `maxBytes`, on a character boundary, with an ellipsis
static void clip(char* s, int maxBytes) {
    int n = (int)strlen(s);
    if (n <= maxBytes) return;
    int k = maxBytes;
    while (k > 0 && ((unsigned char)s[k] & 0xC0) == 0x80) k--;
    snprintf(s + k, 4, "…");
}

static Obj* label(Obj* parent, const char* s, int size, int color, int align, bool bold, Vec2 anchor, Vec2 pos,
                  Vec2 box, float alpha = 1) {
    Vec2 pivot{align % 3 * 0.5f, 0.5f};
    return text(parent, s, size, rgb(color, alpha), align, bold, anchor, pivot, pos, box);
}

static void chip(Obj* page, float x, float y, float w, float h, const char* name, const char* value, int valueColor) {
    Obj* c = image(page, "chip", SPR_ROUND, rgb(C_CHIP), Vec2{0, 0.5f}, Vec2{x + w / 2, y}, Vec2{w, h}, 16);
    Obj* crt = rt(c);
    label(crt, name, 18, C_MUTED, MiddleLeft, false, Vec2{0, 0.5f}, Vec2{18, h / 2 - 22}, Vec2{w - 30, 28});
    label(crt, value, 30, valueColor, MiddleLeft, true, Vec2{0, 0.5f}, Vec2{18, -12}, Vec2{w - 30, 40});
}

static void judgeCell(Obj* page, float x, float y, float w, float h, const char* name, int color, int value,
                      bool known) {
    Obj* c = image(page, "cell", SPR_ROUND, rgb(C_CHIP), Vec2{0, 0.5f}, Vec2{x + w / 2, y}, Vec2{w, h}, 16);
    Obj* crt = rt(c);
    char b[24];
    if (known) snprintf(b, sizeof b, "%d", value);
    else snprintf(b, sizeof b, "—");
    label(crt, b, 34, known ? C_TEXT : C_MUTED, MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0, 12}, Vec2{w - 10, 44},
          known ? 1.f : 0.6f);
    // colour dot + name under the number, centred together
    float nameW = (float)strlen(name) * 9.5f;
    float total = 12 + 8 + nameW;
    image(crt, "dot", SPR_CIRCLE, rgb(color), Vec2{0.5f, 0.5f}, Vec2{-total / 2 + 6, -26}, Vec2{12, 12});
    label(crt, name, 18, C_MUTED, MiddleLeft, false, Vec2{0.5f, 0.5f}, Vec2{-total / 2 + 20, -26}, Vec2{nameW + 30, 26});
}

static void paintTabs() {
    for (int i = 0; i < g_cd.n; i++) {
        Obj* l = (Obj*)g_cLabel[i].get();
        bool on = i == g_cd.sel;
        if (alive(l)) setColor(l, rgb(on ? C_TEXT : C_MUTED));
    }
    Obj* th = (Obj*)g_cThumb.get();
    if (alive(th) && g_cd.sel >= 0 && g_cd.sel < g_cd.n) uiGlide(rt(th), g_cAt[g_cd.sel]);
}

static void buildCardPage() {
    Obj* card = (Obj*)g_cCard.get();
    if (!alive(card) || g_cd.n <= 0) return;
    Obj* old = (Obj*)g_cPage.get();
    if (alive(old)) destroy(gameObject(old));
    Obj* page = newUI("page", card);
    stretch(page, 0);
    g_cPage.set(page);
    Obj* pg = addCanvasGroup(gameObject(page));
    setGroupAlpha(pg, 0);
    g_cPageGroup.set(pg);
    g_cPageT = 0;
    const CardLevel& L = g_cd.lv[g_cd.sel];
    const Detail* d = L.hasDet ? &L.det : nullptr;
    bool match = d && L.played && __builtin_fabs(d->acc - L.a) <= 0.00005;  // the kept play is the best-accuracy one
    char b[200];

    // ---- best score + badge ----
    float y = kTop - 200;
    label(page, g_cd.course ? T("课题模式最高分", "Course best") : T("最高分", "Best score"), 19, C_MUTED, MiddleLeft, false, Vec2{0, 0.5f}, Vec2{kPad, y},
          Vec2{300, 28});
    y = kTop - 250;
    if (L.played) {
        snprintf(b, sizeof b, "%07d", L.s);
        label(page, b, 64, C_TEXT, MiddleLeft, true, Vec2{0, 0.5f}, Vec2{kPad - 2, y}, Vec2{360, 80});
        // AP+ (every note a Critical, from our record) / AP / FC
        const char* badge = nullptr;
        int bg = 0, fg = 0;
        if (L.s >= 1000000 && match && d->critical >= 0 && d->critical == d->perfect && d->good + d->bad + d->miss == 0)
            badge = "AP+", bg = 0xF7E2A0, fg = 0x7A5800;
        else if (L.s >= 1000000) badge = "AP", bg = 0xF6E7B8, fg = 0x8C6A10;
        else if (L.c) badge = "FC", bg = 0xDDEBFA, fg = 0x2E6AAE;
        if (badge) {
            float bw = badge[2] ? 78 : 62;
            Obj* p = image(page, "badge", SPR_ROUND, rgb(bg), Vec2{0, 0.5f}, Vec2{kPad + 300 + bw / 2, y - 2},
                           Vec2{bw, 38}, 19);
            label(rt(p), badge, 21, fg, MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0, 1}, Vec2{bw, 38});
        }
    } else {
        label(page, T("还没有成绩", "No score yet"), 36, C_MUTED, MiddleLeft, true, Vec2{0, 0.5f}, Vec2{kPad, y}, Vec2{400, 60});
    }

    // ---- accuracy / single-chart RKS / chart constant ----
    y = kTop - 350;
    float gap = 16, cw = (kW - 2 * kPad - 2 * gap) / 3, ch = 86;
    if (L.played) fmtAcc(b, sizeof b, L.a);
    else snprintf(b, sizeof b, "—");
    chip(page, kPad, y, cw, ch, T("准度（最高）", "Accuracy (best)"), b, L.played ? C_CLAY : C_MUTED);
    // course records have no RKS (the game never counts them)
    bool hasRks = L.played && !g_cd.course && L.constant > 0;
    if (hasRks) snprintf(b, sizeof b, "%.2f", rks(L.a, L.constant));
    else snprintf(b, sizeof b, "—");
    chip(page, kPad + cw + gap, y, cw, ch, T("单曲 RKS", "Chart RKS"), b, hasRks ? C_TEXT : C_MUTED);
    if (L.constant > 0) snprintf(b, sizeof b, "%.1f", L.constant);
    else snprintf(b, sizeof b, "—");
    chip(page, kPad + 2 * (cw + gap), y, cw, ch, T("定数", "Constant"), b, C_TEXT);

    image(page, "divider", SPR_SOLID, rgb(C_LINE), Vec2{0.5f, 0.5f}, Vec2{0, kTop - 426}, Vec2{kW - 2 * kPad, 2});

    // ---- judgements ----
    y = kTop - 460;
    label(page, T("判定详情", "Judgements"), 22, C_TEXT, MiddleLeft, true, Vec2{0, 0.5f}, Vec2{kPad, y}, Vec2{300, 34});
    const char* where = match ? T("PhiMe 记录 · 最高准度那一局", "PhiMe record · the best-accuracy play") : d ? T("PhiMe 记录 · 记下的最好一局", "PhiMe record · best recorded play") : L.played ? T("暂无 PhiMe 记录", "No PhiMe record") : "";
    label(page, where, 18, C_MUTED, MiddleRight, false, Vec2{1, 0.5f}, Vec2{-kPad, y}, Vec2{500, 28});
    y = kTop - 540;
    bool crit = d && d->critical >= 0;
    int n = crit ? 5 : 4;
    float cg = 12, w = (kW - 2 * kPad - (n - 1) * cg) / n, h = 104, x = kPad;
    if (crit) {
        judgeCell(page, x, y, w, h, T("大 P", "Critical"), J_CRIT, d->critical, true);
        x += w + cg;
        judgeCell(page, x, y, w, h, T("小 P", "Perfect"), J_PERF, d->perfect - d->critical, true);
    } else {
        judgeCell(page, x, y, w, h, "Perfect", J_CRIT, d ? d->perfect : 0, d != nullptr);
    }
    x += w + cg;
    judgeCell(page, x, y, w, h, "Good", J_GOOD, d ? d->good : 0, d != nullptr);
    x += w + cg;
    judgeCell(page, x, y, w, h, "Bad", J_BAD, d ? d->bad : 0, d != nullptr);
    x += w + cg;
    judgeCell(page, x, y, w, h, "Miss", J_MISS, d ? d->miss : 0, d != nullptr);

    // ---- the small print ----
    y = kTop - 622;
    if (d) {
        int notes = d->perfect + d->good + d->bad + d->miss;
        snprintf(b, sizeof b, T("Early %d  ·  Late %d  ·  最大连击 %d / %d  ·  游玩 %d 次", "Early %d  ·  Late %d  ·  Max combo %d / %d  ·  Played %d times"), d->early, d->late,
                 d->maxCombo, notes, d->plays);
    } else {
        snprintf(b, sizeof b, "%s", L.played ? T("这个难度还没在装好 PhiMe 后打过，打一次就会记下判定", "Not played since PhiMe was installed; play once to record judgements")
                                             : T("这个难度还没有游玩记录", "This chart has not been played yet"));
    }
    label(page, b, 19, C_MUTED, MiddleLeft, false, Vec2{0, 0.5f}, Vec2{kPad, y}, Vec2{kW - 2 * kPad, 30});
    y -= 34;
    if (d && !match && L.played) {
        char a1[24];
        fmtAcc(a1, sizeof a1, d->acc);
        snprintf(b, sizeof b, T("当前最高准度是在没有 PhiMe 时打的（装之前或官方版）；这里是 PhiMe 记下的最好一局（%s）", "The best accuracy was set without PhiMe (before it, or the official app); this is the best play PhiMe recorded (%s)"), a1);
        label(page, b, 18, C_CLAY, MiddleLeft, false, Vec2{0, 0.5f}, Vec2{kPad, y}, Vec2{kW - 2 * kPad, 28});
    } else if (match && d->score != L.s) {
        snprintf(b, sizeof b, T("这一局的分数是 %07d，最高分来自另一局", "This play scored %07d; the best score is from another play"), d->score);
        label(page, b, 18, C_MUTED, MiddleLeft, false, Vec2{0, 0.5f}, Vec2{kPad, y}, Vec2{kW - 2 * kPad, 28});
    }
}

static void onTab(void* user) {
    int i = (int)(intptr_t)user;
    if (i == g_cd.sel || i < 0 || i >= g_cd.n) return;
    g_cd.sel = i;
    paintTabs();
    buildCardPage();
}

// everything the card shows, read once when it opens
static bool gatherCard() {
    Obj* ss = (Obj*)g_ss.get();
    if (!alive(ss)) return false;
    Sel s;
    if (!currentSel(ss, &s)) return false;
    memset(&g_cd, 0, sizeof g_cd);
    g_cd.course = g_cfg.course;
    if (D.songName >= 0) utf8(fld<Il2CppString*>(s.song, D.songName), g_cd.title, sizeof g_cd.title);
    if (D.songComposer >= 0) utf8(fld<Il2CppString*>(s.song, D.songComposer), g_cd.composer, sizeof g_cd.composer);
    clip(g_cd.title, 66);
    clip(g_cd.composer, 90);
    char id[160];
    utf8(fld<Il2CppString*>(s.song, D.songId), id, sizeof id);
    if (!g_cd.title[0]) snprintf(g_cd.title, sizeof g_cd.title, "%s", id);
    void **lv, **recs;
    int nl = seq(fld<Obj*>(s.song, D.songLevels), &lv);
    int nr = seq(fld<Obj*>(s.item, D.itemRecords), &recs);
    // chart constants: float[] (or a list of floats)
    Obj* diffObj = D.songDiff >= 0 ? fld<Obj*>(s.song, D.songDiff) : nullptr;
    float* diffs = nullptr;
    int nd = 0;
    if (diffObj && strcmp(il2cpp_class_get_name(il2cpp_object_get_class(diffObj)), "Single[]") == 0) {
        diffs = (float*)arrayData((Il2CppArray*)diffObj);
        nd = (int)((Il2CppArray*)diffObj)->max_length;
    }
    for (int l = 0; l < nl && g_cd.n < 6; l++) {
        char name[16];
        if (!lv[l] || utf8((Il2CppString*)lv[l], name, sizeof name) <= 0) continue;
        float k = l < nd ? diffs[l] : 0;
        if (nd && k <= 0) continue;  // no such chart
        CardLevel& c = g_cd.lv[g_cd.n];
        snprintf(c.name, sizeof c.name, "%s", name);
        c.constant = k;
        Obj* rec = l < nr ? (Obj*)recs[l] : nullptr;
        c.played = rec && (recScore(rec) > 0 || recAcc(rec) > 0);
        if (c.played) {
            c.s = recScore(rec);
            c.a = recAcc(rec);
            c.c = recFC(rec);
        }
        char key[224];
        makeKey(key, sizeof key, id, name, g_cd.course);
        Detail* d = detFind(key, false);
        c.hasDet = d != nullptr;
        if (d) c.det = *d;
        if (l == s.level) g_cd.sel = g_cd.n;
        g_cd.n++;
    }
    return g_cd.n > 0;
}

static void openCard(void*) {
    closeCard(nullptr);
    detLoad();
    if (!gatherCard()) {
        LOG("details: card not opened (no song selected)");
        return;
    }
    Obj* root = overlayCanvas("PhiMeScoreCard", 32000, kH + 120);  // Clawd on top + margins must fit
    g_cRoot.set(root);
    Obj* dim = image(root, "dim", SPR_SOLID, Color{0.08f, 0.07f, 0.06f, 0.f}, Vec2{0.5f, 0.5f}, Vec2{0, 0},
                     Vec2{100, 100}, 0, true);
    stretch(rt(dim), -20);
    onClick(gameObject(dim), nullptr, closeCard, nullptr);
    g_cDim.set(dim);

    Obj* card = newUI("card", root);
    place(card, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, -16}, Vec2{kW, kH});
    g_cCard.set(card);
    image(card, "border", SPR_ROUND, rgb(C_LINE), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{kW + 4, kH + 4}, 26);
    image(card, "bg", SPR_ROUND, rgb(C_IVORY), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{kW, kH}, 24, true);
    Obj* clawd = image(card, "clawd", SPR_CLAWD, rgb(0xFFFFFF), Vec2{0.5f, 0.5f}, kClawdPos, Vec2{120, 68});
    g_cClawd.set(clawd);
    g_cClawdT = 0;

    // header: song, composer
    label(card, g_cd.title, 32, C_TEXT, MiddleLeft, true, Vec2{0, 0.5f}, Vec2{kPad, kTop - 46}, Vec2{kW - 2 * kPad - 150, 46});
    if (g_cd.composer[0])
        label(card, g_cd.composer, 19, C_MUTED, MiddleLeft, false, Vec2{0, 0.5f}, Vec2{kPad, kTop - 84},
              Vec2{kW - 2 * kPad - 150, 28});
    image(card, "divider", SPR_SOLID, rgb(C_LINE), Vec2{0.5f, 0.5f}, Vec2{0, kTop - 112}, Vec2{kW - 2 * kPad, 2});

    // level tabs (segmented control) + which records these are
    float tw = 138, th = 44, ty = kTop - 152;
    float total = g_cd.n * tw + 8;
    Obj* seg = image(card, "levels", SPR_ROUND, rgb(C_CHIP), Vec2{0, 0.5f}, Vec2{kPad + total / 2, ty},
                     Vec2{total, th + 8}, 16);
    // the white capsule under the chosen level: it glides to a new one (paintTabs)
    for (int i = 0; i < g_cd.n; i++) g_cAt[i] = Vec2{-total / 2 + 4 + tw * (i + 0.5f), 0};
    g_cThumb.set(image(rt(seg), "thumb", SPR_ROUND, rgb(0xFFFFFF), Vec2{0.5f, 0.5f}, g_cAt[g_cd.sel], Vec2{tw, th}, 12));
    for (int i = 0; i < g_cd.n; i++) {
        const CardLevel& c = g_cd.lv[i];
        Vec2 p = g_cAt[i];
        Obj* pill = image(rt(seg), "opt", SPR_ROUND, rgb(0xFFFFFF, 0), Vec2{0.5f, 0.5f}, p, Vec2{tw, th}, 12, true);
        char t[40];
        if (c.constant > 0) snprintf(t, sizeof t, "%s  %.1f", c.name, c.constant);
        else snprintf(t, sizeof t, "%s", c.name);
        Obj* l = label(rt(pill), t, 21, C_MUTED, MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0, 1}, Vec2{tw, th});
        g_cPill[i].set(pill);
        g_cLabel[i].set(l);
        onClick(gameObject(pill), pill, onTab, (void*)(intptr_t)i);
    }
    paintTabs();
    label(card, g_cd.course ? T("课题模式成绩 · 随云存档同步", "Course records · synced with the cloud save") : T("普通成绩", "Normal records"), 19, g_cd.course ? C_CLAY : C_MUTED, MiddleRight,
          false, Vec2{1, 0.5f}, Vec2{-kPad, ty}, Vec2{360, 28});

    buildCardPage();

    // footer
    label(card, T("分数和准度读自游戏存档，判定数由 PhiMe 记录，都随云存档同步", "Score and accuracy from the game save, judgements from PhiMe; both sync with the cloud save"), 17, C_MUTED, MiddleLeft, false,
          Vec2{0, 0}, Vec2{kPad, 30 + 28}, Vec2{kW - 2 * kPad - 170, 26});
    Obj* done = image(card, "done", SPR_ROUND, rgb(C_CLAY), Vec2{1, 0}, Vec2{-kPad - 70, 30 + 28}, Vec2{140, 56}, 14,
                      true);
    label(rt(done), T("完成", "Done"), 23, 0xFFFFFF, MiddleCenter, true, Vec2{0.5f, 0.5f}, Vec2{0, 1}, Vec2{130, 50});
    onClick(gameObject(done), done, closeCard, nullptr);

    setScale(card, 0.96f);
    g_cOpenT = 0;
    onEveryFrame(detailsFrame);
    LOG("details: card opened for '%s' (%d levels, showing %s, %s records)", g_cd.title, g_cd.n,
        g_cd.lv[g_cd.sel].name, g_cd.course ? "course" : "normal");
}

static void cardTick() {
    if (!g_cRoot.h) return;
    Obj* root = (Obj*)g_cRoot.get();
    if (!alive(root)) {  // scene changed under us
        closeCard(nullptr);
        return;
    }
    float dt = unscaledDt();
    if (g_cOpenT >= 0) {  // quiet fade + slight zoom, like the settings panel
        g_cOpenT += dt / 0.18f;
        float t = g_cOpenT >= 1 ? 1 : g_cOpenT;
        float u = 1 - t, e = 1 - u * u * u;
        Obj* card = (Obj*)g_cCard.get();
        if (alive(card)) setScale(card, 0.96f + 0.04f * e);
        Obj* dim = (Obj*)g_cDim.get();
        if (alive(dim)) setColor(dim, Color{0.08f, 0.07f, 0.06f, 0.4f * e});
        if (g_cOpenT >= 1) g_cOpenT = -1;
    }
    if (g_cPageT >= 0) {  // a level's page fades in after a tab change
        g_cPageT += dt / 0.16f;
        float t = g_cPageT >= 1 ? 1 : g_cPageT, u = 1 - t;
        Obj* pg = (Obj*)g_cPageGroup.get();
        if (alive(pg)) setGroupAlpha(pg, 1 - u * u * u);
        if (g_cPageT >= 1) g_cPageT = -1;
    }
    Obj* clawd = (Obj*)g_cClawd.get();
    if (alive(clawd)) {
        g_cClawdT += dt;
        setPos(rt(clawd), Vec2{kClawdPos.x, kClawdPos.y + clawdHop(g_cClawdT, 14)});
    }
}

static void detailsFrame(void*) {
    resultTick();
    songSelectTick();
    cardTick();
}

// ======================= install =======================
bool detailsInstall() {
    D.LOC = findClass("", "LevelOverControl");
    D.GI = findClass("", "GameInformation");
    D.Text = findClass("UnityEngine.UI", "Text");
    Il2CppClass* LRI = findClass("", "LevelResultInfo");
    Il2CppClass* LSI = findClass("", "LevelStartInfo");
    Il2CppClass* SS = findClass("", "SongSelector");
    Il2CppClass* Item = findClass("", "SongSelectorItem");
    Il2CppClass* Song = findClass("", "SongsItem");
    Il2CppClass* Rec = findClass("", "LevelRecord");
    if (!D.LOC || !D.GI || !LRI || !LSI) return false;

    D.rScore = findMethod(LRI, "get_IntScore", 0);
    D.rPercent = findMethod(LRI, "get_Percent", 0);
    D.rFC = findMethod(LRI, "get_FullCombo", 0);
    D.rPerfect = fieldOffset(LRI, "perfect");
    D.rGood = fieldOffset(LRI, "good");
    D.rBad = fieldOffset(LRI, "bad");
    D.rMiss = fieldOffset(LRI, "miss");
    D.rEarly = fieldOffset(LRI, "early");
    D.rLate = fieldOffset(LRI, "late");
    D.rCombo = fieldOffset(LRI, "maxCombo");
    D.lsiId = fieldType(LSI, "songsId") == IL2CPP_TYPE_STRING ? fieldOffset(LSI, "songsId") : -1;
    D.lsiLevel = fieldType(LSI, "songsLevel") == IL2CPP_TYPE_STRING ? fieldOffset(LSI, "songsLevel") : -1;
    D.ok = D.rScore && D.rPercent && D.rFC && D.rPerfect >= 0 && D.rGood >= 0 && D.rBad >= 0 && D.rMiss >= 0 &&
           D.lsiId >= 0 && D.lsiLevel >= 0;

    D.ssItems = SS ? findMethod(SS, "get_SongItems", 0) : nullptr;
    D.ssNowSong = SS && fieldType(SS, "nowSong") == IL2CPP_TYPE_I4 ? fieldOffset(SS, "nowSong") : -1;
    D.ssNowLevel = SS && fieldType(SS, "nowLevel") == IL2CPP_TYPE_I4 ? fieldOffset(SS, "nowLevel") : -1;
    D.ssScore = SS ? fieldOffset(SS, "score") : -1;
    D.ssAcc = SS ? fieldOffset(SS, "acc") : -1;
    D.ssTopBar = SS ? fieldOffset(SS, "topBar") : -1;
    D.ssStartAnim = SS ? fieldOffset(SS, "startAnimation") : -1;
    D.ssExit = SS && fieldType(SS, "exit") == IL2CPP_TYPE_BOOLEAN ? fieldOffset(SS, "exit") : -1;
    D.itemCover = Item ? fieldOffset(Item, "coverRect") : -1;
    Il2CppClass* LB = findClass("", "LevelBarController");
    Il2CppClass* LI = findClass("", "LevelInfo");
    Il2CppClass* RD = findClass("", "RatingDisplayController");
    D.ssLevelBar = SS ? fieldOffset(SS, "levelBarController") : -1;
    D.lbInfos = LB ? fieldOffset(LB, "levelInfos") : -1;
    D.liMain = LI ? fieldOffset(LI, "mainObject") : -1;
    D.ssRating = SS ? fieldOffset(SS, "ratingDisplayController") : -1;
    D.rdObjects = RD ? fieldOffset(RD, "ratingObjects") : -1;
    D.itemSong = Item ? fieldOffset(Item, "songInfo") : -1;
    D.itemRecords = Item ? fieldOffset(Item, "records") : -1;
    D.songId = Song && fieldType(Song, "songsId") == IL2CPP_TYPE_STRING ? fieldOffset(Song, "songsId") : -1;
    D.songName = Song && fieldType(Song, "songsName") == IL2CPP_TYPE_STRING ? fieldOffset(Song, "songsName") : -1;
    D.songComposer = Song && fieldType(Song, "composer") == IL2CPP_TYPE_STRING ? fieldOffset(Song, "composer") : -1;
    D.songLevels = Song ? fieldOffset(Song, "levels") : -1;
    D.songDiff = Song ? fieldOffset(Song, "difficulty") : -1;
    D.recS = Rec ? fieldOffset(Rec, "s") : -1;
    D.recA = Rec && fieldType(Rec, "a") == IL2CPP_TYPE_R4 ? fieldOffset(Rec, "a") : -1;
    D.recC = Rec ? fieldOffset(Rec, "c") : -1;
    D.recCBool = Rec && fieldType(Rec, "c") == IL2CPP_TYPE_BOOLEAN;
    D.txtOverflow = D.Text ? findMethod(D.Text, "set_horizontalOverflow", 1) : nullptr;
    D.ssOk = D.ssItems && D.ssNowSong >= 0 && D.ssNowLevel >= 0 && D.ssScore >= 0 && D.ssAcc >= 0 &&
             D.itemSong >= 0 && D.itemRecords >= 0 && D.songId >= 0 && D.songLevels >= 0 && D.recS >= 0 &&
             D.recA >= 0 && D.recC >= 0;
    LOG("details: results %s, song select %s (record c is %s)", D.ok ? "on" : "off", D.ssOk ? "on" : "off",
        D.recCBool ? "bool" : "int");
    return D.ok || D.ssOk;
}
