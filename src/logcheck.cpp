// 分析日志 (关于 page): reads this launch's PhiMeMod-least.log, says in plain words what looks wrong (modules that did
// not install, hangs, the game's own errors, failed steps, login / skin problems, low frame rates), and exports the
// file with the system picker so it can be sent without hunting for it in a file manager.
#include "mod.h"

using namespace il2cpp;
using namespace unity;

static bool readFile(const char* path, char** out, int* n) {
    *out = nullptr;
    *n = 0;
    if (!path || !path[0]) return false;
    int fd = open(path, 0);
    if (fd < 0) return false;
    long len = lseek(fd, 0, 2);
    if (len <= 0 || len > 4L * 1024 * 1024) {
        close(fd);
        return false;
    }
    char* b = (char*)malloc((size_t)len + 1);
    bool ok = b && pread(fd, b, (size_t)len, 0) == len;
    close(fd);
    if (!ok) {
        free(b);
        return false;
    }
    b[len] = 0;
    *out = b;
    *n = (int)len;
    return true;
}

// (no strstr / strchr in the freestanding build)
static const char* sfind(const char* s, const char* sub) {
    for (; *s; s++) {
        int j = 0;
        while (sub[j] && s[j] == sub[j]) j++;
        if (!sub[j]) return s;
    }
    return nullptr;
}
static char* schr(char* s, char c) {
    for (; *s; s++)
        if (*s == c) return s;
    return nullptr;
}
static const char* schr(const char* s, char c) { return schr((char*)s, c); }
static bool has(const char* s, const char* sub) { return sfind(s, sub) != nullptr; }

// a line's text after "[PhiMeMod] ", cut to ~90 bytes on a character boundary
static void snippet(char* dst, int cap, const char* line) {
    const char* p = sfind(line, "] ");
    p = p ? p + 2 : line;
    int max = cap - 4 < 90 ? cap - 4 : 90, k = 0;
    while (p[k] && k < max) k++;
    if (p[k]) {
        while (k > 0 && ((unsigned char)p[k] & 0xC0) == 0x80) k--;
        snprintf(dst, cap, "%.*s…", k, p);
    } else {
        snprintf(dst, cap, "%s", p);
    }
}

static char g_body[3072];
static int g_len = 0;
static int g_items = 0;
static void add(const char* fmt, ...) {
    if (g_len >= (int)sizeof g_body - 8) return;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int w = vsnprintf(g_body + g_len, sizeof g_body - g_len, fmt, ap);
    __builtin_va_end(ap);
    if (w > 0) g_len += w;
    if (g_len > (int)sizeof g_body - 1) g_len = (int)sizeof g_body - 1;
}

static void exported(int status, const char*, const char* name) {
    if (status > 0)
        saveMessage(T("日志已导出", "Log exported"), T("把刚保存的日志文件发给作者就好，顺便说说是在哪一步出的问题。", "Send the saved file to the author, and say what you were doing when it went wrong."));
    else if (status < 0)
        saveMessage(T("没能导出", "Export failed"), name ? name : "");
    LOG("logcheck: export %s", status > 0 ? "done" : status < 0 ? "failed" : "cancelled");
}
static void exportPath(const char* path) {
    if (!path || !path[0]) {
        saveMessage(T("没有日志文件", "No log file"), T("这次启动没有写出日志文件。", "This launch wrote no log file."));
        return;
    }
    if (pickerAvailable() && pickerSave(path, PICK_TEXT, exported)) return;
    char b[700];
    snprintf(b, sizeof b, T("系统的文件面板打不开。日志在：\n%s", "The system file sheet could not open. The log is at:\n%s"), path);
    saveMessage(T("导出日志", "Export the log"), b);
}
static void exportLeast() { exportPath(logLeastPath()); }
static void exportFull() { exportPath(logFullPath()); }

