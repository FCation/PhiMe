// 存档: export / import of the official save and of PhiMe's own data, an all-unlocked empty save, and the
// first-run welcome that offers it.
//
// The official save (4.0.x) is what the cloud sync uploads: five modules (settings, user, gameProgress,
// gameRecord, gameKey), each `LoadFromLocal()` (from PlayerPrefs) -> `Serialize()` -> SaveManagement.Encrypt2,
// written as one file per module: [version byte][encrypted data]. The game has both directions itself:
//   CloudSaveManager.SaveToFolder(path)                         local save -> those 5 files
//   CloudSaveManager.LoadFromFolder(path, loadSettings, version) those files -> PlayerPrefs.DeleteAll, Apply each
// and a cloud download is: ZipFile.ExtractToDirectory(zip, dir) -> LoadFromFolder(dir, ...) ->
// GameInformation.DestroyMainForRestart -> SceneManager.LoadScene(0). We do exactly that, so an exported file is
// a zip of the 5 files, the same as a cloud save (community tools read it too), and importing one goes through
// the game's own loader. version >= 2 = Encrypt2 / Decrypt2 (what SaveToFolder writes).
//
// LoadFromFolder wipes PlayerPrefs, where PhiMe keeps its settings and course records: those are read before
// and written back after. Every import first exports the current save to saves/ (Phigros-backup-*.zip).
//
// The all-unlocked empty save is made by the game's own modules, from the game's own data:
//   gameKey      every key in GameInformation.keyStore taken as many times as it can be (GameInformation.GetKey,
//                what buying a song / getting a collection item does)
//   gameProgress the current progress, with every story / chapter / event flag set (see tweakProgress)
//   gameRecord   empty: nothing played
//   settings, user  kept as they are
// then imported like any other file (a copy stays in saves/Phigros-AllUnlocked.zip).
#include "mod.h"

using namespace il2cpp;
using namespace unity;

typedef void (*VoidFn)(Obj*, const MethodInfo*);
static VoidFn orig_CS_Start;

// SaveToFolder's order
static const char* const kModClass[5] = {"SettingsSaveModule", "UserSaveModule", "GameProgressSaveModule",
                                         "GameRecordSaveModule", "GameKeySaveModule"};
enum { M_SETTINGS, M_USER, M_PROGRESS, M_RECORD, M_KEY };

static struct {
    bool ok, unlockOk;
    Il2CppClass* mod[5];
    const MethodInfo *modCtor[5], *modName[5], *modVersion[5], *modLocal[5], *modSerialize[5];
    const MethodInfo *saveToFolder, *loadFromFolder, *extract, *dirExists, *dirDelete, *dirCreate, *dirFiles,
        *fileTime, *tmpPath, *restart, *loadScene, *encrypt2, *now, *dtString, *getKey, *txtOverflow, *allSongs;
    Il2CppClass* GI;
    int giKeyStore, giSong, recData;
    int keyName, keyUnlocked, keyKind, keyTimes;
    int songId, songLevels;
} S;
static char g_names[5][32];  // module file names ("gameKey", ...), read from the modules once

// ======================= small helpers =======================
static Obj* gameInfo() {
    FieldInfo* f = S.GI ? il2cpp_class_get_field_from_name(S.GI, "_main") : nullptr;
    Obj* v = nullptr;
    if (f) il2cpp_field_static_get_value(f, &v);
    return v;
}

// elements of a List<T> or a T[]
static int seq(Obj* o, void*** out) {
    *out = nullptr;
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
    *out = arrayData(a);
    return n;
}

void forEachChart(void (*cb)(const char*, const char*, void*), void* user) {
    Obj* gi = gameInfo();
    Obj* song = gi && S.giSong >= 0 ? fld<Obj*>(gi, S.giSong) : nullptr;
    if (!song || !S.allSongs || S.songId < 0 || S.songLevels < 0) return;
    bool ok;
    Obj* list = invokeObj(S.allSongs, song, nullptr, &ok);
    void** songs;
    int n = ok ? seq(list, &songs) : 0;
    for (int i = 0; i < n; i++) {
        Obj* s = (Obj*)songs[i];
        char id[160], lv[16];
        if (!s || utf8(fld<Il2CppString*>(s, S.songId), id, sizeof id) <= 0) continue;
        void** lvs;
        int nl = seq(fld<Obj*>(s, S.songLevels), &lvs);
        for (int l = 0; l < nl; l++)
            if (lvs[l] && utf8((Il2CppString*)lvs[l], lv, sizeof lv) > 0) cb(id, lv, user);
    }
}

// ---- files ----
struct Bytes {
    uint8_t* p = nullptr;
    int n = 0, cap = 0;
    bool add(const void* d, int len) {
        if (len <= 0) return true;
        if (n + len > cap) {
            int c = cap ? cap : 8192;
            while (c < n + len) c *= 2;
            uint8_t* q = (uint8_t*)malloc(c);
            if (!q) return false;
            if (p) {
                memcpy(q, p, n);
                free(p);
            }
            p = q;
            cap = c;
        }
        memcpy(p + n, d, len);
        n += len;
        return true;
    }
    void u16(int v) {
        uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
        add(b, 2);
    }
    void u32(uint32_t v) {
        uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
        add(b, 4);
    }
    void release() {
        free(p);
        p = nullptr;
        n = cap = 0;
    }
};

static bool readFile(const char* path, Bytes& b) {
    int fd = open(path, 0);
    if (fd < 0) return false;
    char tmp[8192];
    for (;;) {
        ssize_t r = read(fd, tmp, sizeof tmp);
        if (r <= 0) break;
        b.add(tmp, (int)r);
    }
    close(fd);
    return true;
}
static bool writeFile(const char* path, const void* d, int n) {
    int fd = open(path, PM_O_REPLACE, 0660);
    if (fd < 0) return false;
    ssize_t w = write(fd, d, n);
    close(fd);
    return w == n;
}
static bool fileExists(const char* path) {
    int fd = open(path, 0);
    if (fd < 0) return false;
    close(fd);
    return true;
}

// the PhiMeMod folder: Android /sdcard/Android/media/<pkg>/PhiMeMod (next to the log), iOS Documents/PhiMeMod
static char g_base[384];
static const char* baseDir() {
    if (g_base[0]) return g_base;
#if defined(__APPLE__)
    const char* home = getenv("HOME");
    snprintf(g_base, sizeof g_base, "%s/Documents/PhiMeMod", home ? home : ".");
    mkdir(g_base, 0755);
#else
    char pkg[160] = {0};
    int fd = open("/proc/self/cmdline", 0);
    if (fd >= 0) {
        ssize_t n = read(fd, pkg, sizeof(pkg) - 1);
        close(fd);
        if (n < 0) pkg[0] = 0;
    }
    for (int i = 0; pkg[i]; i++)
        if (pkg[i] == ':') pkg[i] = 0;
    if (!pkg[0]) snprintf(pkg, sizeof pkg, "com.PigeonGames.Phigros");
    snprintf(g_base, sizeof g_base, "/sdcard/Android/media/%s", pkg);
    mkdir(g_base, 0770);
    snprintf(g_base, sizeof g_base, "/sdcard/Android/media/%s/PhiMeMod", pkg);
    mkdir(g_base, 0770);
#endif
    return g_base;
}
const char* modFolderPath() { return baseDir(); }
static void folder(char* out, int cap, const char* sub) {
    snprintf(out, cap, "%s/%s", baseDir(), sub);
    mkdir(out, 0770);
}
const char* saveFolderHint() {
#if defined(__APPLE__)
    return T("App 的 Documents/PhiMeMod", "the app's Documents/PhiMeMod");  // visible in the Files app when the IPA was signed with file sharing on
#else
    static char b[200];
    const char* base = baseDir();
    const char* p = strlen(base) > 8 && !memcmp(base, "/sdcard/", 8) ? base + 8 : base;
    snprintf(b, sizeof b, "%s", p);
    return b;
#endif
}
static const char* fileName(const char* path) {
    const char* n = path;
    for (const char* q = path; *q; q++)
        if (*q == '/') n = q + 1;
    return n;
}

