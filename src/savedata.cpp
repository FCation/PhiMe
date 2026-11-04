// PhiMe's own data inside the game's cloud save, the dialog card every PhiMe page uses, the first-run welcome
// and (Android) the login guide, and the settings panel's buttons.
//
// The release dropped the save tools: official save export / import, PhiMe save export / import, the all-unlocked
// empty save and the cloud pull. The full old file is mod/disabled/saves/savedata.cpp (see mod/disabled/README.md).
#include "mod.h"

using namespace il2cpp;
using namespace unity;

typedef void (*VoidFn)(Obj*, const MethodInfo*);
static VoidFn orig_CS_Start;

static struct {
    const MethodInfo *dirExists, *dirDelete, *dirCreate, *tmpPath, *allSongs, *openURL;
    Il2CppClass* GI;
    int giSong, songId, songLevels;
} S;

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
// an empty folder in the game's temporary cache: "<cache>/<name>/"
static bool tmpDir(char* out, int cap, const char* name) {
    if (!S.tmpPath || !S.dirExists || !S.dirDelete || !S.dirCreate) return false;
    bool ok;
    Il2CppString* t = (Il2CppString*)invokeObj(S.tmpPath, nullptr, nullptr, &ok);
    char b[320];
    if (!ok || utf8(t, b, sizeof b) <= 0) return false;
    snprintf(out, cap, "%s/%s/", b, name);
    return mdirReset(out);
}

// a file in an emptied folder of the game's cache (skin.cpp: where the picker copies a skin pack)
bool saveTempFile(char* out, int cap, const char* dirName, const char* file) {
    char dir[400];
    if (!tmpDir(dir, sizeof dir, dirName)) return false;
    snprintf(out, cap, "%s%s", dir, file);
    return true;
}

// ======================= PhiMe's own PlayerPrefs, kept across the game's DeleteAll =======================
// Loading a cloud save wipes PlayerPrefs, where PhiMe keeps its settings and course records: they are read before
// and written back after.
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
    // only while the game is filling /save/ for an upload
    if (cloudDir(dir, sizeof dir) && fileExists(dir)) {
        snprintf(path, sizeof path, "%sphime.json", dir);
        int rec = 0, course = 0;
        bool w = detailsExportFile(path, &rec, &course);
        LOG("savedata: phime.json for the cloud save, %d details, %d course records%s", rec, course,
            w ? "" : " (NOT written)");
    }
    return r;
}
// PlayerPrefs.DeleteAll outside the save sandbox (the game loading a cloud save): PhiMe's own keys (settings,
// course records) are kept, and the loaded save's phime.json, if any, is merged in.
void saveWipe(bool after) {
    if (!after) {
        modPrefsSnapshot();
        return;
    }
    modPrefsRestore();
    char dir[400], path[480];
    if (!cloudDir(dir, sizeof dir)) return;
    snprintf(path, sizeof path, "%sphime.json", dir);
    if (!fileExists(path)) return;
    int rec = 0, course = 0;
    bool ok = detailsImportFile(path, &rec, &course);
    LOG("savedata: cloud save's phime.json merged: %s (%d details, %d course records)", ok ? "ok" : "FAILED", rec,
        course);
}

static void dialog(const char* title, const char* body, const char* cancel, void (*onCancel)(), const char* ok,
                   void (*onOk)(), bool modal = false);
static void savedataFrame(void*);

void saveMessage(const char* title, const char* body) { dialog(title, body, nullptr, nullptr, T("好", "OK"), nullptr); }

// ======================= the panel's buttons =======================
// the license's summary page on the Creative Commons site, in the mod's language
static void openLicense() {
    const char* url = langEn() ? "https://creativecommons.org/licenses/by-nc-sa/4.0/deed.en"
                               : "https://creativecommons.org/licenses/by-nc-sa/4.0/deed.zh-hans";
    if (S.openURL) {
        void* a[] = {str(url)};
        if (invokeSafe(S.openURL, nullptr, a)) {
            LOG("savedata: license page opened (%s)", url);
            return;
        }
    }
    LOG("savedata: Application.OpenURL %s, the license link is shown instead", S.openURL ? "FAILED" : "not found");
    char b[400];
    snprintf(b, sizeof b, T("没能打开浏览器。协议原文在：\n%s", "Could not open a browser. The license is at:\n%s"), url);
    saveMessage(T("开源协议", "License"), b);
}

