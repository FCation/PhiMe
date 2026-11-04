// Course mode (课题模式判定): a normal level judged and scored the way the game's own challenge mode does it.
//
// The game (4.0.x) already has all of it. ProgressControl.Update, every frame:
//   challenge (_inChallengeMode):  JudgeControl.InChallengeMode = true,
//                                  Perfect / Good / Bad window = 40 / 90 / 140 ms + half the average frame time
//                                  (over the last 10 frames)
//   otherwise:                     InChallengeMode = false, 80 / 180 / 220 ms
// and ScoreControl.Update, while JudgeControl.InChallengeMode is set:
//   score = (Perfect + 0.65 Good) / notes * 1000000      (no combo part; normally 900000 x acc + 100000 x combo)
// We only set those static values right after ProgressControl.Update, the way its challenge branch does, so the
// judging (taps, holds, flicks, touch windows) and the score come from the game's own code.
//
// Saving: the level and its result screen run inside the save sandbox (nothing reaches the normal records or
// the cloud). The course result is kept in our own PlayerPrefs keys, PhiMeMod.course.<song id>.<level>.
// Song select shows those records instead of the normal ones while course mode is on: the backing array of each
// SongSelectorItem's `records` list is swapped for one made from them (put back when it is switched off).
// The song and level are taken from GameInformation at level start; the result screen's own copy only appears
// once its Start coroutine runs, so the record is written a frame or two after LevelOverControl.Start.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

typedef void (*VoidFn)(Obj*, const MethodInfo*);
static VoidFn orig_PC_Update;

static struct {
    bool ok;
    FieldInfo *inChallenge, *perfect, *good, *bad;
    int pcChallenge;
    // song select
    const MethodInfo *ssItems, *ssSetText, *listSetRating;
    int itemSong, itemRecords, songId, songLevels, itemUp, itemDown, ssNowLevel;
    Il2CppClass* Record;
    const MethodInfo* recCtor;
    int recS, recA, recC;
    // results
    Il2CppClass *LOC, *Text;
    const MethodInfo *lriScore, *lriPercent, *lriFC;
    int lsiId, lsiLevel;
    Il2CppClass* GI;  // GameInformation (its static _main holds levelStartInfo / levelResultInfo)
} K;

static bool g_levelCourse = false;  // this level (and its result screen) is a course run
static char g_songId[160], g_songLevel[16];  // read at level start (the result scene fills its own copy late)
static float g_dts[10];
static int g_dtN = 0, g_dtI = 0;

static bool g_gameChallenge = false;  // this level is the game's own challenge mode

bool courseLevelActive() { return g_levelCourse; }
bool gameChallengeLevel() { return g_gameChallenge; }

// ---- reading fields that may be static ----
static FieldInfo* field(Il2CppClass* k, const char* name) { return k ? il2cpp_class_get_field_from_name(k, name) : nullptr; }
static int fieldType(Il2CppClass* k, const char* name) {
    FieldInfo* f = field(k, name);
    return f ? il2cpp_type_get_type(il2cpp_field_get_type(f)) : -1;
}
static bool isType(Il2CppClass* k, const char* name, int t) { return fieldType(k, name) == t; }
static Obj* objField(Il2CppClass* k, Obj* inst, const char* name) {
    FieldInfo* f = field(k, name);
    if (!f) return nullptr;
    if (il2cpp_field_get_flags(f) & 0x10) {  // FIELD_ATTRIBUTE_STATIC
        Obj* v = nullptr;
        il2cpp_field_static_get_value(f, &v);
        return v;
    }
    return inst ? fld<Obj*>(inst, (int)il2cpp_field_get_offset(f)) : nullptr;
}

