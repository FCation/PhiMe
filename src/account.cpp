// The game's own TapTap account and cloud sync, on the re-signed Android APK.
//
// The game logs in with TDSUser.LoginWithTapTap, which the TapTap SDK refuses for a re-signed package ("signature
// not match"). Everything after the login is the game's own code and only needs TapSDKWrapper.User (a TDSUser):
//   StatsFrameController.Sync -> CloudSaveManager.SyncSave: the manual sync, with the game's own local / cloud
//       comparison popup when both changed
//   AutoSyncControl -> CloudSaveManager.SilenceSyncSave: the sync after every song; it compares the cloud save's
//       ModifiedAt with the last sync time this device recorded and gives up with SaveConflict when the cloud
//       changed elsewhere (no overwrite), exactly as in the official game
// So only the login is replaced. The account screen's 登录 (StatsFrameController.Login) runs our TapTap QR login
// (cloud.cpp / Cloud.java; the saved sessionToken when it still works), then the game's SDK takes the token with
// TDSUser.BecomeWithSessionToken, and the result becomes TapSDKWrapper.User. The SDK keeps that user on disk, so the
// next start is logged in by the game's own TapSDKWrapper.Init (TDSUser.GetCurrent). The account screen is then
// rebuilt by its own Start. (LoginWithTapTap itself can't be swapped: the game calls it directly, not through its
// MethodInfo.) 登出 (StatsFrameController.Logout) is the game's, and our saved token is forgotten too.
// iOS has no signature check: nothing is hooked there.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

#if defined(__APPLE__)
bool accountInstall() { return false; }
bool accountLoggedIn() { return true; }
bool accountLoginNow() { return false; }
#else
typedef void (*VoidFn)(Obj*, const MethodInfo*);
static VoidFn orig_SFC_Login, orig_SFC_Logout;
static const MethodInfo *g_become, *g_loggedIn, *g_setUser, *g_sfcStart;
static Handle g_screen, g_task;  // the account screen whose 登录 was tapped; BecomeWithSessionToken's Task
static char g_nick[96];
static float g_waited = 0;

static bool boolOf(const MethodInfo* m, Obj* self) {
    bool ok = false;
    Obj* r = m ? invokeObj(m, self, nullptr, &ok) : nullptr;
    return ok && r && *(bool*)il2cpp_object_unbox(r);
}
static bool loggedIn() { return boolOf(g_loggedIn, nullptr); }

static void failed(const char* why, bool forget) {
    if (forget) cloudForget();  // the server refused this token: the next 登录 asks for a scan
    char b[600];
    snprintf(b, sizeof b, T("TapTap 登录成功了，但游戏没能用这个账号登录（%s）。\n\n再点一次「登录」试试。",
                            "TapTap login worked, but the game could not log in with it (%s).\n\nTap Log in again."),
             why);
    saveMessage(T("登录失败", "Login failed"), b);
}