void saveAction(int action) {
    onEveryFrame(savedataFrame);  // picker results arrive through it
    if (action == SAVE_LICENSE) {
        openLicense();
        return;
    }
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
        // Android: the PhiMeMod folder (log, skins, libPhiMeMod.so updates); iOS: Documents (log + PhiMeMod/)
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
        snprintf(b, sizeof b, "%s\n%s\n\n%s", how, saveFolderHint(), T("日志 PhiMeMod.log 在这里；新的 libPhiMeMod.so 放进来，重启游戏就会加载。", "PhiMeMod.log is here; drop a new libPhiMeMod.so in and restart the game to load it."));
#endif
        dialog(T("Mod 文件夹", "Mod folder"), b, nullptr, nullptr, T("好", "OK"), nullptr);
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
    onEveryFrame(savedataFrame);  // the opening animation
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
// Where the settings are, and the release terms in two lines (the full text: PhiMe 小助手 → 关于).
static float g_welcomeAt = -1;

static void welcomeOk() {
    prefSetInt("PhiMeMod.welcomed", 1);
    prefSave();
    LOG("savedata: welcome read");
    g_welcomeAt = timeSinceLevelLoad() + 0.4f;  // then the login guide (Android)
}
static void showWelcome() {
    dialog(T("欢迎使用 PhiMe 小助手", "Welcome to PhiMe Assistant"),
           T("设置入口：游戏的「设置」→「PhiMe 小助手」，选歌界面也有一个。\n\n"
             "PhiMe 免费开源，禁止商用：如果你是花钱买到的，那你被骗了！它不改判定和分数，只帮你练习和推分。"
             "请支持正版，支持鸽游～",
             "Settings: the game's Settings → PhiMe Assistant; song select has an entry too.\n\n"
             "PhiMe is free and open source, no commercial use: if you paid for it, you were scammed! It never "
             "changes judgements or scores; it only helps you practise. Please support the official game!"),
           nullptr, nullptr, T("好", "OK"), welcomeOk, true);
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
}

static void hk_CS_Start(Obj* self, const MethodInfo* m) {
    orig_CS_Start(self, m);
    onEveryFrame(savedataFrame);
    bool welcome = prefGetInt("PhiMeMod.welcomed", 0) == 0;
    if ((welcome || wantGuide()) && g_welcomeAt < 0) g_welcomeAt = timeSinceLevelLoad() + 0.8f;
}

// ======================= install =======================
bool savedataInstall() {
    Il2CppClass* Dir = findClass("System.IO", "Directory");
    Il2CppClass* App = findClass("UnityEngine", "Application");
    Il2CppClass* SongBase = findClass("", "SongBase");
    Il2CppClass* Song = findClass("", "SongsItem");
    Il2CppClass* KeyMod = findClass("", "GameKeySaveModule");
    S.GI = findClass("", "GameInformation");
    S.dirExists = Dir ? findMethod(Dir, "Exists", 1) : nullptr;
    S.dirDelete = Dir ? findMethod(Dir, "Delete", 2) : nullptr;
    S.dirCreate = Dir ? findMethod(Dir, "CreateDirectory", 1) : nullptr;
    S.tmpPath = App ? findMethod(App, "get_temporaryCachePath", 0) : nullptr;
    S.openURL = App ? findMethod(App, "OpenURL", 1) : nullptr;
    S.giSong = S.GI ? fieldOffset(S.GI, "song") : -1;
    S.allSongs = SongBase ? findMethod(SongBase, "get_AllSongsBypassLimitedAndOthers", 0) : nullptr;
    if (!S.allSongs && SongBase) S.allSongs = findMethod(SongBase, "get_AllSongs", 0);
    S.songId = Song ? fieldOffset(Song, "songsId") : -1;
    S.songLevels = Song ? fieldOffset(Song, "levels") : -1;
    const MethodInfo* keySer = KeyMod ? findMethod(KeyMod, "Serialize", 0) : nullptr;
    if (keySer) orig_keySerialize = (SerFn)hookVirtual(KeyMod, keySer, (void*)hk_keySerialize);
    Il2CppClass* CS = findClass("", "ChapterSelector");
    const MethodInfo* csStart = CS ? findMethod(CS, "Start", 0) : nullptr;
    if (csStart) orig_CS_Start = (VoidFn)hookMethod(csStart, (void*)hk_CS_Start);
    bool temp = S.tmpPath && S.dirExists && S.dirDelete && S.dirCreate;
    LOG("savedata: phime.json in the cloud save %s, temp folder %s, OpenURL %s, welcome %s",
        orig_keySerialize ? "on" : "off", temp ? "on" : "off", S.openURL ? "on" : "off", orig_CS_Start ? "on" : "off");
    return temp && orig_CS_Start;
}