// ---- records ----
struct Rec {
    int s;
    float a;
    bool c;
    bool has;
};
static void recKey(char* out, int cap, const char* song, const char* level) {
    snprintf(out, cap, "PhiMeMod.course.%s.%s", song, level);
}
static Rec recLoad(const char* song, const char* level) {
    char k[256], v[64];
    recKey(k, sizeof k, song, level);
    Rec r{0, 0, false, false};
    if (prefGetString(k, v, sizeof v) <= 0) return r;
    int s = 0, a = 0, c = 0, n = 0;
    // "score acc*10000 fc"
    const char* p = v;
    int* dst[3] = {&s, &a, &c};
    while (*p && n < 3) {
        while (*p == ' ') p++;
        if (!*p) break;
        int x = 0;
        while (*p >= '0' && *p <= '9') x = x * 10 + (*p++ - '0');
        *dst[n++] = x;
        while (*p && *p != ' ') p++;
    }
    if (n < 2) return r;
    r = Rec{s, a / 10000.f, c != 0, true};
    return r;
}
static void recSave(const char* song, const char* level, const Rec& r) {
    char k[256], v[64];
    recKey(k, sizeof k, song, level);
    snprintf(v, sizeof v, "%d %d %d", r.s, (int)(r.a * 10000 + 0.5f), r.c ? 1 : 0);
    prefSetStringRaw(k, v);
    prefSave();
}

// for the PhiMe save file (savedata.cpp / details.cpp)
bool courseRecGet(const char* id, const char* lv, int* s, float* a, bool* c) {
    Rec r = recLoad(id, lv);
    if (!r.has) return false;
    *s = r.s;
    *a = r.a;
    *c = r.c;
    return true;
}
// keeps the better of what is there and what comes in; the caller calls prefSave() once at the end
void courseRecMerge(const char* id, const char* lv, int s, float a, bool c) {
    Rec r = recLoad(id, lv);
    if (!r.has || s > r.s) r.s = s;
    if (!r.has || a > r.a) r.a = a;
    r.c = (r.has && r.c) || c;
    char k[256], v[64];
    recKey(k, sizeof k, id, lv);
    snprintf(v, sizeof v, "%d %d %d", r.s, (int)(r.a * 10000 + 0.5f), r.c ? 1 : 0);
    prefSetStringRaw(k, v);
}

// ---- level ----
static Obj* gameInfo() { return K.GI ? objField(K.GI, nullptr, "_main") : nullptr; }

static bool songOf(Obj* lsi, char* id, int idCap, char* lv, int lvCap) {
    id[0] = lv[0] = 0;
    if (!lsi || K.lsiId < 0 || K.lsiLevel < 0) return false;
    utf8(fld<Il2CppString*>(lsi, K.lsiId), id, idCap);
    utf8(fld<Il2CppString*>(lsi, K.lsiLevel), lv, lvCap);
    return id[0] && lv[0];
}

void courseLevelStart(Obj* pc) {
    configLoad();
    g_dtN = g_dtI = 0;
    bool gameChallenge = K.pcChallenge >= 0 && fld<bool>(pc, K.pcChallenge);
    g_gameChallenge = gameChallenge;
    g_levelCourse = K.ok && g_cfg.course && !gameChallenge;
    if (g_levelCourse) {
        sandboxBegin(true);  // nothing of a course run reaches the normal records or the cloud
        Obj* gi = gameInfo();
        songOf(gi ? objField(K.GI, gi, "levelStartInfo") : nullptr, g_songId, sizeof g_songId, g_songLevel,
               sizeof g_songLevel);
        LOG("course: level start, course judging on (Perfect 40 / Good 90 / Bad 140 ms + half a frame), song '%s' "
            "level '%s'",
            g_songId, g_songLevel);
    }
}

static void setStatic(FieldInfo* f, float v) { il2cpp_field_static_set_value(f, &v); }

static void hk_PC_Update(Obj* self, const MethodInfo* m) {
    orig_PC_Update(self, m);
    if (!g_levelCourse) return;
    if (K.pcChallenge >= 0 && fld<bool>(self, K.pcChallenge)) {  // the game's own challenge: hands off
        g_levelCourse = false;
        return;
    }
    // what ProgressControl.Update does in challenge mode: half the average of the last 10 frame times
    g_dts[g_dtI] = unscaledDt();
    g_dtI = (g_dtI + 1) % 10;
    if (g_dtN < 10) g_dtN++;
    float sum = 0;
    for (int i = 0; i < g_dtN; i++) sum += g_dts[i];
    float half = g_dtN ? sum / g_dtN * 0.5f : 0;
    if (half > 0.05f) half = 0.05f;  // a hitch (loading, pause) must not open the windows wide
    bool on = true;
    il2cpp_field_static_set_value(K.inChallenge, &on);
    setStatic(K.perfect, half + 0.04f);
    setStatic(K.good, half + 0.09f);
    setStatic(K.bad, half + 0.14f);
    PM_ONCE("course: judge windows set by us after ProgressControl.Update");
}

