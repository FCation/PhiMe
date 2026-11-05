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
static int g_guideSeen = 0, g_tutorialSeen = 0;  // PhiMeMod.loginGuide / .tutorial, kept across DeleteAll like the rest
static void modPrefsSnapshot() {
    configLoad();
    g_guideSeen = prefGetInt("PhiMeMod.loginGuide", 0);
    g_tutorialSeen = prefGetInt("PhiMeMod.tutorial", 0);
    g_kvN = 0;
    forEachChart(snapOne, nullptr);
}
static void modPrefsRestore() {
    configSave();
    for (int i = 0; i < g_kvN; i++) prefSetStringRaw(g_kv[i].k, g_kv[i].v);
    prefSetInt("PhiMeMod.welcomed", 1);
    if (g_guideSeen) prefSetInt("PhiMeMod.loginGuide", 1);
    if (g_tutorialSeen) prefSetInt("PhiMeMod.tutorial", g_tutorialSeen);
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

static void loginNow();  // the first-run guide's 现在登录 (below), also the 账号 tab's 登录
void saveAction(int action) {
    onEveryFrame(savedataFrame);  // picker results arrive through it
    if (action == SAVE_LICENSE) {
        openLicense();
        return;
    }
    if (action == SAVE_B30) {
        b30Open();
        return;
    }
    if (action == SAVE_TUTORIAL) {
        tutorialOpen();
        return;
    }
    if (action == SAVE_LOGIN) {
        loginNow();
        return;
    }
    if (action == SAVE_FORGET_LOGIN) {
        cloudForget();
        LOG("savedata: the saved TapTap login was cleared from the panel");
        saveMessage(T("已清除", "Cleared"),
                    T("PhiMe 保存的 TapTap 登录凭证已删除。下次点「登录」时会重新扫码，可以换一个账号。\n\n"
                      "游戏当前的登录状态不变；要退出当前账号，请在游戏的账号页点「登出」。",
                      "PhiMe's saved TapTap login is deleted. The next Log in asks for a new scan, so you can use another "
                      "account.\n\nThe game stays logged in as it is; to log out, use 登出 on the game's account page."));
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
static float g_dOpenT = -1, g_dClawdT = 0, g_dTop = kDTop;  // g_dTop: half the open card's height
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
    float H = d.height > kDH ? d.height : kDH, top = H / 2;
    g_dTop = top;
    Obj* card = newUI("card", root);
    place(card, Vec2{0.5f, 0.5f}, Vec2{0.5f, 0.5f}, Vec2{0, -16}, Vec2{kDW, H});
    g_dCard.set(card);
    image(card, "border", SPR_ROUND, rgb(C_LINE), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{kDW + 4, H + 4}, 26);
    image(card, "bg", SPR_ROUND, rgb(C_IVORY), Vec2{0.5f, 0.5f}, Vec2{0, 0}, Vec2{kDW, H}, 24, true);
    g_dClawd.set(image(card, "clawd", SPR_CLAWD, rgb(0xFFFFFF), Vec2{0.5f, 0.5f}, Vec2{kDClawd.x, top + 2 + 34}, Vec2{120, 68}));
    g_dClawdT = 0;
    text(card, title, 30, rgb(C_TEXT), MiddleLeft, true, Vec2{0, 0.5f}, Vec2{0, 0.5f}, Vec2{kDPad, top - 52},
         Vec2{kDW - 2 * kDPad - 120, 44});
    image(card, "divider", SPR_SOLID, rgb(C_LINE), Vec2{0.5f, 0.5f}, Vec2{0, top - 92}, Vec2{kDW - 2 * kDPad, 2});
    float bw = d.bodyW > 0 ? d.bodyW : kDW - 2 * kDPad;
    Obj* bt = text(card, body, 22, rgb(C_BODY), UpperLeft, false, Vec2{0, 0.5f}, Vec2{0, 1}, Vec2{kDPad, top - 116},
                   Vec2{bw, 230 + (H - kDH)});
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
        setPos(rt(c), Vec2{kDClawd.x, g_dTop + 2 + 34 + clawdHop(g_dClawdT, 14)});
    }
}

// ======================= first-run guide (新手引导) =======================
// Six short pages: what PhiMe is, song select, practice, display + skins, how this install logs in, the last tips.
// Shown once (pref PhiMeMod.tutorial = 2; 1.1.x and older users get it once too, their old welcome was one card),
// and again from PhiMe 小助手 → 关于 → 新手引导. The login page depends on the install:
//   Android: the re-signed APK can't use the game's TapTap login -> PhiMe's QR login (account.cpp)
//   iOS, official bundle ID (TrollStore, wildcard-profile certificates): the game's own login, PhiMe stays out
//   iOS, changed bundle ID (Apple-ID signing): the game's login fails with -101 sdk_not_matched -> PhiMe's QR login
static float g_welcomeAt = -1;
static const int kTutorialVersion = 2, kPages = 6;
static int g_page = 0;
static void showPage();

static bool needModLogin() { return accountModLogin() && cloudAvailable() && !accountLoggedIn(); }
static void loginNow() {
    if (accountLoginNow()) return;
    saveMessage(T("没能开始登录", "Could not start the login"),
#if defined(__APPLE__)
                T("PhiMe 的登录组件没准备好，或者游戏的登录组件没找到。日志里 account: 和 cloud(iOS): 开头的行有原因。",
                  "PhiMe's login could not start, or the game's login was not found. See the log lines starting with account: and cloud(iOS):."));
#else
                T("PhiMe 的 Java 部分没有加载（需要 Android 8 以上），或者游戏的登录组件没找到。日志里 account: 开头的行有原因。",
                  "PhiMe's Java side did not load (needs Android 8+), or the game's login was not found. See the log lines starting with account:."));
#endif
}
static void tutorialFinish(bool login) {
    prefSetInt("PhiMeMod.welcomed", 1);
    prefSetInt("PhiMeMod.tutorial", kTutorialVersion);
    prefSetInt("PhiMeMod.loginGuide", 1);  // the login page was part of it
    prefSave();
    LOG("savedata: guide finished at page %d%s", g_page + 1, login ? ", logging in" : "");
    if (login) loginNow();
}
static void pageNext() {
    if (g_page + 1 < kPages) {
        g_page++;
        showPage();
    } else {
        tutorialFinish(false);
    }
}
static void pagePrev() {
    if (g_page > 0) g_page--;
    showPage();
}
static void pageSkip() { tutorialFinish(false); }
static void pageLogin() { tutorialFinish(true); }