// ---- managed file system (the game's own calls) ----
static bool mdirExists(const char* p) {
    void* a[] = {str(p)};
    bool ok;
    Obj* r = invokeObj(S.dirExists, nullptr, a, &ok);
    return ok && r && *(bool*)il2cpp_object_unbox(r);
}
static bool mdirReset(const char* p) {
    if (mdirExists(p)) {
        bool rec = true;
        void* a[] = {str(p), &rec};
        invokeSafe(S.dirDelete, nullptr, a);
    }
    void* a[] = {str(p)};
    return invokeSafe(S.dirCreate, nullptr, a);
}
// an empty folder in the game's temporary cache: "<cache>/<name>/" (with the slash the save calls expect)
static bool tmpDir(char* out, int cap, const char* name) {
    bool ok;
    Il2CppString* t = (Il2CppString*)invokeObj(S.tmpPath, nullptr, nullptr, &ok);
    char b[320];
    if (!ok || utf8(t, b, sizeof b) <= 0) return false;
    snprintf(out, cap, "%s/%s/", b, name);
    return mdirReset(out);
}

static void stamp(char* out, int cap) {
    out[0] = 0;
    if (S.now && S.dtString) {
        uint64_t dt = callm<uint64_t>(S.now);
        utf8(callm<Il2CppString*>(S.dtString, &dt, str("yyyyMMdd-HHmmss")), out, cap);
    }
    if (!out[0]) snprintf(out, cap, "%ld", time(nullptr));
}

// the newest file in `dir` ending with `ext` (".zip" / ".json")
static bool newestFile(const char* dir, const char* ext, char* out, int cap) {
    out[0] = 0;
    if (!S.dirFiles) return false;
    void* a[] = {str(dir)};
    bool ok;
    Obj* arr = invokeObj(S.dirFiles, nullptr, a, &ok);
    void** files;
    int n = ok ? seq(arr, &files) : 0;
    uint64_t best = 0;
    int el = (int)strlen(ext);
    for (int i = 0; i < n; i++) {
        char p[512];
        int len = files[i] ? utf8((Il2CppString*)files[i], p, sizeof p) : 0;
        if (len <= el) continue;
        bool match = true;
        for (int k = 0; k < el; k++) {
            char c = p[len - el + k];
            if (c >= 'A' && c <= 'Z') c += 32;
            if (c != ext[k]) match = false;
        }
        if (!match) continue;
        uint64_t t = 1;
        if (S.fileTime) {
            void* b[] = {files[i]};
            Obj* r = invokeObj(S.fileTime, nullptr, b, &ok);
            if (ok && r) t = *(uint64_t*)il2cpp_object_unbox(r) & 0x3FFFFFFFFFFFFFFFull;  // ticks (no Kind bits)
        }
        if (t >= best) {
            best = t;
            snprintf(out, cap, "%s", p);
        }
    }
    return out[0] != 0;
}