// ---- song select: show course records ----
// SongSelectorItem.records is a List<LevelRecord> (one per level, filled from GameInformation.GetSongAllRecord)
// and SongSelector.SetText reads it with get_Item. We swap the list's backing array for one made of our
// records and put the original array back when course mode is switched off.
struct Swapped {
    Handle list, orig;  // the List object and its original _items array (or the item + its array, see isArr)
    bool isArr;
};
static Swapped g_sw[1024];
static int g_swN = 0;
static Handle g_ss;

// elements of a List<T> or a T[]
static int seqView(Obj* o, Obj*** out, Il2CppArray** backing = nullptr) {
    if (!o) return 0;
    Il2CppClass* k = il2cpp_object_get_class(o);
    FieldInfo* fi = il2cpp_class_get_field_from_name(k, "_items");
    FieldInfo* fs = il2cpp_class_get_field_from_name(k, "_size");
    Il2CppArray* a;
    int n;
    if (fi && fs) {
        a = fld<Il2CppArray*>(o, (int)il2cpp_field_get_offset(fi));
        n = fld<int>(o, (int)il2cpp_field_get_offset(fs));
        if (!a || n < 0 || (uintptr_t)n > a->max_length) return 0;
    } else {
        const char* nm = il2cpp_class_get_name(k);
        size_t L = strlen(nm);
        if (L < 2 || nm[L - 2] != '[' || nm[L - 1] != ']') return 0;
        a = (Il2CppArray*)o;
        n = (int)a->max_length;
    }
    if (backing) *backing = a;
    *out = (Obj**)arrayData(a);
    return n;
}
static int listItemsOffset(Obj* list) {
    FieldInfo* f = list ? il2cpp_class_get_field_from_name(il2cpp_object_get_class(list), "_items") : nullptr;
    return f ? (int)il2cpp_field_get_offset(f) : -1;
}

static void restoreAll() {
    for (int i = 0; i < g_swN; i++) {
        Obj* o = (Obj*)g_sw[i].list.get();  // plain managed objects (no Unity alive check), kept by the handle
        if (o) {
            if (g_sw[i].isArr) fld<Obj*>(o, K.itemRecords) = (Obj*)g_sw[i].orig.get();
            else {
                int off = listItemsOffset(o);
                if (off >= 0) fld<Obj*>(o, off) = (Obj*)g_sw[i].orig.get();
            }
        }
        g_sw[i].list.clear();
        g_sw[i].orig.clear();
    }
    g_swN = 0;
}

static int applyAll(Obj* ss, int* items_, int* noLevels) {
    Obj** items = nullptr;
    int n = seqView(callm<Obj*>(K.ssItems, ss), &items);
    int shown = 0;
    for (int i = 0; i < n && g_swN < 1024; i++) {
        Obj* it = items[i];
        if (!it) continue;
        Obj* song = fld<Obj*>(it, K.itemSong);
        Obj* recsObj = fld<Obj*>(it, K.itemRecords);
        Obj** recs = nullptr;
        Il2CppArray* back = nullptr;
        int nr = seqView(recsObj, &recs, &back);
        Obj** levels = nullptr;
        int nl = song ? seqView(fld<Obj*>(song, K.songLevels), &levels) : 0;
        char id[160];
        if (!song || !back || utf8(fld<Il2CppString*>(song, K.songId), id, sizeof id) <= 0) continue;
        if (nl <= 0) {
            (*noLevels)++;
            continue;
        }
        bool isArr = (Obj*)back == recsObj;
        uintptr_t len = back->max_length;  // the list's capacity (its count stays as it is)
        Il2CppArray* mine = newArray(K.Record, len);
        for (int l = 0; l < nr; l++) {
            Obj* o = recs[l];
            char lv[16] = "";
            if (l < nl) utf8((Il2CppString*)levels[l], lv, sizeof lv);
            Rec r = lv[0] ? recLoad(id, lv) : Rec{0, 0, false, false};
            if (!o && !r.has) continue;  // nothing played either way: leave the slot empty like the game does
            Obj* rec = il2cpp_object_new(K.Record);
            if (K.recCtor) callm<void>(K.recCtor, rec);
            fld<int>(rec, K.recS) = r.s;
            fld<float>(rec, K.recA) = r.a;
            fld<bool>(rec, K.recC) = r.c;
            arrayData(mine)[l] = rec;
            if (r.has) shown++;
        }
        g_sw[g_swN].isArr = isArr;
        g_sw[g_swN].list.set(isArr ? it : recsObj);
        g_sw[g_swN].orig.set(back);
        g_swN++;
        if (isArr) fld<Il2CppArray*>(it, K.itemRecords) = mine;
        else fld<Il2CppArray*>(recsObj, listItemsOffset(recsObj)) = mine;
        (*items_)++;
    }
    return shown;
}