// the login page's text for this install (also on the panel's 账号 tab)
const char* loginHelpText();
static const char* loginPageText() { return loginHelpText(); }
const char* loginHelpText() {
    static char b[900];
#if defined(__APPLE__)
    if (accountModLogin()) {
        snprintf(b, sizeof b,
                 T("你的安装包名是 %s，是签名工具改的（免费 Apple ID 签名都会改）。游戏自带的 TapTap 登录会检查包名，所以会失败：-101 sdk_not_matched。\n\n"
                   "PhiMe 已接管账号页的「登录」：改用 TapTap 扫码登录，之后同步、自动同步都是游戏原本的。\n"
                   "想用游戏自带的登录：用巨魔 TrollStore，或带通配描述文件的证书重新签名，保留原包名。",
                   "This install's bundle ID is %s: the signing tool changed it (free Apple ID signing always does). The game's "
                   "TapTap login checks it and fails: -101 sdk_not_matched.\n\nSo PhiMe runs 登录 on the account page as a TapTap QR "
                   "login; sync and auto sync are then the game's own.\nFor the game's own login, sign with TrollStore or a "
                   "certificate with a wildcard profile, keeping the original ID."),
                 accountBundleId());
    } else {
        snprintf(b, sizeof b, "%s",
                 T("你的安装保留了原版包名 games.Pigeon.Phigros（比如巨魔 TrollStore，或带通配描述文件的证书签名）。\n\n"
                   "直接用游戏自带的登录就好：账号页点「登录」，跳到 TapTap 授权后回来。PhiMe 不插手登录。\n\n"
                   "以后如果换成免费 Apple ID 签名（包名会被改），PhiMe 会自动改用扫码登录。",
                   "This install kept the official bundle ID games.Pigeon.Phigros (TrollStore, or a certificate with a wildcard "
                   "profile).\n\nUse the game's own login: 登录 on the account page, authorize in TapTap, come back. PhiMe stays out "
                   "of it.\n\nIf you later sign with a free Apple ID (which changes the ID), PhiMe switches to the QR login by itself."));
    }
#else
    snprintf(b, sizeof b, "%s",
             T("这个安装包为了装 PhiMe 重新签过名，游戏原本的 TapTap 登录会检查签名并拒绝（signature not match）。\n\n"
               "PhiMe 已接管账号页的「登录」：用另一台设备上的 TapTap 扫码，或在本机用 TapTap / 浏览器打开链接确认，拿到凭证交给游戏。"
               "之后同步、自动同步和冲突提醒都是游戏原本的。凭证只存在本机。",
               "This APK is re-signed for PhiMe, and the game's TapTap login refuses a changed signature (signature not match).\n\n"
               "So PhiMe runs 登录 on the account page: scan with TapTap on another device, or open the link in TapTap / a browser on "
               "this phone. The token goes to the game; sync, auto sync and conflict warnings are then the game's own. The token stays "
               "on this phone."));
#endif
    return b;
}