static void accountFrame(void*) {
    Obj* task = (Obj*)g_task.get();
    if (!task) return;
    Il2CppClass* k = il2cpp_object_get_class(task);
    g_waited += unscaledDt();
    if (!boolOf(findMethod(k, "get_IsCompleted", 0), task)) {
        if (g_waited > 30) {
            g_task.clear();
            LOG("account: BecomeWithSessionToken did not finish in 30 s");
            failed(T("连接超时", "timed out"), false);
        }
        return;
    }
    g_task.clear();
    bool ok;
    Obj* user = nullptr;
    if (!boolOf(findMethod(k, "get_IsFaulted", 0), task) && !boolOf(findMethod(k, "get_IsCanceled", 0), task))
        user = invokeObj(findMethod(k, "get_Result", 0), task, nullptr, &ok);
    if (!user) {
        LOG("account: BecomeWithSessionToken failed");
        failed(T("云端不认这个凭证", "the server refused the token"), true);
        return;
    }
    void* a[] = {user};
    if (!invokeSafe(g_setUser, nullptr, a) || !loggedIn()) {
        failed(T("设置账号出错", "could not set the account"), false);
        return;
    }
    LOG("account: the game is logged in as %s (its own sync and auto sync work now)", g_nick);
    Obj* scr = (Obj*)g_screen.get();
    if (alive(scr) && g_sfcStart) invokeSafe(g_sfcStart, scr);  // the account screen shows the logged-in state
    char b[400];
    snprintf(b, sizeof b, T("已用 %s 登录。\n\n游戏自己的云存档同步可以用了：账号页的「同步」，以及打开自动同步后每打完一首歌自动上传。云端存档被别的设备改过时，游戏会让你选择保留哪份，自动同步不会覆盖。",
                            "Logged in as %s.\n\nThe game's own cloud sync works now: Sync on the account page, and the upload after every song when auto sync is on. If the cloud save changed elsewhere the game asks which one to keep; auto sync never overwrites it."),
             g_nick[0] ? g_nick : "TapTap");
    saveMessage(T("登录成功", "Logged in"), b);
}

// TapTap QR login done: the game's SDK logs in with the sessionToken
static void onToken(const char* token, const char* nick) {
    snprintf(g_nick, sizeof g_nick, "%s", nick ? nick : "");
    void* a[] = {str(token)};
    bool ok;
    Obj* task = invokeObj(g_become, nullptr, a, &ok);
    if (!ok || !task) {
        failed(T("游戏的登录接口出错", "the game's login call failed"), false);
        return;
    }
    g_task.set(task);
    g_waited = 0;
    onEveryFrame(accountFrame);
    LOG("account: TapTap QR login done (%s), the game's SDK takes the token", g_nick);
}

bool accountLoggedIn() { return loggedIn(); }
// the first-run guide's 现在登录: the same login without the account screen
bool accountLoginNow() {
    if (!g_become || !g_setUser || loggedIn()) return false;
    g_screen.clear();
    return cloudLogin(onToken);
}

static void hk_SFC_Login(Obj* self, const MethodInfo* m) {
    if (loggedIn() || !cloudAvailable()) {
        orig_SFC_Login(self, m);
        return;
    }
    g_screen.set(self);
    if (!cloudLogin(onToken)) orig_SFC_Login(self, m);
}

static void hk_SFC_Logout(Obj* self, const MethodInfo* m) {
    bool was = loggedIn();
    orig_SFC_Logout(self, m);
    if (was) {
        cloudForget();
        LOG("account: logged out, the saved TapTap login is forgotten too");
    }
}

bool accountInstall() {
    Il2CppClass* SFC = findClass("", "StatsFrameController");
    Il2CppClass* TDS = findClass("TapTap.Bootstrap", "TDSUser");
    Il2CppClass* TSW = findClass("", "TapSDKWrapper");
    g_become = TDS ? findMethod(TDS, "BecomeWithSessionToken", 1) : nullptr;
    g_loggedIn = TSW ? findMethod(TSW, "get_LoggedIn", 0) : nullptr;
    g_setUser = TSW ? findMethod(TSW, "set_User", 1) : nullptr;
    g_sfcStart = SFC ? findMethod(SFC, "Start", 0) : nullptr;
    const MethodInfo* login = SFC ? findMethod(SFC, "Login", 0) : nullptr;
    const MethodInfo* logout = SFC ? findMethod(SFC, "Logout", 0) : nullptr;
    bool ok = g_become && g_loggedIn && g_setUser && login && logout;
    if (ok) {
        orig_SFC_Login = (VoidFn)hookMethod(login, (void*)hk_SFC_Login);
        orig_SFC_Logout = (VoidFn)hookMethod(logout, (void*)hk_SFC_Logout);
        ok = orig_SFC_Login && orig_SFC_Logout;
    }
    LOG("account: game login through PhiMe's TapTap login %s", ok ? "on" : "OFF");
    return ok;
}
#endif