// The song list's rank letters (SongListItem, two per song: the upper and lower list) were drawn from the records
// when the list was made; draw them again from the records now in place (course or normal), for the level shown.
static int refreshList(Obj* ss) {
    if (!K.listSetRating || K.itemUp < 0 || K.itemDown < 0 || K.ssNowLevel < 0) return -1;
    int level = fld<int>(ss, K.ssNowLevel), done = 0;
    Obj** items = nullptr;
    int n = seqView(callm<Obj*>(K.ssItems, ss), &items);
    for (int i = 0; i < n; i++) {
        Obj* it = items[i];
        if (!it) continue;
        Obj** recs = nullptr;
        int nr = seqView(fld<Obj*>(it, K.itemRecords), &recs);
        int l = level < nr ? level : nr - 1;
        Obj* rec = l >= 0 ? recs[l] : nullptr;
        int score = rec ? fld<int>(rec, K.recS) : 0;
        bool fc = rec && fld<uint8_t>(rec, K.recC) != 0;
        Obj* sides[2] = {fld<Obj*>(it, K.itemUp), fld<Obj*>(it, K.itemDown)};
        for (Obj* li : sides) {
            if (!alive(li)) continue;
            void* a[] = {&score, &fc};
            if (invokeSafe(K.listSetRating, li, a)) done++;
        }
    }
    return done / 2;
}

void courseSongSelect(Obj* ss) {
    if (!K.ok || !K.ssItems || K.itemRecords < 0) {
        LOG("course: song select records unavailable (items %d, records field %d)", K.ssItems != nullptr,
            K.itemRecords);
        return;
    }
    configLoad();
    if (ss != g_ss.get()) {  // a new song select (restoring the old lists is harmless if they are abandoned)
        restoreAll();
        g_ss.set(ss);
    }
    restoreAll();
    int items = 0, noLevels = 0;
    int shown = g_cfg.course ? applyAll(ss, &items, &noLevels) : 0;
    if (K.ssSetText) invokeSafe(K.ssSetText, ss);
    int listed = refreshList(ss);
    LOG("course: song list ratings redrawn for %d songs", listed);
    LOG("course: song select shows %s records (%d songs swapped, %d course records, %d without level names)",
        g_cfg.course ? "course" : "normal", items, shown, noLevels);
}

// ---- results ----
static Handle g_loc, g_bestStr, g_bestText;
static bool g_resultBuilt = false;

static Obj* textIn(Obj* o) {
    if (!alive(o)) return nullptr;
    const char* kn = il2cpp_class_get_name(il2cpp_object_get_class(o));
    if (strcmp(kn, "Text") == 0) return o;
    Obj* go = strcmp(kn, "GameObject") == 0 ? o : gameObject(o);
    Il2CppArray* ts = K.Text ? componentsInChildren(go, K.Text) : nullptr;
    return ts && ts->max_length ? (Obj*)arrayData(ts)[0] : nullptr;
}

static bool g_recorded = false;
static float g_resT0 = 0;