static void showPage() {
    static const char* const titles[2][kPages] = {
        {"欢迎使用 PhiMe", "选歌界面", "练习模式", "显示和皮肤", "登录和云存档", "最后几句"},
        {"Welcome to PhiMe", "Song select", "Practice mode", "Display and skins", "Login and cloud save", "A few last tips"}};
    char title[96];
    snprintf(title, sizeof title, "%s  %d/%d", titles[langEn() ? 1 : 0][g_page], g_page + 1, kPages);
    const char* body = "";
    switch (g_page) {
    case 0:
        body = T("PhiMe 是 Phigros 的非官方 Mod，免费开源、禁止商用：花钱买到的，那你被骗了！它不改判定和分数，只帮你练习、推分和换皮肤。\n\n"
                 "设置入口：游戏「设置」里的「PhiMe 小助手」，选歌界面也有一个。\n\n"
                 "这份引导以后在「小助手 → 关于 → 新手引导」还能再看。",
                 "PhiMe is an unofficial Phigros mod, free and open source, no commercial use: if you paid for it, you were scammed! "
                 "It never changes judgements or scores; it helps you practise, push your scores and change skins.\n\n"
                 "Settings: PhiMe Assistant in the game's Settings; song select has an entry too.\n\n"
                 "You can read this guide again under Assistant → About → Guide.");
        break;
    case 1:
        body = T("封面下方多了一排按钮：\n"
                 "· 镜像：谱面左右翻转\n"
                 "· 练习：进歌后打开选段练习，成绩不保存\n"
                 "· 自动：Clawd 帮你打，成绩不保存\n"
                 "· 普通 / 课题：课题模式的判定和计分，记录只存本机\n"
                 "· 详情：判定数和 4 位小数准度\n\n"
                 "小助手「游玩」页里还有 B30 成绩：Best 27 + Phi 3 和每张谱面的推分准度。",
                 "A row of buttons under the cover:\n"
                 "· Mirror: the chart flipped left-right\n"
                 "· Practice: opens the section editor in the song, nothing saved\n"
                 "· Auto: Clawd plays for you, nothing saved\n"
                 "· Normal / Course: course-mode judging and scoring, records on this device\n"
                 "· Details: judgement counts and 4-decimal accuracy\n\n"
                 "The Assistant's Play page has Best 30: Best 27 + Phi 3 and the accuracy each chart needs for +0.01.");
        break;
    case 2:
        body = T("开着「练习」进歌会打开选段面板：拖 A / B 两个把手选一段，双指缩放时间轴。\n"
                 "· 速度 0.05x～2x，可以开「变速不变调」\n"
                 "· 流速：慢速时音符会飞得很慢，把流速拉到「抵消」标记，看起来就和原速一样\n"
                 "· AB 循环：一段反复练\n\n"
                 "暂停时屏幕下方的「选段练习」随时回到面板。",
                 "With Practice on, a song opens the section editor: drag the A / B handles, pinch to zoom the timeline.\n"
                 "· Speed 0.05x–2x, optionally keeping the pitch\n"
                 "· Flow: at low speed notes crawl; put Flow on the 1:1 mark and they look as fast as at full speed\n"
                 "· A-B loop: repeat one part\n\n"
                 "While paused, Sections at the bottom of the screen brings the editor back.");
        break;
    case 3:
        body = T("「显示」页：准度显示、判定详情、上隐条，还能隐藏分数和连击。\n"
                 "「其他」页：区分大小 P（±40 ms 金色特效）、正解音、快速加载、语言。\n"
                 "「皮肤」页：导入 Phira 格式的皮肤包（zip），音符、打击特效和打击音都会换，三种音效可以分别调音量。\n\n"
                 "继续前回放、开头 / 结尾的 SKIP 在打歌时自动出现。",
                 "Display: accuracy line, judgement counts, the top hide bar, hiding score and combo.\n"
                 "Other: Critical Perfect (±40 ms, gold effect), on-time hit sounds, fast loading, language.\n"
                 "Skins: import Phira skin packs (zip): notes, hit effects and hit sounds, each sound with its own volume.\n\n"
                 "Replay before resuming and the intro / ending SKIP show up by themselves while playing.");
        break;
    case 4:
        body = loginPageText();
        break;
    default:
        body = T("· 卸载官方版前，先在官方版里同步一次云存档，装好后登录再同步回来\n"
                 "· 现在不登录也没关系：小助手的「账号」页随时可以登录，也能看到这个安装该用哪种登录\n"
                 "· 遇到问题：把 Mod 文件夹里的 PhiMeMod-least.log 发给作者（「关于」页点「打开」），再说说在哪一步出的问题\n"
                 "· 练习、自动和课题的成绩都不会写进游戏记录\n\n"
                 "请支持正版，支持鸽游～",
                 "· Before uninstalling the official game, sync its cloud save; log in here and sync it back\n"
                 "· No need to log in now: the Assistant's Account tab logs in any time and shows which login this install uses\n"
                 "· Problems: send the author PhiMeMod-least.log from the mod folder (About → Open) and say what you were doing\n"
                 "· Practice, autoplay and course runs never reach the game's records\n\n"
                 "Please support the official game!");
        break;
    }
    SaveDialog d = {};
    d.title = title;
    d.body = body;
    d.modal = true;
    // as tall as the text needs (the card's header and buttons take ~230), at most the screen
    float h = textHeight(body, 22, false, 760 - 2 * 44) + 116 + 120;
    float maxH = canvasH() - 90;
    d.height = h < 470 ? 470 : h > maxH ? maxH : h;
    bool last = g_page == kPages - 1;
    if (last && needModLogin()) {
        d.ok = T("现在登录", "Log in now");
        d.onOk = pageLogin;
        d.extra = T("以后再说", "Later");
        d.onExtra = pageSkip;
    } else {
        d.ok = last ? T("开始玩吧", "Let's play") : T("下一步", "Next");
        d.onOk = pageNext;
        if (!last) {
            d.extra = T("跳过", "Skip");
            d.onExtra = pageSkip;
        }
    }
    if (g_page > 0) {
        d.cancel = T("上一步", "Back");
        d.onCancel = pagePrev;
    }
    saveDialog(d);
}
static void showWelcome() {
    g_page = 0;
    showPage();
    LOG("savedata: first-run guide shown (login: %s, bundle ID %s)", accountModLogin() ? "PhiMe's QR login" : "the game's own",
        accountBundleId());
}
void tutorialOpen() {
    onEveryFrame(savedataFrame);
    g_page = 0;
    showPage();
}
static bool wantTutorial() { return prefGetInt("PhiMeMod.tutorial", 0) < kTutorialVersion; }