// ---- zip (stored, no compression: the save files are encrypted anyway) ----
static uint32_t crc32(const uint8_t* d, int n) {
    static uint32_t t[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++) c = t[(c ^ d[i]) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static const char* const* moduleNames();

// the 5 module files of `dir` (ending in '/') -> zipPath; false if none was there
static bool zipFolder(const char* dir, const char* zipPath) {
    const char* const* names = moduleNames();
    Bytes z, cd;
    int count = 0;
    for (int i = 0; i < 5; i++) {
        if (!names[i][0]) continue;
        char p[512];
        snprintf(p, sizeof p, "%s%s", dir, names[i]);
        Bytes f;
        if (!readFile(p, f)) continue;
        uint32_t crc = crc32(f.p, f.n), off = (uint32_t)z.n;
        int nl = (int)strlen(names[i]);
        z.u32(0x04034b50);
        z.u16(20), z.u16(0), z.u16(0), z.u16(0), z.u16(0x21);  // version, flags, stored, time, date 1980-01-01
        z.u32(crc), z.u32(f.n), z.u32(f.n);
        z.u16(nl), z.u16(0);
        z.add(names[i], nl);
        z.add(f.p, f.n);
        cd.u32(0x02014b50);
        cd.u16(20), cd.u16(20), cd.u16(0), cd.u16(0), cd.u16(0), cd.u16(0x21);
        cd.u32(crc), cd.u32(f.n), cd.u32(f.n);
        cd.u16(nl), cd.u16(0), cd.u16(0), cd.u16(0), cd.u16(0);
        cd.u32(0), cd.u32(off);
        cd.add(names[i], nl);
        f.release();
        count++;
    }
    uint32_t cdOff = (uint32_t)z.n;
    z.add(cd.p, cd.n);
    z.u32(0x06054b50);
    z.u16(0), z.u16(0), z.u16(count), z.u16(count);
    z.u32(cd.n), z.u32(cdOff), z.u16(0);
    bool ok = count > 0 && writeFile(zipPath, z.p, z.n);
    LOG("savedata: %s, %d files, %d bytes%s", zipPath, count, z.n, ok ? "" : " (NOT written)");
    z.release();
    cd.release();
    return ok;
}

// ---- the save modules ----
static Obj* newModule(int i) {
    if (!S.mod[i]) return nullptr;
    Obj* o = il2cpp_object_new(S.mod[i]);
    return invokeSafe(S.modCtor[i], o) ? o : nullptr;
}
static const char* const* moduleNames() {
    static const char* ptrs[5];
    for (int i = 0; i < 5; i++) {
        if (!g_names[i][0]) {
            Obj* m = newModule(i);
            bool ok;
            Obj* s = m ? invokeObj(S.modName[i], m, nullptr, &ok) : nullptr;
            if (s && ok) utf8((Il2CppString*)s, g_names[i], sizeof g_names[i]);
        }
        ptrs[i] = g_names[i];
    }
    return ptrs;
}

// ======================= PhiMe's own PlayerPrefs, kept across LoadFromFolder's DeleteAll =======================
struct KV {
    char k[256], v[64];
};
static KV* g_kv = nullptr;
static int g_kvN = 0, g_kvCap = 0;
static void snapOne(const char* id, const char* lv, void*) {
    char k[256], v[64];
    snprintf(k, sizeof k, "PhiMeMod.course.%s.%s", id, lv);
    if (prefGetString(k, v, sizeof v) <= 0) return;
    if (g_kvN == g_kvCap) {
        int c = g_kvCap ? g_kvCap * 2 : 64;
        KV* q = (KV*)malloc(sizeof(KV) * c);
        if (!q) return;
        if (g_kv) {
            memcpy(q, g_kv, sizeof(KV) * g_kvN);
            free(g_kv);
        }
        g_kv = q;
        g_kvCap = c;
    }
    snprintf(g_kv[g_kvN].k, sizeof g_kv[g_kvN].k, "%s", k);
    snprintf(g_kv[g_kvN].v, sizeof g_kv[g_kvN].v, "%s", v);
    g_kvN++;
}
static int g_guideSeen = 0;  // PhiMeMod.loginGuide, kept across DeleteAll like the rest
static void modPrefsSnapshot() {
    configLoad();
    g_guideSeen = prefGetInt("PhiMeMod.loginGuide", 0);
    g_kvN = 0;
    forEachChart(snapOne, nullptr);
}
static void modPrefsRestore() {
    configSave();
    for (int i = 0; i < g_kvN; i++) prefSetStringRaw(g_kv[i].k, g_kv[i].v);
    prefSetInt("PhiMeMod.welcomed", 1);
    if (g_guideSeen) prefSetInt("PhiMeMod.loginGuide", 1);
    prefSave();
    LOG("savedata: PhiMe settings and %d course records put back", g_kvN);
}

// ======================= official save =======================
// the current save -> saves/<prefix>-<time>.zip
static bool exportOfficial(char* out, int cap, const char* prefix) {
    char dir[400];
    if (!S.ok || !tmpDir(dir, sizeof dir, "PhiMeExport")) return false;
    void* a[] = {str(dir)};
    if (!invokeSafe(S.saveToFolder, nullptr, a)) return false;
    char saves[400], st[40];
    folder(saves, sizeof saves, "saves");
    stamp(st, sizeof st);
    snprintf(out, cap, "%s/%s-%s.zip", saves, prefix, st);
    return zipFolder(dir, out);
}

// the game's LoadFromFolder, with PhiMe's own keys kept
static char g_loadDir[400];  // the folder our own import is loading (its phime.json is merged), "" = the game's

// ======================= PhiMe data inside the cloud save =======================
// The game's sync writes its 5 module files into <temporaryCachePath>/save/ and uploads a zip of that whole folder
// (ZipFile.CreateFromDirectory); a download is extracted into a freshly emptied /save/ and LoadFromFolder reads only
// the 5 module names. So a phime.json put into /save/ while the game writes it travels with the cloud save, and
// the unmodified game simply ignores it (its next upload leaves it out; this device keeps its own copy anyway).
// Writing: GameKeySaveModule.Serialize (the last module SaveToFolder serialises, through an interface call, so the
// vtable is patched). Reading: the game's LoadFromFolder wipes PlayerPrefs first (result.cpp's DeleteAll hook calls
// saveWipe), while the extracted folder is still there.
typedef Obj* (*SerFn)(Obj*, const MethodInfo*);
static SerFn orig_keySerialize;
static bool cloudDir(char* out, int cap) {
    bool ok;
    Il2CppString* t = S.tmpPath ? (Il2CppString*)invokeObj(S.tmpPath, nullptr, nullptr, &ok) : nullptr;
    char b[320];
    if (!t || utf8(t, b, sizeof b) <= 0) return false;
    snprintf(out, cap, "%s/save/", b);
    return true;
}
static Obj* hk_keySerialize(Obj* self, const MethodInfo* m) {
    Obj* r = orig_keySerialize(self, m);
    char dir[400], path[480];
    // only while the game is filling /save/ for an upload (our own exports use other folders)
    if (cloudDir(dir, sizeof dir) && fileExists(dir)) {
        snprintf(path, sizeof path, "%sphime.json", dir);
        int rec = 0, course = 0;
        bool w = detailsExportFile(path, &rec, &course);
        LOG("savedata: phime.json for the cloud save, %d details, %d course records%s", rec, course,
            w ? "" : " (NOT written)");
    }
    return r;
}
// PlayerPrefs.DeleteAll outside the save sandbox (the game loading a cloud save, or our own import): PhiMe's own
// keys (settings, course records) are kept, and the loaded save's phime.json, if any, is merged in.
void saveWipe(bool after) {
    if (!after) {
        modPrefsSnapshot();
        return;
    }
    modPrefsRestore();
    char dir[400], path[480];
    if (g_loadDir[0]) snprintf(dir, sizeof dir, "%s", g_loadDir);
    else if (!cloudDir(dir, sizeof dir)) return;
    snprintf(path, sizeof path, "%sphime.json", dir);
    if (!fileExists(path)) return;
    int rec = 0, course = 0;
    bool ok = detailsImportFile(path, &rec, &course);
    LOG("savedata: cloud save's phime.json merged: %s (%d details, %d course records)", ok ? "ok" : "FAILED", rec,
        course);
}

static bool applyFolder(const char* dir, bool loadSettings) {
    modPrefsSnapshot();
    snprintf(g_loadDir, sizeof g_loadDir, "%s", dir);
    int version = 3;  // >= 2: Decrypt2, what SaveToFolder / the cloud write
    void* a[] = {str(dir), &loadSettings, &version};
    bool ok = invokeSafe(S.loadFromFolder, nullptr, a);
    g_loadDir[0] = 0;
    modPrefsRestore();
    LOG("savedata: LoadFromFolder(%s, settings %d) %s", dir, loadSettings, ok ? "done" : "FAILED");
    return ok;
}

static bool hasModuleFiles(const char* dir) {
    const char* const* names = moduleNames();
    int n = 0;
    for (int i = 0; i < 5; i++) {
        char p[512];
        snprintf(p, sizeof p, "%s%s", dir, names[i]);
        if (names[i][0] && fileExists(p)) n++;
    }
    return n >= 3;  // at least progress, record and key (settings / user may be missing in old saves)
}

static bool extractTo(const char* zip, char* dir, int cap, const char* name) {
    if (!tmpDir(dir, cap, name)) return false;
    void* a[] = {str(zip), str(dir)};
    return invokeSafe(S.extract, nullptr, a) && hasModuleFiles(dir);
}

static int g_restartFrame = -1;
static void scheduleRestart() { g_restartFrame = frameCount() + 2; }

static void dialog(const char* title, const char* body, const char* cancel, void (*onCancel)(), const char* ok,
                   void (*onOk)(), bool modal = false);
static void dialogClose();
static void savedataFrame(void*);
static void showError(const char* what, const char* detail) {
    char b[600];
    snprintf(b, sizeof b, T("%s\n\n详细原因在 PhiMeMod.log 里，可以把它发给开发者看看。", "%s\n\nThe details are in PhiMeMod.log; you can send it to the developer."), detail);
    dialog(what, b, nullptr, nullptr, T("好", "OK"), nullptr);
}

// something went wrong after the save was already replaced: put the backup back
static void rollback(const char* backupZip) {
    char dir[400];
    if (extractTo(backupZip, dir, sizeof dir, "PhiMeRollback") && applyFolder(dir, true)) {
        LOG("savedata: rolled back to %s", backupZip);
        scheduleRestart();
    }
}

static char g_pending[512];  // the file the confirm dialog is about

static void doImportOfficial() {
    char bak[512], dir[400];
    if (!exportOfficial(bak, sizeof bak, "Phigros-backup")) {
        showError(T("导入取消了", "Import cancelled"), T("当前存档没能备份，为了安全没有继续导入。", "The current save could not be backed up, so nothing was imported."));
        return;
    }
    if (!extractTo(g_pending, dir, sizeof dir, "PhiMeImport")) {
        showError(T("导入失败", "Import failed"), T("这个文件不是官方存档：解压后没有 gameKey / gameProgress / gameRecord 这些文件。当前存档没有改动。", "This is not an official save: no gameKey / gameProgress / gameRecord inside. Your save is unchanged."));
        return;
    }
    if (!applyFolder(dir, true)) {
        rollback(bak);
        showError(T("导入失败", "Import failed"), T("游戏读取这个存档时出错了，已经换回原来的存档。", "The game could not read this save; your old save has been put back."));
        return;
    }
    LOG("savedata: imported %s (backup %s), restarting", g_pending, bak);
    dialog(T("导入完成", "Imported"), T("正在重新启动游戏…", "Restarting the game…"), nullptr, nullptr, nullptr, nullptr, true);
    scheduleRestart();
}

// ======================= the cloud save (cloud.cpp downloads it) =======================
bool saveTempFile(char* out, int cap, const char* dirName, const char* file) {
    char dir[400];
    if (!S.ok || !tmpDir(dir, sizeof dir, dirName)) return false;
    snprintf(out, cap, "%s%s", dir, file);
    return true;
}

static void doImportCloud() {
    // a copy of what came from the cloud, next to the backups
    Bytes b;
    if (readFile(g_pending, b)) {
        char saves[400], st[40], copy[512];
        folder(saves, sizeof saves, "saves");
        stamp(st, sizeof st);
        snprintf(copy, sizeof copy, "%s/Phigros-cloud-%s.zip", saves, st);
        writeFile(copy, b.p, b.n);
        b.release();
    }
    doImportOfficial();
}

void saveImportCloud(const char* zip, const char* info, void (*onLogout)()) {
    onEveryFrame(savedataFrame);
    char dir[400];
    if (!extractTo(zip, dir, sizeof dir, "PhiMeCloudCheck")) {
        showError(T("云存档读不出来", "Cloud save unreadable"), T("下载到的文件解压后没有 gameKey / gameProgress / gameRecord 这些文件。当前存档没有改动。", "The download has no gameKey / gameProgress / gameRecord inside. Your save is unchanged."));
        return;
    }
    snprintf(g_pending, sizeof g_pending, "%s", zip);
    char b[900];
    snprintf(b, sizeof b, T("%s\n\n将用这份云存档替换当前的全部存档：成绩、解锁、剧情进度和设置。当前存档会先自动备份到 saves；导入后游戏会重新启动。", "%s\n\nThis cloud save replaces your whole save: scores, unlocks, story and settings. The current save is backed up to saves first; the game restarts afterwards."),
             info ? info : "");
    SaveDialog d = {};
    d.title = T("导入云存档？", "Import the cloud save?");
    d.body = b;
    d.cancel = T("取消", "Cancel");
    d.ok = T("导入", "Import");
    d.onOk = doImportCloud;
    d.extra = onLogout ? T("退出登录", "Log out") : nullptr;
    d.onExtra = onLogout;
    saveDialog(d);
}

void saveMessage(const char* title, const char* body) { dialog(title, body, nullptr, nullptr, T("好", "OK"), nullptr); }

// ======================= the all-unlocked empty save =======================
static int unlockAllKeys() {
    Obj* gi = gameInfo();
    void** keys;
    int n = gi ? seq(fld<Obj*>(gi, S.giKeyStore), &keys) : 0, raised = 0, calls = 0;
    for (int i = 0; i < n; i++) {
        Obj* k = (Obj*)keys[i];
        if (!k) continue;
        Il2CppString* name = fld<Il2CppString*>(k, S.keyName);
        int kind = fld<int>(k, S.keyKind);
        int have = fld<int>(k, S.keyUnlocked), need = fld<int>(k, S.keyTimes);
        if (!name || have >= need) continue;
        for (int t = have; t < need && t - have < 64; t++) {
            void* a[] = {name, &kind};
            if (!invokeSafe(S.getKey, gi, a)) break;
            calls++;
        }
        raised++;
    }
    LOG("savedata: %d of %d keys taken to their limit (%d GetKey calls)", raised, n, calls);
    return n;
}

static FieldInfo* modField(int i, const char* name, int type) {
    FieldInfo* f = S.mod[i] ? il2cpp_class_get_field_from_name(S.mod[i], name) : nullptr;
    if (f && il2cpp_type_get_type(il2cpp_field_get_type(f)) != type) {
        LOG("savedata: %s.%s has another type now, left alone", kModClass[i], name);
        return nullptr;
    }
    return f;
}
static int off(FieldInfo* f) { return (int)il2cpp_field_get_offset(f); }

// every story / chapter / event step done, every tip already seen; Data and the challenge-mode rank as they are
// except the rank (a result) cleared
// Checked against a real fully played save (4.0.1): the values below are what it has.
static void tweakProgress(Obj* m) {
    // isFirstRun is named backwards: SplashScene.NextScene starts the tutorial while SaveManagement int
    // "IsFirstRun" is 0, and the tutorial's result screen sets it to 1. So true = tutorial done.
    static const char* const done[] = {"isFirstRun",          "legacyChapterFinished",     "alreadyShowCollectionTip",
                                       "alreadyShowAutoUnlockINTip", "chapter8UnlockBegin", "chapter8UnlockSecondPhase",
                                       "chapter8Passed",      "chapter9UnlockBegin",       "chapter9Phase2Begin",
                                       "chapter9Phase2Passed", "c9BaselineChallengeReached"};
    for (const char* n : done)
        if (FieldInfo* f = modField(M_PROGRESS, n, IL2CPP_TYPE_BOOLEAN)) fld<bool>(m, off(f)) = true;
    // no half-done secret challenge
    if (FieldInfo* f = modField(M_PROGRESS, "chapter9SecretChallengePendingLifeUnlock", IL2CPP_TYPE_BOOLEAN))
        fld<bool>(m, off(f)) = false;
    // chapter 9: the second phase walked to its end, the secret challenge's lives earned and the password known
    static const struct {
        const char* name;
        uint8_t v;
    } bytesDone[] = {{"chapter9Phase2Step", 4}, {"chapter9SecretChallengeLifeTier", 3},
                     {"chapter9SecretChallengeSelectedLifeTier", 3}};
    for (auto& b : bytesDone)
        if (FieldInfo* f = modField(M_PROGRESS, b.name, 0x05 /* U1 */)) fld<uint8_t>(m, off(f)) = b.v;
    if (FieldInfo* f = modField(M_PROGRESS, "chapter9SecretPassword", IL2CPP_TYPE_STRING)) {
        Il2CppString* s = fld<Il2CppString*>(m, off(f));
        if (!s || il2cpp_string_length(s) == 0)
            fld<Il2CppString*>(m, off(f)) =
                str("Backward, go backward, turn back to the antemundane realm, go back to the -");
    }
    // per-song unlock flags (bool[] in memory)
    static const char* const lists[] = {"randomVersionUnlocked", "chapter8SongUnlocked", "chapter9SongUnlocked",
                                        "chapter9Phase2SongUnlocked"};
    int songs = 0;
    for (const char* n : lists)
        if (FieldInfo* f = modField(M_PROGRESS, n, IL2CPP_TYPE_SZARRAY)) {
            Il2CppArray* a = fld<Il2CppArray*>(m, off(f));
            if (!a || strcmp(il2cpp_class_get_name(il2cpp_object_get_class((Obj*)a)), "Boolean[]") != 0) continue;
            bool* b = (bool*)arrayData(a);
            for (uintptr_t i = 0; i < a->max_length; i++) b[i] = true;
            songs += (int)a->max_length;
        }
    // event flags: one bit per step (Apply turns each bit into its own PlayerPrefs bool). The three story events
    // get exactly the steps a finished save has (not every bit: an extra one could replay part of an event);
    // the song-record collection flags all bits.
    static const struct {
        const char* name;
        uint8_t v;
    } bits[] = {{"unlockFlagOfSpasmodic", 0x03}, {"unlockFlagOfIgallta", 0x0F}, {"unlockFlagOfRrharil", 0x0E},
                {"flagOfSongRecordKey", 0xFF},   {"flagOfSongRecordKeyTakumi", 0xFF}};
    for (auto& b : bits)
        if (FieldInfo* f = modField(M_PROGRESS, b.name, 0x05 /* U1 */)) {
            uint8_t& v = fld<uint8_t>(m, off(f));
            v = (uint8_t)(v | b.v);  // keep any step the current save already has
        }
    // the game is "completed" when this string is not empty
    if (FieldInfo* f = modField(M_PROGRESS, "completed", IL2CPP_TYPE_STRING)) {
        Il2CppString* s = fld<Il2CppString*>(m, off(f));
        if (!s || il2cpp_string_length(s) == 0) fld<Il2CppString*>(m, off(f)) = str("4.0.1");
    }
    // challenge-mode rank is a result: none yet
    FieldInfo* r = S.mod[M_PROGRESS] ? il2cpp_class_get_field_from_name(S.mod[M_PROGRESS], "challengeModeRank") : nullptr;
    int rt = r ? il2cpp_type_get_type(il2cpp_field_get_type(r)) : -1;
    if (rt == 0x06 || rt == 0x07) fld<short>(m, off(r)) = 0;
    else if (rt == IL2CPP_TYPE_I4) fld<int>(m, off(r)) = 0;
    LOG("savedata: progress flags set (%d per-song flags)", songs);
}

// gameKey: every collected item already read (no "new" dots), like a played-through save. Per key the module keeps
// a List<byte> laid out like the file: [flags, one count per set flag], flag bits 0 read, 1 single, 2 collection,
// 3 illustration, 4 avatar. (0.16.1 took it for five fixed slots and wrote a count over the flags byte of keys
// that were already partly read: LoadFromFolder then ran past the list, ArgumentOutOfRangeException.)
// Plus the one-off flags a finished save has: the Lanota story read, the 3.9.0 score reset already done.
static int popcount8(unsigned v) {
    int n = 0;
    for (; v; v &= v - 1) n++;
    return n;
}
static void tweakKeys(Obj* m) {
    FieldInfo* fk = S.mod[M_KEY] ? il2cpp_class_get_field_from_name(S.mod[M_KEY], "keys") : nullptr;
    Obj* dict = fk ? fld<Obj*>(m, off(fk)) : nullptr;
    const MethodInfo* tryGet = dict ? findMethod(il2cpp_object_get_class(dict), "TryGetValue", 2) : nullptr;
    Obj* gi = gameInfo();
    void** keys;
    int n = gi && tryGet ? seq(fld<Obj*>(gi, S.giKeyStore), &keys) : 0, read = 0;
    for (int i = 0; i < n; i++) {
        Obj* k = (Obj*)keys[i];
        Il2CppString* name = k ? fld<Il2CppString*>(k, S.keyName) : nullptr;
        if (!name) continue;
        Obj* list = nullptr;
        void* a[] = {name, &list};
        bool ok;
        invokeObj(tryGet, dict, a, &ok);
        if (!ok || !list) continue;
        Il2CppClass* lk = il2cpp_object_get_class(list);
        FieldInfo* fi = il2cpp_class_get_field_from_name(lk, "_items");
        FieldInfo* fs = il2cpp_class_get_field_from_name(lk, "_size");
        if (!fi || !fs) continue;
        Il2CppArray* arr = fld<Il2CppArray*>(list, off(fi));
        int size = fld<int>(list, off(fs));
        if (!arr || size < 1 || (int)arr->max_length < size) continue;
        uint8_t* b = (uint8_t*)arrayData(arr);
        unsigned flags = b[0];
        if (flags > 0x1F || size != 1 + popcount8(flags)) {  // not the layout we know: leave this key alone
            LOG("savedata: key list with flags %02x and %d bytes left as it is", flags, size);
            continue;
        }
        if (!(flags & 4)) continue;  // no collection item
        uint8_t coll = b[1 + popcount8(flags & 3)];
        if (flags & 1) {  // read already counted: up to the collected count
            if (b[1] < coll) {
                b[1] = coll;
                read++;
            }
            continue;
        }
        // no read count yet: insert it right after the flags, then set the flag
        static const MethodInfo* insert = nullptr;
        if (!insert) insert = findMethod(lk, "Insert", 2);
        int at = 1;
        void* ia[] = {&at, &coll};
        if (!insert || !invokeSafe(insert, list, ia)) continue;
        arr = fld<Il2CppArray*>(list, off(fi));  // the insert may have grown the array
        ((uint8_t*)arrayData(arr))[0] = (uint8_t)(flags | 1);
        read++;
    }
    static const struct {
        const char* name;
        uint8_t v;
    } flags[] = {{"lanotaReadKeys", 0x1E}, {"oldScoreClearedV390", 0x01}};
    for (auto& f : flags)
        if (FieldInfo* fi = modField(M_KEY, f.name, 0x05 /* U1 */)) {
            uint8_t& v = fld<uint8_t>(m, off(fi));
            v = (uint8_t)(v | f.v);
        }
    LOG("savedata: %d collection items marked read (dictionary %s)", read, tryGet ? "ok" : "MISSING");
}

static bool emptyRecords(Obj* m) {
    if (S.recData < 0) return false;
    Obj* d = fld<Obj*>(m, S.recData);
    if (!d) {
        invokeSafe(S.modLocal[M_RECORD], m);
        d = fld<Obj*>(m, S.recData);
    }
    if (!d) return false;
    const MethodInfo* clr = findMethod(il2cpp_object_get_class(d), "Clear", 0);
    return clr && invokeSafe(clr, d);
}

static bool writeModule(int i, Obj* m, const char* dir) {
    bool ok;
    Obj* data = invokeObj(S.modSerialize[i], m, nullptr, &ok);
    if (!ok || !data) return false;
    void* a[] = {data};
    Il2CppArray* enc = (Il2CppArray*)invokeObj(S.encrypt2, nullptr, a, &ok);
    if (!ok || !enc) return false;
    uint8_t ver = callm<uint8_t>(S.modVersion[i], m);
    const char* const* names = moduleNames();
    char p[512];
    snprintf(p, sizeof p, "%s%s", dir, names[i]);
    Bytes b;
    b.add(&ver, 1);
    b.add(arrayData(enc), (int)enc->max_length);
    bool w = writeFile(p, b.p, b.n);
    LOG("savedata: %s v%d, %d bytes%s", names[i], ver, b.n, w ? "" : " (NOT written)");
    b.release();
    return w;
}

static bool buildUnlocked(const char* dir) {
    unlockAllKeys();  // into the live save first: gameKey's LoadFromLocal reads them back
    for (int i = 0; i < 5; i++) {
        Obj* m = newModule(i);
        if (!m) return false;
        if (i == M_RECORD) {
            if (!emptyRecords(m)) {
                LOG("savedata: could not empty the records");
                return false;
            }
        } else if (!invokeSafe(S.modLocal[i], m)) {
            return false;
        }
        if (i == M_PROGRESS) tweakProgress(m);
        if (i == M_KEY) tweakKeys(m);
        if (!writeModule(i, m, dir)) return false;
    }
    return true;
}

static void doUnlocked() {
    if (!S.unlockOk) {
        showError(T("没法生成全解锁存档", "Cannot make the all-unlocked save"), T("这个游戏版本里找不到需要的存档模块。", "This game version lacks the save modules it needs."));
        return;
    }
    char bak[512], dir[400], copy[512];
    if (!exportOfficial(bak, sizeof bak, "Phigros-backup")) {
        showError(T("导入取消了", "Import cancelled"), T("当前存档没能备份，为了安全没有继续。", "The current save could not be backed up, so nothing was changed."));
        return;
    }
    if (!tmpDir(dir, sizeof dir, "PhiMeUnlocked") || !buildUnlocked(dir)) {
        rollback(bak);  // the keys were already raised in the live save
        showError(T("生成失败", "Failed"), T("全解锁存档没能生成，已经换回原来的存档。", "The all-unlocked save could not be made; your old save has been put back."));
        return;
    }
    char saves[400];
    folder(saves, sizeof saves, "saves");
    snprintf(copy, sizeof copy, "%s/Phigros-AllUnlocked.zip", saves);
    zipFolder(dir, copy);  // a copy to keep / share
    // what the save itself can't open (score-gated chapters, songs and IN / AT) is opened by the runtime switch;
    // saved before applyFolder, whose PhiMe-settings snapshot carries it over the wipe
    g_cfg.unlockAll = true;
    configSave();
    if (!applyFolder(dir, false)) {
        rollback(bak);
        showError(T("导入失败", "Import failed"), T("游戏读取全解锁存档时出错了，已经换回原来的存档。", "The game could not read the all-unlocked save; your old save has been put back."));
        return;
    }
    LOG("savedata: all-unlocked empty save imported (backup %s), restarting", bak);
    dialog(T("全解锁存档已导入", "All-unlocked save imported"), T("正在重新启动游戏…", "Restarting the game…"), nullptr, nullptr, nullptr, nullptr, true);
    scheduleRestart();
}

// ======================= PhiMe save =======================
static void doImportPhiMe() {
    int rec = 0, course = 0;
    bool ok = detailsImportFile(g_pending, &rec, &course);
    char b[400];
    if (!ok) {
        showError(T("导入失败", "Import failed"), T("这个文件不是 PhiMe 存档（读不出判定详情和课题成绩）。", "This is not a PhiMe save (no judgement details or course records in it)."));
        return;
    }
    snprintf(b, sizeof b, T("判定详情 %d 条、课题模式成绩 %d 条已经合并进来：每个难度保留更好的那一局，本机原有的记录不会丢。", "%d judgement details and %d course records merged in: each chart keeps the better play, nothing on this device is lost."),
             rec, course);
    dialog(T("PhiMe 存档已导入", "PhiMe save imported"), b, nullptr, nullptr, T("好", "OK"), nullptr);
}

// ======================= the panel's buttons =======================
static void confirmImport(bool official, const char* shown) {
    char b[900];
    if (official)
        snprintf(b, sizeof b, T("将用 %s 替换当前的全部存档：成绩、解锁、剧情进度和设置。\n\n当前存档会先自动备份到 saves 文件夹；导入后游戏会重新启动。PhiMe 的设置和记录不受影响。", "%s replaces your whole save: scores, unlocks, story and settings.\n\nThe current save is backed up to saves first; the game restarts afterwards. PhiMe's own settings and records stay."),
                 shown);
    else
        snprintf(b, sizeof b, T("把 %s 里的判定详情和课题模式成绩合并进来。\n\n同一个难度保留更好的那一局，本机原有的记录不会被删掉。", "Merges the judgement details and course records from %s.\n\nEach chart keeps the better play; nothing on this device is deleted."),
                 shown);
    dialog(official ? T("导入官方存档？", "Import the official save?") : T("导入 PhiMe 存档？", "Import the PhiMe save?"), b, T("取消", "Cancel"), nullptr, T("导入", "Import"),
           official ? doImportOfficial : doImportPhiMe);
}

// ---- through the system file picker ----
static bool g_pickOfficial = false;
static char g_exported[512], g_exportTitle[64], g_exportInfo[200];

static void onPicked(int status, const char* path, const char* name) {
    if (status == 0) return;  // cancelled
    if (status < 0 || !path) {
        char b[400];
        snprintf(b, sizeof b, T("选中的文件没能读出来。\n%s", "The chosen file could not be read.\n%s"), name ? name : "");
        showError(T("没能读取文件", "Could not read the file"), b);
        return;
    }
    snprintf(g_pending, sizeof g_pending, "%s", path);
    confirmImport(g_pickOfficial, name ? name : fileName(path));
}

static void onExported(int status, const char* path, const char* name) {
    char b[900];
    if (status > 0)
        snprintf(b, sizeof b, T("已保存为 %s。\n%s\n\n另外 %s/saves 里也留了一份。", "Saved as %s.\n%s\n\nA copy also stays in %s/saves."), name ? name : fileName(g_exported),
                 g_exportInfo, saveFolderHint());
    else if (status == 0)
        snprintf(b, sizeof b, T("没有另存到别处，文件在这里：\n%s/saves/%s\n%s", "Not saved elsewhere; the file is here:\n%s/saves/%s\n%s"), saveFolderHint(), fileName(g_exported),
                 g_exportInfo);
    else
        snprintf(b, sizeof b, T("另存没有成功（%s）。\n文件仍在 %s/saves/%s。", "Saving a copy failed (%s).\nThe file is still in %s/saves/%s."), name ? name : T("未知原因", "unknown reason"), saveFolderHint(),
                 fileName(g_exported));
    dialog(status < 0 ? T("另存失败", "Save failed") : g_exportTitle, b, nullptr, nullptr, T("好", "OK"), nullptr);
}

// exported to saves/: let the user put a copy anywhere (system picker), or just say where it is
static void exported(const char* path, int type, const char* title, const char* info) {
    snprintf(g_exported, sizeof g_exported, "%s", path);
    snprintf(g_exportTitle, sizeof g_exportTitle, "%s", title);
    snprintf(g_exportInfo, sizeof g_exportInfo, "%s", info);
    if (pickerAvailable() && pickerSave(path, type, onExported)) return;
    char b[900];
    snprintf(b, sizeof b, T("%s\n\n位置：%s/saves\n%s", "%s\n\nIn: %s/saves\n%s"), fileName(path), saveFolderHint(), info);
    dialog(title, b, nullptr, nullptr, T("好", "OK"), nullptr);
}

void saveAction(int action) {
    onEveryFrame(savedataFrame);  // picker results arrive through it
    if (action == SAVE_MASK_PREVIEW) {
        maskPreviewArm();
        if (maskPreviewStart()) {  // straight into Rrhar'il AT
            panelCloseNow();
            return;
        }
        saveMessage(T("上隐条预览", "Hide bar preview"), T("请在选歌界面选择 Rrhar'il 的 AT 难度并开始游戏。\n\n进入后会从 1:22 开始自动播放并循环；拖动遮罩下边的把手调整高度，松手吸附到整数 %；按底部「确认」保存，再在暂停菜单里点返回离开。", "Pick Rrhar'il AT in song select and start it.\n\nIt plays itself from 1:22, looping. Drag the handle under the band to set its height (it snaps to whole %); tap Done to save, then leave from the pause menu."));
        return;
    }
    if (action == SAVE_SKIN_IMPORT) {
        skinImport();
        return;
    }
    if (action == SAVE_OPEN_FOLDER) {
        // Android: the PhiMeMod folder (log, saves, libPhiMeMod.so updates); iOS: Documents (log + PhiMeMod/)
#if defined(__APPLE__)
        const char* home = getenv("HOME");
        char docs[400];
        snprintf(docs, sizeof docs, "%s/Documents", home ? home : ".");
        const char* dir = docs;
        const char* how = T("在「文件」App 里找：我的 iPad / iPhone → Phigros。\n\n如果那里没有 Phigros，是安装包没开文件共享（UIFileSharingEnabled），用 PhiMe 打的 ipa 已经开好了；签名工具里一般也有「文件共享」选项。", "In the Files app: On My iPad / iPhone → Phigros.\n\nIf Phigros is missing there, the IPA was signed without file sharing (UIFileSharingEnabled); PhiMe's packaged IPA has it on, and signing tools usually have a file sharing option.");
#else
        baseDir();
        const char* dir = g_base;
        const char* how = T("用文件管理器（比如 MT 管理器）打开这个文件夹：", "Open this folder in a file manager (e.g. MT Manager):");
#endif
        if (openFolder(dir)) return;
        char b[900];
#if defined(__APPLE__)
        snprintf(b, sizeof b, "%s", how);
#else
        snprintf(b, sizeof b, "%s\n%s\n\n%s", how, saveFolderHint(), T("日志 PhiMeMod.log、导出的存档都在这里；新的 libPhiMeMod.so 放进来，重启游戏就会加载。", "PhiMeMod.log and exported saves are here; drop a new libPhiMeMod.so in and restart the game to load it."));
#endif
        dialog(T("Mod 文件夹", "Mod folder"), b, nullptr, nullptr, T("好", "OK"), nullptr);
        return;
    }
    if (!S.ok && action != SAVE_EXPORT_PHIME && action != SAVE_IMPORT_PHIME) {
        showError(T("存档功能不可用", "Saves unavailable"), T("这个游戏版本里找不到云存档用的读写方法。", "This game version lacks the cloud save read / write methods."));
        return;
    }
    char path[512], b[900], imp[400];
    switch (action) {
    case SAVE_EXPORT_OFFICIAL:
        if (!exportOfficial(path, sizeof path, "Phigros")) {
            showError(T("导出失败", "Export failed"), T("官方存档没能导出。", "The official save could not be exported."));
            return;
        }
        exported(path, PICK_ZIP, T("官方存档已导出", "Official save exported"), T("格式和云存档一样（gameKey / gameProgress / gameRecord / settings / user）。", "Same format as the cloud save (gameKey / gameProgress / gameRecord / settings / user)."));
        return;
    case SAVE_IMPORT_OFFICIAL:
    case SAVE_IMPORT_PHIME: {
        bool official = action == SAVE_IMPORT_OFFICIAL;
        if (pickerAvailable()) {
            char dir[400], copy[512];
            copy[0] = 0;
            if (tmpDir(dir, sizeof dir, "PhiMePick")) snprintf(copy, sizeof copy, "%spicked%s", dir, official ? ".zip" : ".json");
            g_pickOfficial = official;
            if (copy[0] && pickerOpen(official ? PICK_ZIP : PICK_JSON, copy, onPicked)) return;
            LOG("savedata: system picker did not start, using the import folder");
        }
        folder(imp, sizeof imp, "import");
        if (!newestFile(imp, official ? ".zip" : ".json", g_pending, sizeof g_pending)) {
            snprintf(b, sizeof b, T("先把%s放进这个文件夹，再点导入：\n%s/import\n\n导出的文件在旁边的 saves 文件夹里，可以直接复制过去。", "Put %s into this folder, then tap Import:\n%s/import\n\nExports are in the saves folder next to it; you can copy them over."),
                     official ? T("官方存档（.zip）", "an official save (.zip)") : T("PhiMe 存档（.json）", "a PhiMe save (.json)"), saveFolderHint());
            dialog(T("没找到存档文件", "No save file found"), b, nullptr, nullptr, T("好", "OK"), nullptr);
            return;
        }
        confirmImport(official, fileName(g_pending));
        return;
    }
    case SAVE_EXPORT_PHIME: {
        char saves[400], st[40];
        folder(saves, sizeof saves, "saves");
        stamp(st, sizeof st);
        snprintf(path, sizeof path, "%s/PhiMe-%s.json", saves, st);
        int rec = 0, course = 0;
        if (!detailsExportFile(path, &rec, &course)) {
            showError(T("导出失败", "Export failed"), T("PhiMe 存档没能写入。", "The PhiMe save could not be written."));
            return;
        }
        snprintf(b, sizeof b, T("判定详情 %d 条、课题模式成绩 %d 条。", "%d judgement details, %d course records."), rec, course);
        exported(path, PICK_JSON, T("PhiMe 存档已导出", "PhiMe save exported"), b);
        return;
    }
    case SAVE_CLOUD:
    case SAVE_CLOUD_PASTE:
        cloudPull(action == SAVE_CLOUD_PASTE);
        return;
    case SAVE_UNLOCKED:
        dialog(T("导入全解锁空存档？", "Import the all-unlocked empty save?"),
               T("歌曲、剧情、收藏品、头像和曲绘全部打开，成绩清空，设置不变。主线章节、章节后面的歌和 IN/AT 难度游戏按成绩判断，所以会一起打开「解锁全部歌曲」开关（不写进存档）。\n\n当前存档会先自动备份到 saves 文件夹；导入后游戏会重新启动。", "Songs, story, collectibles, avatars and illustrations open; scores are cleared; settings stay. Main chapters, later chapter songs and IN / AT open by score, so the Unlock all songs switch turns on too (never saved).\n\nYour save is backed up to saves first; the game restarts afterwards."),
               T("取消", "Cancel"), nullptr, T("导入", "Import"), doUnlocked);
        return;
    }
}

// ======================= dialog =======================
static const int C_CLAY = 0xD97757, C_IVORY = 0xFAF9F5, C_LINE = 0xE8E6DC, C_TEXT = 0x141413, C_BODY = 0x3D3D3A;
static const float kDW = 760, kDH = 470, kDPad = 44, kDTop = kDH / 2;
static const Vec2 kDClawd{kDW / 2 - 110, kDTop + 2 + 34};
static Handle g_dRoot, g_dCard, g_dDim, g_dClawd, g_dBody;
static float g_dOpenT = -1, g_dClawdT = 0;
static void (*g_dOk)() = nullptr;
static void (*g_dCancel)() = nullptr;
static void (*g_dExtra)() = nullptr;
static bool g_dModal = false;
static void savedataFrame(void*);

static void dialogClose() {
    Obj* r = (Obj*)g_dRoot.get();
    uiFadeAway(r);  // fades out, then destroyed
    g_dRoot.clear();
    g_dCard.clear();
    g_dDim.clear();
    g_dClawd.clear();
    g_dBody.clear();
    g_dOpenT = -1;
}
static void onDlgButton(void* u) {
    intptr_t k = (intptr_t)u;
    void (*f)() = k == 1 ? g_dOk : k == 2 ? g_dExtra : g_dCancel;
    dialogClose();
    if (f) f();
}
static void onDlgDim(void*) {
    if (!g_dModal) onDlgButton(nullptr);
}


// right >= 0: from the right edge; right < 0: at the left edge (the extra button)
static Obj* button(Obj* card, float right, const char* label, bool primary, void* user) {
    float w = textWidth(label, 23, primary) + 56;
    if (w < 120) w = 120;
    bool left = right < 0;
    Obj* b = image(card, "button", SPR_ROUND, rgb(primary ? C_CLAY : C_LINE), Vec2{left ? 0.f : 1.f, 0},
                   Vec2{left ? kDPad + w / 2 : -right - w / 2, 30 + 28}, Vec2{w, 56}, 14, true);
    if (!primary)
        image(rt(b), "fill", SPR_ROUND, rgb(C_IVORY), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{w - 4, 52}, 12);
    text(rt(b), label, 23, rgb(primary ? 0xFFFFFF : C_TEXT), MiddleCenter, primary, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f},
         Vec2{0, 1}, Vec2{w, 50});
    onClick(gameObject(b), b, onDlgButton, user);
    return b;
}

// title, wrapped body, up to two buttons (`ok` is the primary one, on the right). No buttons = a status card.
static void dialog(const char* title, const char* body, const char* cancel, void (*onCancel)(), const char* ok,
                   void (*onOk)(), bool modal) {
    SaveDialog d = {};
    d.title = title;
    d.body = body;
    d.cancel = cancel;
    d.onCancel = onCancel;
    d.ok = ok;
    d.onOk = onOk;
    d.modal = modal;
    saveDialog(d);
}

bool saveDialogOpen() { return g_dRoot.h && alive((Obj*)g_dRoot.get()); }
void saveDialogClose() { dialogClose(); }
void saveDialogBody(const char* body) {
    Obj* t = (Obj*)g_dBody.get();
    if (saveDialogOpen() && alive(t)) setText(t, body);
}

void saveDialog(const SaveDialog& d) {
    const char *title = d.title, *body = d.body, *cancel = d.cancel, *ok = d.ok;
    dialogClose();
    g_dOk = d.onOk;
    g_dCancel = d.onCancel;
    g_dExtra = d.onExtra;
    g_dModal = d.modal || (!ok && !cancel);
    Obj* root = overlayCanvas("PhiMeDialog", 32100);  // above the settings panel
    g_dRoot.set(root);
    Obj* dim = image(root, "dim", SPR_SOLID, Color{0.08f, 0.07f, 0.06f, 0.f}, Vec2{0.5f, 0.5f}, Vec2{0, 0},
                     Vec2{100, 100}, 0, true);
    stretch(rt(dim), -20);
    onClick(gameObject(dim), nullptr, onDlgDim, nullptr);
    g_dDim.set(dim);
    Obj* card = newUI("card", root);
    place(card, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, -16}, Vec2{kDW, kDH});
    g_dCard.set(card);
    image(card, "border", SPR_ROUND, rgb(C_LINE), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{kDW + 4, kDH + 4}, 26);
    image(card, "bg", SPR_ROUND, rgb(C_IVORY), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{kDW, kDH}, 24, true);
    g_dClawd.set(image(card, "clawd", SPR_CLAWD, rgb(0xFFFFFF), Vec2{0.5f, 0.5f}, kDClawd, Vec2{120, 68}));
    g_dClawdT = 0;
    text(card, title, 30, rgb(C_TEXT), MiddleLeft, true, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{kDPad, kDTop - 52},
         Vec2{kDW - 2 * kDPad - 120, 44});
    image(card, "divider", SPR_SOLID, rgb(C_LINE), Vec2{0.5f, 0.5f}, Vec2{0, kDTop - 92}, Vec2{kDW - 2 * kDPad, 2});
    float bw = d.bodyW > 0 ? d.bodyW : kDW - 2 * kDPad;
    Obj* bt = text(card, body, 22, rgb(C_BODY), UpperLeft, false, Vec2{0, 0.5f}, Vec2{0, 1}, Vec2{kDPad, kDTop - 116},
                   Vec2{bw, 230});
    setTextWrap(bt, true);
    g_dBody.set(bt);
    float right = kDPad;
    if (ok) {
        Obj* b = button(card, right, ok, true, (void*)1);
        Rect r{0, 0, 120, 56};
        rectOf(rt(b), &r);
        right += r.w + 16;
    }
    if (cancel) button(card, right, cancel, false, nullptr);
    if (d.extra) button(card, -1, d.extra, false, (void*)2);
    if (d.decorate) d.decorate(card);
    setScale(card, 0.96f);
    g_dOpenT = 0;
    onEveryFrame(savedataFrame);  // animation, and the restart after an import
}

