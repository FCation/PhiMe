// 云端拉取: download the official cloud save without the game's TapTap SDK (Android only).
//
// A re-signed APK cannot log in through the game ("signature not match": the SDK checks the package signature on
// the phone), but the save server does not care. phime.Cloud (android/picker/src/phime/Cloud.java, in the same dex
// as the file picker) does the network part: TapTap QR login -> LeanCloud sessionToken -> the newest _GameSave
// -> its zip. The zip is exactly what the game's own cloud download gets, so it goes through the usual import
// (backup first, LoadFromFolder, restart). Nothing is uploaded. The token stays in the game's private files dir,
// so the next pull needs no scan; 退出登录 deletes it.
//
// Same phone, no second device: the QR card can open the login link in the TapTap app or a browser instead.
// A sessionToken from somewhere else (a score bot) can be pasted from the clipboard (粘贴).
#include "mod.h"

using namespace unity;

#if defined(__APPLE__)
void cloudPull(bool) {}
void cloudPoll() {}
bool cloudAvailable() { return false; }
bool cloudLogin(void (*)(const char*, const char*)) { return false; }
void cloudForget() {}
#else
#include "jni_lite.h"

enum { OP_PULL, OP_PASTE, OP_CANCEL, OP_OPEN_TAPTAP, OP_OPEN_BROWSER, OP_LOGOUT, OP_LANG, OP_LOGIN };
enum { EV_FAIL = -1, EV_CANCELLED = 0, EV_DONE = 1, EV_QR = 2, EV_PROGRESS = 3, EV_TOKEN = 4 };