// LevelOverControl.Start is a coroutine: _levelStartInfo / levelResultInfo are only set once it runs, so the
// record is taken on a later frame (song from the result screen, else from GameInformation, else from the
// level start)
static bool takeRecord(Obj* loc, bool lastTry) {
    Obj* gi = gameInfo();
    Obj* lsi = objField(K.LOC, loc, "_levelStartInfo");
    Obj* lri = objField(K.LOC, loc, "levelResultInfo");
    if ((!lsi || !lri) && !lastTry) return false;
    if (!lsi && gi) lsi = objField(K.GI, gi, "levelStartInfo");
    if (!lri && gi) lri = objField(K.GI, gi, "levelResultInfo");
    char id[160], lv[16];
    const char* from = "result";
    if (!songOf(lsi, id, sizeof id, lv, sizeof lv)) {
        snprintf(id, sizeof id, "%s", g_songId);
        snprintf(lv, sizeof lv, "%s", g_songLevel);
        from = "level start";
    }
    if (!lri || !id[0] || !lv[0]) {
        LOG("course: result without song info (result %p, start %p, id '%s', level '%s')", (void*)lri, (void*)lsi, id,
            lv);
        return true;
    }
    int score = K.lriScore ? callm<int>(K.lriScore, lri) : 0;
    float pct = K.lriPercent ? callm<float>(K.lriPercent, lri) : 0;
    bool fc = K.lriFC ? callm<bool>(K.lriFC, lri) : false;
    Rec old = recLoad(id, lv);
    bool record = !autoplayLevelActive() && !g_cfg.practice;
    bool newBest = record && score > old.s;
    if (record) {
        Rec nr = old;
        if (score > nr.s) nr.s = score;
        if (pct > nr.a) nr.a = pct;
        nr.c = nr.c || fc;
        nr.has = true;
        recSave(id, lv, nr);
    }
    char b[96];
    if (newBest) snprintf(b, sizeof b, "NEW BEST    %07d   +%07d", score, score - old.s);
    else snprintf(b, sizeof b, "BEST       %07d", old.s > score || !record ? old.s : score);
    g_bestStr.set(str(b));
    g_bestText.set(textIn(objField(K.LOC, loc, "best")));
    LOG("course: %s %s (from %s) -> %07d %.2f%% fc=%d (best was %07d)%s", id, lv, from, score, pct, fc, old.s,
        record ? (newBest ? ", new best saved" : ", saved") : ", not saved (autoplay / practice)");
    return true;
}

static void resultFrame(void*) {
    Obj* loc = (Obj*)g_loc.get();
    if (!loc) return;
    if (!alive(loc)) {
        g_loc.clear();
        g_bestText.clear();
        return;
    }
    if (!g_recorded) g_recorded = takeRecord(loc, timeSinceLevelLoad() - g_resT0 > 1.5f);
    // the game writes its own BEST line (from the normal records), possibly late: keep ours
    Obj* bt = (Obj*)g_bestText.get();
    // (not while the ACC best line is shown instead: resacc.cpp keeps this one to switch back to)
    if (alive(bt) && g_bestStr.h && !resAccOwnsBest() && textStr(bt) != g_bestStr.get())
        setTextStr(bt, (Il2CppString*)g_bestStr.get());
    if (g_resultBuilt) return;
    g_resultBuilt = true;
    // "COURSE MODE" right above the score, on the score's side
    Obj* score = textIn(objField(K.LOC, loc, "scoreText"));
    if (alive(score)) {
        int col = textAlignment(score) % 3;
        int fs = textFontSize(score);
        int size = fs > 0 ? (int)(fs * 0.3f + 0.5f) : 22;
        if (size < 18) size = 18;
        Obj* t = text(rt(score), "COURSE MODE", size, rgb(0xF0A27F), 6 + col /* Lower + same column */, true,
                      Vec2{col * 0.5f, 1}, Vec2{col * 0.5f, 0}, Vec2{0, 2}, Vec2{400, size * 1.4f});
        setTextFont(t, textFont(score));
        LOG("course: result label added (score text %d px, align %d)", fs, textAlignment(score));
    }
}

void courseResultStart(Obj* loc) {
    g_loc.clear();
    g_bestText.clear();
    g_bestStr.clear();
    g_resultBuilt = false;
    g_recorded = false;
    if (!g_levelCourse || !K.ok) return;
    g_resT0 = timeSinceLevelLoad();
    g_loc.set(loc);
    onEveryFrame(resultFrame);
}