static void dialogTick() {
    if (!g_dRoot.h) return;
    Obj* root = (Obj*)g_dRoot.get();
    if (!alive(root)) {
        dialogClose();
        return;
    }
    float dt = unscaledDt();
    if (g_dOpenT >= 0) {
        g_dOpenT += dt / 0.18f;
        float t = g_dOpenT >= 1 ? 1 : g_dOpenT, u = 1 - t, e = 1 - u * u * u;
        Obj* card = (Obj*)g_dCard.get();
        if (alive(card)) setScale(card, 0.96f + 0.04f * e);
        Obj* dim = (Obj*)g_dDim.get();
        if (alive(dim)) setColor(dim, Color{0.08f, 0.07f, 0.06f, 0.45f * e});
        if (g_dOpenT >= 1) g_dOpenT = -1;
    }
    Obj* c = (Obj*)g_dClawd.get();
    if (alive(c)) {
        g_dClawdT += dt;
        setPos(rt(c), Vec2{kDClawd.x, kDClawd.y + clawdHop(g_dClawdT, 14)});
    }
}

// ======================= first-run welcome =======================
static float g_welcomeAt = -1;

static void welcomeDone() {
    prefSetInt("PhiMeMod.welcomed", 1);
    prefSave();
}
static void welcomeKeep() {
    welcomeDone();
    LOG("savedata: welcome -> keep the current save");
    g_welcomeAt = timeSinceLevelLoad() + 0.4f;  // then the login guide (Android)
}
static void welcomeUnlock() {
    welcomeDone();
    LOG("savedata: welcome -> all-unlocked empty save");
    doUnlocked();
}
static void showWelcome() {
    dialog(T("欢迎使用 PhiMe 小助手", "Welcome to PhiMe Assistant"),
           T("要从「全解锁空存档」开始吗？\n歌曲、剧情、收藏品、头像和曲绘全部打开，成绩清空，设置不变，也不会再进新手教程。"
             "主线章节、章节后面的歌和 IN/AT 难度游戏按成绩判断，所以会一起打开「解锁全部歌曲」开关（不写进存档）。\n\n"
             "当前存档会先自动备份；之后可在「PhiMe 小助手 → 存档」随时导入、导出。",
             "Start from the all-unlocked empty save?\nSongs, story, collectibles, avatars and illustrations open; scores are "
             "cleared; no tutorial. Main chapters, later chapter songs and IN / AT open by score, so the Unlock all songs "
             "switch turns on too (never saved).\n\nYour save is backed up first (PhiMe Assistant > Saves)."),
           T("保持当前存档", "Keep my save"), welcomeKeep, T("导入全解锁存档", "Use all-unlocked"), welcomeUnlock, true);
    LOG("savedata: first-run welcome shown");
}