// ---- events from the Java thread, handed to the main thread ----
struct Ev {
    int id, kind;
    char *a, *b;
};
static Ev g_q[32];
static int g_qn = 0, g_lock = 0;
static void lock() {
    while (__atomic_exchange_n(&g_lock, 1, __ATOMIC_ACQUIRE)) usleep(50);
}
static void unlock() { __atomic_store_n(&g_lock, 0, __ATOMIC_RELEASE); }
static char* dupStr(const char* s) {
    if (!s) return nullptr;
    size_t n = strlen(s);
    char* p = (char*)malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

// phime.Cloud.event(int id, int kind, String a, String b), on a Java thread
static void jniEvent(void* env, JObj, int id, int kind, JObj a, JObj b) {
    J j;
    j.e = env;
    const char* ca = j.chars(a);
    const char* cb = j.chars(b);
    Ev e{id, kind, dupStr(ca), dupStr(cb)};
    j.release(a, ca);
    j.release(b, cb);
    lock();
    if (g_qn < 32) {
        g_q[g_qn++] = e;
    } else {
        free(e.a);
        free(e.b);
    }
    unlock();
}

// ---- the Java side ----
static JObj g_cls = nullptr, g_cmd = nullptr;
static bool g_tried = false, g_ok = false;

static bool setup(J& j) {
    if (g_tried) return g_ok;
    g_tried = true;
    if (j.pushFrame(16) != 0) {
        j.exc();
        return false;
    }
    JObj k = jniModClass(j, "phime.Cloud");
    if (k) {
        JNINativeMethod nm = {"event", "(IILjava/lang/String;Ljava/lang/String;)V", (void*)jniEvent};
        if (j.registerNatives(k, &nm, 1) == 0 && !j.exc()) {
            g_cmd = j.staticMethod(k, "cmd", "(Landroid/app/Activity;IILjava/lang/String;)V");
            if (!j.exc() && g_cmd) {
                g_cls = k;
                g_ok = true;
            }
        }
    }
    j.popFrame();
    LOG("cloud: Java side %s", g_ok ? "ready" : "FAILED");
    return g_ok;
}

bool cloudAvailable() {
    J j;
    return jniEnv(j) && setup(j);
}

static bool cmd(int id, int op, const char* arg) {
    J j;
    if (!jniEnv(j) || !setup(j)) return false;
    if (j.pushFrame(8) != 0) {
        j.exc();
        return false;
    }
    bool ok = false;
    JObj act = jniActivity(j);
    if (act) {
        JValue a[4];
        a[0].l = act;
        a[1].i = id;
        a[2].i = op;
        a[3].l = j.newString(arg ? arg : "");
        j.callStaticVoid(g_cls, g_cmd, a);
        ok = !j.exc();
    }
    j.popFrame();
    return ok;
}

// ======================= the cards =======================
static const int C_INK = 0x141413, C_LINE = 0xE8E6DC;
static int g_seq = 0, g_id = 0;  // the running job (0 = none)
static bool g_qrShown = false;
static char g_zip[512], g_status[300];
static int g_qrN = 0;
static char* g_qr = nullptr;  // N*N '0' / '1'
// a login job (the game's account screen, account.cpp) instead of a pull: ends with the sessionToken
static bool g_login = false;
static void (*g_onToken)(const char* token, const char* nick) = nullptr;
static const char* jobTitle() { return g_login ? T("登录 TapTap", "TapTap login") : T("云端拉取", "Cloud pull"); }

static void stopJob() {
    if (g_id) cmd(g_id, OP_CANCEL, nullptr);
    g_id = 0;
    g_qrShown = false;
}
static void onCancel() {
    LOG("cloud: cancelled by the user");
    stopJob();
}

static void showStatus(const char* s) {
    snprintf(g_status, sizeof g_status, "%s", s);
    if (g_qrShown) return;  // the QR card shows it in its own text
    if (saveDialogOpen()) {
        saveDialogBody(g_status);
        return;
    }
    SaveDialog d = {};
    d.title = jobTitle();
    d.body = g_status;
    d.cancel = T("取消", "Cancel");
    d.onCancel = onCancel;
    saveDialog(d);
}

// ---- the QR card ----
static const float kQr = 232;                               // white square, card units
static const Vec2 kQrPos{760.f / 2 - 44 - kQr / 2, -12};    // right side, between the divider and the buttons

static void drawQr(Obj* card) {
    if (!g_qr || g_qrN <= 0) return;
    image(card, "qrBorder", SPR_ROUND, rgb(C_LINE), Vec2{0.5f, 0.5f}, kQrPos, Vec2{kQr + 4, kQr + 4}, 16);
    image(card, "qrBg", SPR_ROUND, rgb(0xFFFFFF), Vec2{0.5f, 0.5f}, kQrPos, Vec2{kQr, kQr}, 14);
    float m = (kQr - 28) / g_qrN;  // 14 units of quiet zone each side
    float left = kQrPos.x - m * g_qrN / 2, top = kQrPos.y + m * g_qrN / 2;
    int rects = 0;
    for (int y = 0; y < g_qrN; y++)
        for (int x = 0; x < g_qrN;) {
            if (g_qr[y * g_qrN + x] != '1') {
                x++;
                continue;
            }
            int x1 = x;
            while (x1 + 1 < g_qrN && g_qr[y * g_qrN + x1 + 1] == '1') x1++;
            // a hair wider / taller than the module so neighbouring runs leave no seams
            image(card, "qr", SPR_SOLID, rgb(C_INK), Vec2{0.5f, 0.5f},
                  Vec2{left + (x + x1 + 1) * m / 2, top - (y + 0.5f) * m}, Vec2{(x1 - x + 1) * m + 0.3f, m + 0.3f});
            rects++;
            x = x1 + 1;
        }
    LOG("cloud: QR %dx%d drawn with %d rects", g_qrN, g_qrN, rects);
}

static void showQr();
static void onOpenTapTap() {
    cmd(g_id, OP_OPEN_TAPTAP, nullptr);
    showQr();  // the button closed the card: put it back, the login keeps waiting
}
static void onOpenBrowser() {
    cmd(g_id, OP_OPEN_BROWSER, nullptr);
    showQr();
}

static const char* qrBody() {
    static char body[700];
    snprintf(body, sizeof body,
             T("用另一台设备上的 TapTap 扫右边的二维码，确认授权后会自动继续。\n\n"
             "只有这一台手机？点「用 TapTap 打开」或「浏览器」，确认后回到游戏就好。\n\n%s", "Scan the code on the right with TapTap on another device and confirm; this continues by itself.\n\nOnly this phone? Tap Open TapTap or Browser, confirm there, then come back.\n\n%s"),
             g_status);
    return body;
}

static void showQr() {
    g_qrShown = true;
    SaveDialog d = {};
    d.title = T("用 TapTap 扫码登录", "Log in with TapTap");
    d.body = qrBody();
    d.bodyW = 760 - 2 * 44 - kQr - 28;
    d.cancel = T("取消", "Cancel");
    d.onCancel = onCancel;
    d.ok = T("用 TapTap 打开", "Open TapTap");
    d.onOk = onOpenTapTap;
    d.extra = T("浏览器", "Browser");
    d.onExtra = onOpenBrowser;
    d.decorate = drawQr;
    saveDialog(d);
}
static void qrStatus(const char* s) {
    snprintf(g_status, sizeof g_status, "%s", s);
    if (saveDialogOpen()) saveDialogBody(qrBody());
}

// ---- results ----
static void onLogout() {
    if (!cmd(0, OP_LOGOUT, nullptr)) {
        saveMessage(T("没能退出登录", "Could not log out"), T("Java 部分没有响应。", "The Java side did not respond."));
        return;
    }
    LOG("cloud: logged out (token deleted)");
    saveMessage(T("已退出登录", "Logged out"), T("保存在这台手机上的云存档凭证已经删掉，下次拉取要重新扫码。", "The cloud login saved on this phone is deleted; the next pull asks for a scan again."));
}

static void handle(const Ev& e) {
    switch (e.kind) {
    case EV_PROGRESS:
        LOG("cloud: %s", e.b ? e.b : "");
        if (g_qrShown) qrStatus(e.b ? e.b : "");
        else showStatus(e.b ? e.b : "");
        break;
    case EV_QR: {
        const char* bits = e.b;
        int n = 0;
        while (bits && *bits >= '0' && *bits <= '9') n = n * 10 + (*bits++ - '0');
        free(g_qr);
        g_qr = nullptr;
        g_qrN = 0;
        if (bits && *bits == ':' && n > 0 && (int)strlen(bits + 1) == n * n) {
            g_qr = dupStr(bits + 1);
            g_qrN = n;
        }
        LOG("cloud: login link %s (QR %d)", e.a ? e.a : "?", g_qrN);
        snprintf(g_status, sizeof g_status, "%s", T("等待扫码…（二维码 5 分钟内有效）", "Waiting for the scan… (the code is valid for 5 minutes)"));
        showQr();
        break;
    }
    case EV_TOKEN: {
        LOG("cloud: logged in as %s", e.b ? e.b : "?");
        g_id = 0;
        g_qrShown = false;
        saveDialogClose();
        void (*cb)(const char*, const char*) = g_onToken;
        g_onToken = nullptr;
        if (cb && e.a) cb(e.a, e.b ? e.b : "");
        break;
    }
    case EV_DONE:
        LOG("cloud: downloaded %s | %s", e.a ? e.a : "", e.b ? e.b : "");
        g_id = 0;
        g_qrShown = false;
        saveImportCloud(e.a ? e.a : g_zip, e.b, onLogout);
        break;
    case EV_CANCELLED:
        g_id = 0;
        g_qrShown = false;
        break;
    default: {
        LOG("cloud: FAILED %s", e.b ? e.b : "");
        g_id = 0;
        g_qrShown = false;
        char b[700];
        snprintf(b, sizeof b, g_login ? T("%s", "%s") : T("%s\n\n当前存档没有改动。云存档的接口是社区摸索出来的，如果一直失败，可能是 TapTap 或鸽游改了接口。", "%s\n\nYour save is unchanged. The cloud API was worked out by the community; if this keeps failing, TapTap or Pigeon Games may have changed it."),
                 e.b ? e.b : T("未知原因", "unknown reason"));
        saveMessage(g_login ? T("登录失败", "Login failed") : T("云端拉取失败", "Cloud pull failed"), b);
        break;
    }
    }
}

void cloudPoll() {
    if (!__atomic_load_n(&g_qn, __ATOMIC_ACQUIRE)) return;
    Ev evs[32];
    lock();
    int n = g_qn;
    memcpy(evs, g_q, sizeof(Ev) * n);
    g_qn = 0;
    unlock();
    for (int i = 0; i < n; i++) {
        if (evs[i].id == g_id && g_id) handle(evs[i]);
        free(evs[i].a);
        free(evs[i].b);
    }
}

void cloudPull(bool fromClipboard) {
    if (!cloudAvailable()) {
        saveMessage(T("云端拉取不可用", "Cloud pull unavailable"), T("PhiMe 的 Java 部分没能加载（需要 Android 8 以上）。", "PhiMe's Java side did not load (needs Android 8 or newer)."));
        return;
    }
    stopJob();
    if (!saveTempFile(g_zip, sizeof g_zip, "PhiMeCloud", "cloud.zip")) {
        saveMessage(T("云端拉取不可用", "Cloud pull unavailable"), T("游戏的临时文件夹用不了。", "The game's temporary folder is not usable."));
        return;
    }
    cmd(0, OP_LANG, langEn() ? "en" : "zh");  // the Java side's messages follow the mod's language
    g_id = ++g_seq;
    g_login = false;
    g_qrShown = false;
    saveDialogClose();
    showStatus(fromClipboard ? T("正在读取剪贴板里的凭证…", "Reading the token from the clipboard…") : T("正在连接云端…", "Connecting…"));
    if (!cmd(g_id, fromClipboard ? OP_PASTE : OP_PULL, g_zip)) {
        g_id = 0;
        saveMessage(T("云端拉取失败", "Cloud pull failed"), T("Java 部分没有响应。", "The Java side did not respond."));
        return;
    }
    LOG("cloud: job %d started (%s)", g_id, fromClipboard ? "clipboard token" : "saved token / QR login");
}
// The game's own login, without its TapTap SDK (account.cpp): a working sessionToken (the saved one, else a QR
// login), handed to `onToken`. Nothing is downloaded.
bool cloudLogin(void (*onToken)(const char* token, const char* nick)) {
    if (!cloudAvailable()) return false;
    stopJob();
    cmd(0, OP_LANG, langEn() ? "en" : "zh");
    g_id = ++g_seq;
    g_login = true;
    g_onToken = onToken;
    g_qrShown = false;
    saveDialogClose();
    showStatus(T("正在连接 TapTap…", "Connecting to TapTap…"));
    if (!cmd(g_id, OP_LOGIN, nullptr)) {
        g_id = 0;
        g_onToken = nullptr;
        return false;
    }
    LOG("cloud: login job %d started", g_id);
    return true;
}
void cloudForget() { cmd(0, OP_LOGOUT, nullptr); }
#endif
