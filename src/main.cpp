// Entry point: wait for the IL2CPP VM, then install hooks. No code pages are modified.
#include "mod.h"

static int g_logFd = -2;
static int g_leastFd = -2;  // PhiMeMod-least.log: this launch only (overwritten at every start), small to send

#if !defined(__APPLE__)
// Android: /sdcard/Android/media/<pkg>/PhiMeMod/PhiMeMod.log, next to where updates go, so a file
// manager (MT) can open it. Started fresh each launch.
static int openAndroidLog() {
    char pkg[160] = {0};
    int fd = open("/proc/self/cmdline", 0);
    if (fd < 0) return -1;
    ssize_t n = read(fd, pkg, sizeof(pkg) - 1);
    close(fd);
    if (n <= 0) return -1;
    for (int i = 0; pkg[i]; i++)
        if (pkg[i] == ':') pkg[i] = 0;
    char path[320];
    snprintf(path, sizeof(path), "/sdcard/Android/media/%s", pkg);
    mkdir(path, 0770);
    snprintf(path, sizeof(path), "/sdcard/Android/media/%s/PhiMeMod", pkg);
    mkdir(path, 0770);
    snprintf(path, sizeof(path), "/sdcard/Android/media/%s/PhiMeMod/PhiMeMod-least.log", pkg);
    g_leastFd = open(path, 0x0001 | 0x0040 | 0x0200, 0660);  // O_WRONLY|O_CREAT|O_TRUNC
    snprintf(path, sizeof(path), "/sdcard/Android/media/%s/PhiMeMod/PhiMeMod.log", pkg);
    return open(path, 0x0001 | 0x0040 | 0x0200, 0660);
}
#endif

void pm_log(const char* fmt, ...) {
    char buf[768];
    int n = snprintf(buf, sizeof(buf), "[PhiMeMod] ");
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    __builtin_va_end(ap);
#if defined(__APPLE__)
    syslog(4 /*LOG_WARNING*/, "%s", buf);
    if (g_logFd == -2) {  // Documents/PhiMeMod.log inside the app container
        char path[512];
        const char* home = getenv("HOME");
        g_logFd = home ? open((snprintf(path, sizeof(path), "%s/Documents/PhiMeMod.log", home), path), PM_O_FLAGS, 0644) : -1;
        g_leastFd = home ? open((snprintf(path, sizeof(path), "%s/Documents/PhiMeMod-least.log", home), path), PM_O_REPLACE, 0644) : -1;
    }
#else
    __android_log_print(5 /*ANDROID_LOG_WARN*/, "PhiMeMod", "%s", buf);
    if (g_logFd == -2) g_logFd = openAndroidLog();
#endif
    if (g_logFd >= 0) {
        size_t len = strlen(buf);
        if (len > sizeof(buf) - 2) len = sizeof(buf) - 2;
        buf[len] = '\n';
        write(g_logFd, buf, len + 1);
        if (g_leastFd >= 0) write(g_leastFd, buf, len + 1);
    }
}

// Two PhiMe builds injected at once (an old dylib left next to the new one) hook everything twice and draw
// their UI on top of each other. Each copy reports its version to the first-loaded copy's PhiMe_Claim (dlsym
// with the default handle finds that one) when it is loaded; once IL2CPP is up only the newest goes on. Builds before 0.14.2 don't
// take part.
extern "C" __attribute__((visibility("default"))) int PhiMe_Claim(int version) {
    static int best = 0;
    if (version > best) best = version;
    return best;
}

typedef int (*ClaimFn)(int);
static void claimNow() {  // at load time: every injected copy has reported long before IL2CPP is up
    ClaimFn c = (ClaimFn)dlsym(PM_RTLD_DEFAULT, "PhiMe_Claim");
    if (c) c(PM_VERSION_NUM);
}
static bool newestCopy() {
    ClaimFn c = (ClaimFn)dlsym(PM_RTLD_DEFAULT, "PhiMe_Claim");
    if (!c) return true;
    int best = c(0);
    if (best > PM_VERSION_NUM) {
        pm_log("PhiMeMod v" PM_VERSION ": a newer copy (%d) is loaded too, this one stays off", best);
        return false;
    }
    return true;
}

static void* initThread(void*) {
    // up to ~2 minutes for the game to boot
    for (int i = 0; i < 1200; i++) {
        if (il2cpp::resolveApi() && il2cpp::ready()) break;
        usleep(100000);
    }
    if (!il2cpp::ready()) {
        pm_log("IL2CPP not found, mod disabled");
        return nullptr;
    }
    if (!newestCopy()) return nullptr;
    void* thread = il2cpp::il2cpp_thread_attach(il2cpp::il2cpp_domain_get());
    pm_log("PhiMeMod v" PM_VERSION " starting");
    bool u = unity::init();
    bool a = u && autoplayInstall();
    bool p = u && panelInstall();
    bool r = u && resultInstall();
    bool pr = u && practiceInstall();
    bool rs = u && resumeInstall();
    bool h = u && hudInstall();  // after practice: chains onto its ScoreControl.Update hook
    bool l = u && loadingInstall();
    bool c = u && criticalInstall();
    bool co = u && courseInstall();  // after resume: chains onto its ProgressControl.Update hook
    bool d = u && detailsInstall();
    bool sv = u && savedataInstall();
    bool ti = u && titleInstall();
    bool ac = u && accountInstall();  // Android: the game's TapTap login via PhiMe's QR login (its own sync after)
    bool rk = u && rksInstall();
    bool ra = u && resAccInstall();
    bool hs = u && hitsoundInstall();
    bool sk = u && skipInstall();
    bool sn = u && skinInstall();
    bool ch = u && challengeInstall();
    bool mk = u && maskInstall();
    pm_log("init: unity=%d autoplay=%d panel=%d result=%d practice=%d resume=%d hud=%d loading=%d "
           "critical=%d course=%d details=%d savedata=%d title=%d account=%d rks=%d resacc=%d hitsound=%d skip=%d "
           "skin=%d challenge=%d mask=%d",
           u, a, p, r, pr, rs, h, l, c, co, d, sv, ti, ac, rk, ra, hs, sk, sn, ch, mk);
    // this thread is about to exit: unregister it so the GC never tries to suspend a dead thread
    if (thread) il2cpp::il2cpp_thread_detach(thread);
    return nullptr;
}

__attribute__((constructor)) static void phimeModInit() {
    claimNow();
    void* th = nullptr;
    if (pthread_create(&th, nullptr, initThread, nullptr) == 0) pthread_detach(th);
}