// ======================= Android: how to log in (once, after the welcome) =======================
// The re-signed APK can't use the game's TapTap login; PhiMe's QR login hands the game's SDK a token instead, and
// from then on the game's own sync runs (account.cpp).
static void guideDone() {
    prefSetInt("PhiMeMod.loginGuide", 1);
    prefSave();
}
static void guideLater() {
    guideDone();
    LOG("savedata: login guide -> later");
}
static void guideLogin() {
    guideDone();
    LOG("savedata: login guide -> log in now");
    if (!accountLoginNow())
        saveMessage(T("没能开始登录", "Could not start the login"),
                    T("PhiMe 的 Java 部分没有加载（需要 Android 8 以上），或者游戏的登录组件没找到。日志里 account: 开头的行有原因。",
                      "PhiMe's Java side did not load (needs Android 8+), or the game's login was not found. See the log lines starting with account:."));
}
static bool wantGuide() {
#if defined(__APPLE__)
    return false;
#else
    return prefGetInt("PhiMeMod.loginGuide", 0) == 0 && cloudAvailable() && !accountLoggedIn();
#endif
}
static void showGuide() {
    dialog(T("登录 TapTap 账号", "Log in to TapTap"),
           T("这个安装包为了装 PhiMe 重新签过名，TapTap 登录会检查签名并拒绝（signature not match），所以游戏原本的登录用不了。\n\n"
             "PhiMe 改用 TapTap 扫码拿凭证，交给游戏自己的登录；之后同步、自动同步和冲突提醒都是游戏原本的逻辑。\n\n"
             "以后也能在账号页点「登录」，凭证只存在本机。",
             "PhiMe's APK is re-signed, and TapTap's login refuses a changed signature, so the game's own login fails.\n\n"
             "PhiMe logs in with a TapTap QR code and hands the token to the game; sync, auto sync and conflict "
             "warnings are then the game's own.\n\nLater: Log in on the account page. The token stays on this phone."),
           T("以后再说", "Later"), guideLater, T("现在登录", "Log in now"), guideLogin, true);
    LOG("savedata: login guide shown");
}