void logCheckOpen() {
    char* text = nullptr;
    int n = 0;
    g_len = 0;
    g_items = 0;
    g_body[0] = 0;
    if (!readFile(logLeastPath(), &text, &n)) {
        add("%s", T("读不到这次启动的日志。可以点「导出日志」试试，或者在「Mod 文件夹」里找 PhiMeMod-least.log。",
                     "This launch's log could not be read. Try Export, or find PhiMeMod-least.log in the mod folder."));
    } else {
        int lines = 0, unityErr = 0, unityExc = 0, threw = 0, failed = 0, stuck = 0;
        float minFps = 1e9f;
        char ver[32] = "?", firstExc[128] = "", firstFail[3][128] = {"", "", ""}, skinMsg[128] = "", loginMsg[128] = "",
             offMods[200] = "";
        bool sdk = false, dropped = false;
        for (char* line = text; line && *line;) {
            char* nl = schr(line, '\n');
            if (nl) *nl = 0;
            lines++;
            const char* p;
            if ((p = sfind(line, "PhiMeMod v")) && has(line, " starting")) {
                int k = 0;
                p += 10;
                while (p[k] && p[k] != ' ' && k < 30) ver[k] = p[k], k++;
                ver[k] = 0;
            }
            if (has(line, "] init: ")) {  // "init: unity=1 autoplay=1 ...": what did not install
                for (const char* q = sfind(line, "init: ") + 6; *q;) {
                    const char* eq = schr(q, '=');
                    if (!eq) break;
                    if (eq[1] == '0') {
                        size_t l = strlen(offMods);
                        snprintf(offMods + l, sizeof offMods - l, "%s%.*s", l ? "、" : "", (int)(eq - q), q);
                    }
                    const char* sp = schr(eq, ' ');
                    if (!sp) break;
                    q = sp + 1;
                }
            }
            if ((p = sfind(line, "watchdog: no frame for "))) {
                int s = 0;
                for (p += 23; *p >= '0' && *p <= '9'; p++) s = s * 10 + (*p - '0');
                if (s > stuck) stuck = s;
            }
            if (has(line, "] unity exception")) {
                if (!unityExc++) snippet(firstExc, sizeof firstExc, line);
            } else if (has(line, "] unity error") && !has(line, "GKErrorDomain") && !has(line, "Login failed")) {
                unityErr++;
            }
            if (has(line, " threw ")) threw++;
            if ((has(line, "FAILED") || has(line, "MISSING")) && failed < 3) snippet(firstFail[failed++], 128, line);
            if (has(line, "] skin:") && (has(line, "failed") || has(line, "rejected")) && !skinMsg[0])
                snippet(skinMsg, sizeof skinMsg, line);
            if ((has(line, "] cloud") || has(line, "] account") || has(line, "login")) &&
                (has(line, "error") || has(line, "fail") || has(line, "-101")) && !loginMsg[0])
                snippet(loginMsg, sizeof loginMsg, line);
            if (has(line, "sdk_not_matched") || has(line, "-101")) sdk = true;
            if (has(line, "callback DROPPED")) dropped = true;
            if ((p = sfind(line, " frames (")) && has(line, "] perf:")) {
                float v = 0, f = 0.1f;
                bool dot = false;
                for (p += 9; (*p >= '0' && *p <= '9') || *p == '.'; p++) {
                    if (*p == '.') dot = true;
                    else if (!dot) v = v * 10 + (*p - '0');
                    else v += (*p - '0') * f, f *= 0.1f;
                }
                if (v > 0 && v < minFps) minFps = v;
            }
            line = nl ? nl + 1 : nullptr;
        }
        free(text);
        add(T("PhiMe %s · 这次启动 · 共 %d 行\n\n", "PhiMe %s · this launch · %d lines\n\n"), ver, lines);
        if (offMods[0]) g_items++, add(T("· 这些功能没装上：%s（游戏版本对不上，或者注入有问题）\n", "· These parts did not install: %s (another game version, or an injection problem)\n"), offMods);
        if (stuck >= 3) g_items++, add(T("· 游戏卡住过 %d 秒没有画面\n", "· The game froze for %d s\n"), stuck);
        if (unityExc) g_items++, add(T("· 游戏代码出错 %d 次，第一次：%s\n", "· The game's code failed %d time(s), first: %s\n"), unityExc, firstExc);
        if (unityErr) g_items++, add(T("· 游戏报告了 %d 个错误\n", "· The game reported %d error(s)\n"), unityErr);
        if (threw) g_items++, add(T("· PhiMe 调用游戏时出错 %d 次\n", "· PhiMe's calls into the game failed %d time(s)\n"), threw);
        for (int i = 0; i < failed; i++) g_items++, add(T("· 有一步没成功：%s\n", "· A step failed: %s\n"), firstFail[i]);
        if (skinMsg[0]) g_items++, add(T("· 皮肤包有问题：%s\n", "· Skin problem: %s\n"), skinMsg);
        if (sdk) g_items++, add("%s", T("· 登录被 TapTap 拒绝（sdk_not_matched）：这个安装包改过包名，请用 PhiMe 的扫码登录\n", "· TapTap refused the login (sdk_not_matched): this install has another bundle ID, use PhiMe's QR login\n"));
        else if (loginMsg[0]) g_items++, add(T("· 登录可能有问题：%s\n", "· Login may have failed: %s\n"), loginMsg);
        if (minFps < 45 && minFps < 1e8f) g_items++, add(T("· 打歌时帧率最低到过 %.0f fps\n", "· The frame rate dropped to %.0f fps in a level\n"), minFps);
        if (dropped) g_items++, add("%s", T("· 内部回调满了，有功能可能没在运行\n", "· Internal callbacks are full: a feature may not be running\n"));
        if (!g_items) add("%s", T("· 没发现明显的问题。\n", "· Nothing looks wrong.\n"));
        add("%s", T("\n还是不对劲的话，点「导出日志」把文件发给作者，并说说在哪一步出的问题。", "\nStill wrong? Export the log, send it to the author, and say what you were doing."));
    }
    LOG("logcheck: %d finding(s)", g_items);
    SaveDialog d = {};
    d.title = T("日志分析", "Log check");
    d.body = g_body;
    d.cancel = T("关闭", "Close");
    d.ok = T("导出日志", "Export");
    d.onOk = exportLeast;
#if defined(__APPLE__)
    d.extra = T("完整日志", "Full log");
#else
    d.extra = T("上次启动的", "Last launch");
#endif
    d.onExtra = exportFull;
    d.height = 640;
    saveDialog(d);
}
