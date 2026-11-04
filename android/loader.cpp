// PhiMe loader: replaces Unity's libmain.so (the original is renamed libmain_orig.so).
//
// On load it pulls in the mod, then forwards JNI_OnLoad to the original libmain, which registers
// Unity's natives exactly as before. The mod is loaded from:
//   1. the newer of
//        /sdcard/Android/media/<pkg>/PhiMeMod/libPhiMeMod.so       (file managers such as MT can write here)
//        /sdcard/Android/data/<pkg>/files/PhiMeMod/libPhiMeMod.so  (push.ps1 / adb)
//      copied into the app's private dir first, because shared storage is mounted noexec
//   2. otherwise libPhiMeMod.so bundled in the APK
// so updating the mod is: drop in the new .so + restart the game. No reinstall.
typedef unsigned long size_t;
typedef long ssize_t;

extern "C" {
void* dlopen(const char* path, int mode);
void* dlsym(void* handle, const char* symbol);
char* dlerror(void);
int open(const char* path, int flags, ...);
ssize_t read(int fd, void* buf, size_t n);
ssize_t write(int fd, const void* buf, size_t n);
int close(int fd);
int mkdir(const char* path, unsigned int mode);
int rename(const char* from, const char* to);
int stat(const char* path, void* st);
int snprintf(char* buf, size_t n, const char* fmt, ...);
int __android_log_print(int prio, const char* tag, const char* fmt, ...);
}

#define TAG "PhiMeMod"
#define LOGI(...) __android_log_print(4, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, TAG, __VA_ARGS__)
#define RTLD_NOW 2
#define O_RDONLY 0
#define O_WRONLY_CREAT_TRUNC (01 | 0100 | 01000)

static bool packageName(char* out, int cap) {
    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd < 0) return false;
    ssize_t n = read(fd, out, cap - 1);
    close(fd);
    if (n <= 0) return false;
    out[n] = 0;
    for (int i = 0; out[i]; i++)  // "com.foo:process" -> "com.foo"
        if (out[i] == ':') out[i] = 0;
    return out[0] != 0;
}

// Copy src to dst atomically. Returns false if src does not exist.
static bool copyFile(const char* src, const char* dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) return false;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
    int out = open(tmp, O_WRONLY_CREAT_TRUNC, 0700);
    if (out < 0) {
        close(in);
        LOGE("loader: cannot write %s", tmp);
        return false;
    }
    static char buf[64 * 1024];
    bool ok = true;
    for (;;) {
        ssize_t n = read(in, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0 || write(out, buf, (size_t)n) != n) {
            ok = false;
            break;
        }
    }
    close(in);
    close(out);
    if (ok) ok = rename(tmp, dst) == 0;
    return ok;
}

// Modification time in seconds, or -1 if the file doesn't exist. arm64 struct stat: st_mtim at offset 88.
static long mtimeOf(const char* path) {
    alignas(16) unsigned char st[256];
    if (stat(path, st) != 0) return -1;
    return *(long*)(st + 88);
}

// Pick whichever of two path templates (formatted with pkg) exists and is newer.
static bool newestOf(const char* pkg, const char* a, const char* b, char* out, size_t cap) {
    char pa[512], pb[512];
    snprintf(pa, sizeof(pa), a, pkg);
    snprintf(pb, sizeof(pb), b, pkg);
    long ta = mtimeOf(pa), tb = mtimeOf(pb);
    if (ta < 0 && tb < 0) return false;
    snprintf(out, cap, "%s", ta >= tb ? pa : pb);
    return true;
}

// Save import: a PlayerPrefs file taken from the official app (adb backup, see import-save.ps1) is pushed to
//   /sdcard/Android/data/<pkg>/files/PhiMeMod/import/<pkg>.v2.playerprefs.xml
// We put it in place before Unity reads its preferences. The current file is backed up first, and the
// import file is renamed afterwards so it is applied only once.
static void importSave(const char* pkg) {
    char src[512], dir[512], dst[512], bak[512], done[512], a[512], b[512];
    snprintf(a, sizeof(a), "/sdcard/Android/media/%%s/PhiMeMod/import/%s.v2.playerprefs.xml", pkg);
    snprintf(b, sizeof(b), "/sdcard/Android/data/%%s/files/PhiMeMod/import/%s.v2.playerprefs.xml", pkg);
    if (!newestOf(pkg, a, b, src, sizeof(src))) return;
    snprintf(dir, sizeof(dir), "/data/data/%s/shared_prefs", pkg);
    mkdir(dir, 0771);
    snprintf(dst, sizeof(dst), "%s/%s.v2.playerprefs.xml", dir, pkg);
    snprintf(bak, sizeof(bak), "/data/data/%s/files/PhiMeMod/playerprefs.before-import.xml", pkg);
    if (copyFile(dst, bak)) LOGI("import: previous save backed up to %s", bak);
    if (!copyFile(src, dst)) {
        LOGE("import: failed to write %s", dst);
        return;
    }
    snprintf(done, sizeof(done), "%s.imported", src);
    if (rename(src, done) != 0) LOGE("import: could not rename %s (it will be imported again next launch)", src);
    LOGI("import: official save imported");
}

static void loadMod() {
    char pkg[256];
    if (packageName(pkg, sizeof(pkg))) {
        char dir[512], priv[512];
        snprintf(dir, sizeof(dir), "/data/data/%s/files", pkg);
        mkdir(dir, 0700);
        snprintf(dir, sizeof(dir), "/data/data/%s/files/PhiMeMod", pkg);
        mkdir(dir, 0700);
        snprintf(priv, sizeof(priv), "%s/libPhiMeMod.so", dir);
        importSave(pkg);  // needs files/PhiMeMod for the backup copy, created just above

        // Update locations; the most recently modified file wins:
        //  Android/media/<pkg>/... : writable by file managers (e.g. MT) on every Android version
        //  Android/data/<pkg>/...  : where adb push.ps1 puts it
        char ext[512];
        if (newestOf(pkg, "/sdcard/Android/media/%s/PhiMeMod/libPhiMeMod.so",
                     "/sdcard/Android/data/%s/files/PhiMeMod/libPhiMeMod.so", ext, sizeof(ext)) &&
            copyFile(ext, priv)) {
            if (dlopen(priv, RTLD_NOW)) {
                LOGI("loader: loaded pushed mod %s", ext);
                return;
            }
            LOGE("loader: pushed mod %s failed to load: %s", ext, dlerror());
        }
    }
    if (dlopen("libPhiMeMod.so", RTLD_NOW))
        LOGI("loader: loaded bundled mod");
    else
        LOGE("loader: no mod loaded: %s", dlerror());
}

// the mod asks for the JavaVM here (its system file picker needs JNI)
static void* g_vm = nullptr;
extern "C" __attribute__((visibility("default"))) void* PhiMe_JavaVM() { return g_vm; }

extern "C" __attribute__((visibility("default"))) int JNI_OnLoad(void* vm, void* reserved) {
    g_vm = vm;
    loadMod();
    void* orig = dlopen("libmain_orig.so", RTLD_NOW);
    if (!orig) {
        LOGE("loader: libmain_orig.so missing: %s", dlerror());
        return -1;
    }
    typedef int (*OnLoad)(void*, void*);
    OnLoad f = (OnLoad)dlsym(orig, "JNI_OnLoad");
    return f ? f(vm, reserved) : 0x00010006;  // JNI_VERSION_1_6
}