// ---- the login reminder (once): users who finished an older guide but use PhiMe's login and are not logged in ----
static void guideLater() {
    prefSetInt("PhiMeMod.loginGuide", 1);
    prefSave();
    LOG("savedata: login guide -> later");
}
static void guideLogin() {
    guideLater();
    loginNow();
}
static bool wantGuide() { return prefGetInt("PhiMeMod.loginGuide", 0) == 0 && needModLogin(); }
static void showGuide() {
    dialog(T("登录 TapTap 账号", "Log in to TapTap"), loginPageText(), T("以后再说", "Later"), guideLater,
           T("现在登录", "Log in now"), guideLogin, true);
    LOG("savedata: login guide shown");
}

// ======================= per frame =======================
static void savedataFrame(void*) {
    dialogTick();
    pickerPoll();
    cloudPoll();
    if (g_welcomeAt >= 0 && timeSinceLevelLoad() >= g_welcomeAt) {
        g_welcomeAt = -1;
        if (saveDialogOpen()) return;
        if (wantTutorial()) showWelcome();
        else if (wantGuide()) showGuide();
    }
}

static void hk_CS_Start(Obj* self, const MethodInfo* m) {
    orig_CS_Start(self, m);
    onEveryFrame(savedataFrame);
    if ((wantTutorial() || wantGuide()) && g_welcomeAt < 0) g_welcomeAt = timeSinceLevelLoad() + 0.8f;
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