// ======================= per frame =======================
static void savedataFrame(void*) {
    dialogTick();
    pickerPoll();
    cloudPoll();
    if (g_welcomeAt >= 0 && timeSinceLevelLoad() >= g_welcomeAt) {
        g_welcomeAt = -1;
        if (prefGetInt("PhiMeMod.welcomed", 0) == 0) showWelcome();
        else if (!saveDialogOpen() && wantGuide()) showGuide();
    }
    if (g_restartFrame >= 0 && frameCount() >= g_restartFrame) {
        g_restartFrame = -1;
        // what the game does after loading a cloud save
        invokeSafe(S.restart, nullptr);
        int scene = 0;
        void* a[] = {&scene};
        invokeSafe(S.loadScene, nullptr, a);
        LOG("savedata: restarted");
    }
}

static void hk_CS_Start(Obj* self, const MethodInfo* m) {
    orig_CS_Start(self, m);
    onEveryFrame(savedataFrame);
    bool welcome = S.unlockOk && prefGetInt("PhiMeMod.welcomed", 0) == 0;
    if ((welcome || wantGuide()) && g_welcomeAt < 0) g_welcomeAt = timeSinceLevelLoad() + 0.8f;
}

// ======================= install =======================
bool savedataInstall() {
    Il2CppClass* CSM = findClass("", "CloudSaveManager");
    Il2CppClass* Zip = findClass("System.IO.Compression", "ZipFile");
    Il2CppClass* Dir = findClass("System.IO", "Directory");
    Il2CppClass* File = findClass("System.IO", "File");
    Il2CppClass* App = findClass("UnityEngine", "Application");
    Il2CppClass* SM = findClass("UnityEngine.SceneManagement", "SceneManager");
    Il2CppClass* SaveM = findClass("", "SaveManagement");
    Il2CppClass* DT = findClass("System", "DateTime");
    Il2CppClass* Txt = findClass("UnityEngine.UI", "Text");
    Il2CppClass* KeyK = findClass("", "Key");
    Il2CppClass* SongBase = findClass("", "SongBase");
    Il2CppClass* Song = findClass("", "SongsItem");
    S.GI = findClass("", "GameInformation");
    S.saveToFolder = CSM ? findMethod(CSM, "SaveToFolder", 1) : nullptr;
    S.loadFromFolder = CSM ? findMethod(CSM, "LoadFromFolder", 3) : nullptr;
    S.extract = Zip ? findMethod(Zip, "ExtractToDirectory", 2, IL2CPP_TYPE_STRING) : nullptr;
    S.dirExists = Dir ? findMethod(Dir, "Exists", 1) : nullptr;
    S.dirDelete = Dir ? findMethod(Dir, "Delete", 2) : nullptr;
    S.dirCreate = Dir ? findMethod(Dir, "CreateDirectory", 1) : nullptr;
    S.dirFiles = Dir ? findMethod(Dir, "GetFiles", 1) : nullptr;
    S.fileTime = File ? findMethod(File, "GetLastWriteTime", 1) : nullptr;
    S.tmpPath = App ? findMethod(App, "get_temporaryCachePath", 0) : nullptr;
    S.restart = S.GI ? findMethod(S.GI, "DestroyMainForRestart", 0) : nullptr;
    S.loadScene = SM ? findMethod(SM, "LoadScene", 1, IL2CPP_TYPE_I4) : nullptr;
    S.encrypt2 = SaveM ? findMethod(SaveM, "Encrypt2", 1) : nullptr;
    S.now = DT ? findMethod(DT, "get_Now", 0) : nullptr;
    S.dtString = DT ? findMethod(DT, "ToString", 1, IL2CPP_TYPE_STRING) : nullptr;
    S.txtOverflow = Txt ? findMethod(Txt, "set_horizontalOverflow", 1) : nullptr;
    S.getKey = S.GI ? findMethod(S.GI, "GetKey", 2, IL2CPP_TYPE_STRING) : nullptr;
    S.giKeyStore = S.GI ? fieldOffset(S.GI, "keyStore") : -1;
    S.giSong = S.GI ? fieldOffset(S.GI, "song") : -1;
    S.allSongs = SongBase ? findMethod(SongBase, "get_AllSongsBypassLimitedAndOthers", 0) : nullptr;
    if (!S.allSongs && SongBase) S.allSongs = findMethod(SongBase, "get_AllSongs", 0);
    S.songId = Song ? fieldOffset(Song, "songsId") : -1;
    S.songLevels = Song ? fieldOffset(Song, "levels") : -1;
    S.keyName = KeyK ? fieldOffset(KeyK, "keyName") : -1;
    S.keyUnlocked = KeyK ? fieldOffset(KeyK, "unlockedTimes") : -1;
    S.keyKind = KeyK ? fieldOffset(KeyK, "kindOfKey") : -1;
    S.keyTimes = KeyK ? fieldOffset(KeyK, "unlockTimes") : -1;
    bool mods = true;
    for (int i = 0; i < 5; i++) {
        Il2CppClass* k = findClass("", kModClass[i]);
        S.mod[i] = k;
        S.modCtor[i] = k ? findMethod(k, ".ctor", 0) : nullptr;
        S.modName[i] = k ? findMethod(k, "get_Name", 0) : nullptr;
        S.modVersion[i] = k ? findMethod(k, "get_Version", 0) : nullptr;
        S.modLocal[i] = k ? findMethod(k, "LoadFromLocal", 0) : nullptr;
        S.modSerialize[i] = k ? findMethod(k, "Serialize", 0) : nullptr;
        if (!S.modCtor[i] || !S.modName[i] || !S.modVersion[i] || !S.modLocal[i] || !S.modSerialize[i]) mods = false;
    }
    S.recData = S.mod[M_RECORD] ? fieldOffset(S.mod[M_RECORD], "data") : -1;
    if (S.mod[M_KEY] && S.modSerialize[M_KEY])
        orig_keySerialize = (SerFn)hookVirtual(S.mod[M_KEY], S.modSerialize[M_KEY], (void*)hk_keySerialize);
    S.ok = S.saveToFolder && S.loadFromFolder && S.extract && S.dirExists && S.dirDelete && S.dirCreate && S.tmpPath &&
           S.restart && S.loadScene && mods;
    S.unlockOk = S.ok && S.encrypt2 && S.getKey && S.giKeyStore >= 0 && S.keyName >= 0 && S.keyUnlocked >= 0 &&
                 S.keyKind >= 0 && S.keyTimes >= 0 && S.recData >= 0;
    Il2CppClass* CS = findClass("", "ChapterSelector");
    const MethodInfo* csStart = CS ? findMethod(CS, "Start", 0) : nullptr;
    if (csStart) orig_CS_Start = (VoidFn)hookMethod(csStart, (void*)hk_CS_Start);
    LOG("savedata: official save %s, all-unlocked save %s, file list %s, welcome %s", S.ok ? "on" : "off",
        S.unlockOk ? "on" : "off", S.dirFiles ? "on" : "off", orig_CS_Start ? "on" : "off");
    return S.ok;
}