bool courseInstall() {
    Il2CppClass* JC = findClass("", "JudgeControl");
    Il2CppClass* PC = findClass("", "ProgressControl");
    if (!JC || !PC) return false;
    K.inChallenge = field(JC, "InChallengeMode");
    K.perfect = field(JC, "PerfectTimeRange");
    K.good = field(JC, "GoodTimeRange");
    K.bad = field(JC, "BadTimeRange");
    K.pcChallenge = fieldOffset(PC, "_inChallengeMode");
    if (!K.inChallenge || !K.perfect || !K.good || !K.bad) return false;

    Il2CppClass* SS = findClass("", "SongSelector");
    Il2CppClass* Item = findClass("", "SongSelectorItem");
    Il2CppClass* Song = findClass("", "SongsItem");
    K.Record = findClass("", "LevelRecord");
    K.ssItems = SS ? findMethod(SS, "get_SongItems", 0) : nullptr;
    K.ssSetText = SS ? findMethod(SS, "SetText", 0) : nullptr;
    K.itemSong = Item ? fieldOffset(Item, "songInfo") : -1;
    K.itemRecords = Item ? fieldOffset(Item, "records") : -1;
    K.songId = Song ? fieldOffset(Song, "songsId") : -1;
    K.songLevels = Song ? fieldOffset(Song, "levels") : -1;
    K.recCtor = K.Record ? findMethod(K.Record, ".ctor", 0) : nullptr;
    K.recS = K.Record ? fieldOffset(K.Record, "s") : -1;
    K.recA = K.Record ? fieldOffset(K.Record, "a") : -1;
    K.recC = K.Record ? fieldOffset(K.Record, "c") : -1;
    K.itemUp = Item ? fieldOffset(Item, "selectorItemUpside") : -1;
    K.itemDown = Item ? fieldOffset(Item, "selectorItemDownside") : -1;
    K.ssNowLevel = SS && isType(SS, "nowLevel", IL2CPP_TYPE_I4) ? fieldOffset(SS, "nowLevel") : -1;
    Il2CppClass* SLI = findClass("", "SongListItem");
    K.listSetRating = SLI ? findMethod(SLI, "SetRatingWithScore", 2) : nullptr;
    // records: List<LevelRecord> in 4.0.x; levels: a list or an array of names (both are read either way)
    if (K.itemSong < 0 || K.songId < 0 || K.songLevels < 0 || K.recS < 0 || K.recA < 0 || K.recC < 0 ||
        !K.Record || !isType(Song, "songsId", IL2CPP_TYPE_STRING))
        K.itemRecords = -1;  // no song-select records then, the rest still works
    LOG("course: song select records %s (records field type %d, levels field type %d)",
        K.itemRecords >= 0 ? "on" : "off", fieldType(Item, "records"), fieldType(Song, "levels"));

    K.LOC = findClass("", "LevelOverControl");
    K.Text = findClass("UnityEngine.UI", "Text");
    Il2CppClass* LRI = findClass("", "LevelResultInfo");
    Il2CppClass* LSI = findClass("", "LevelStartInfo");
    K.GI = findClass("", "GameInformation");
    K.lriScore = LRI ? findMethod(LRI, "get_IntScore", 0) : nullptr;
    K.lriPercent = LRI ? findMethod(LRI, "get_Percent", 0) : nullptr;
    K.lriFC = LRI ? findMethod(LRI, "get_FullCombo", 0) : nullptr;
    K.lsiId = LSI && isType(LSI, "songsId", IL2CPP_TYPE_STRING) ? fieldOffset(LSI, "songsId") : -1;
    K.lsiLevel = LSI && isType(LSI, "songsLevel", IL2CPP_TYPE_STRING) ? fieldOffset(LSI, "songsLevel") : -1;
    if (K.lsiLevel < 0) LOG("course: LevelStartInfo.songsLevel is not a string, course records off");

    orig_PC_Update = (VoidFn)hookMethod(findMethod(PC, "Update", 0), (void*)hk_PC_Update);
    K.ok = orig_PC_Update != nullptr;
    return K.ok;
}
