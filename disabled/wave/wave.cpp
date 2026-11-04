// Music waveform for the practice editor, the game side: find the song's bundle file through Addressables
// (main thread), then read + decode it on a worker thread (wave_decode.cpp). The bins fill in from the start
// while it runs, and are kept in <persistentDataPath>/PhiMeWave/<bundle name>.pmw so the next time is instant.
#include "practice.h"
#include "wave_decode.h"

using namespace il2cpp;
using namespace unity;

namespace {

struct Job {
    int refs;  // the UI's reference + the worker's
    char key[128];
    char file[4][512], entry[4][256];  // candidate bundles: a file, or an entry inside it (APK)
    int nFiles;
    char cacheDir[512];
    uint16_t* bins;
    uint32_t cap;
    int rate;
    volatile uint32_t done;  // bins ready
    uint32_t written;        // worker only: bins written so far
    volatile int state;      // 1 working, 2 done, -1 failed
    volatile int cancel;
};

Job* g_job = nullptr;

void release(Job* j) {
    if (__atomic_sub_fetch(&j->refs, 1, __ATOMIC_ACQ_REL) == 0) {
        free(j->bins);
        free(j);
    }
}

// ---------- worker ----------
struct Fd {
    int fd;
};
bool fdReadAt(void* ctx, uint64_t off, void* dst, size_t n) {
    int fd = ((Fd*)ctx)->fd;
    uint8_t* p = (uint8_t*)dst;
    while (n) {
        ssize_t k = pread(fd, p, n, (long)off);
        if (k <= 0) return false;
        p += k;
        off += (uint64_t)k;
        n -= (size_t)k;
    }
    return true;
}

bool sinkBegin(void* u, int rate, uint32_t samples) {
    Job* j = (Job*)u;
    if (j->cancel) return false;
    uint32_t cap = samples / WAVE_BIN + 256;
    uint16_t* b = (uint16_t*)calloc((size_t)cap * 2, sizeof(uint16_t));
    if (!b) return false;
    j->rate = rate;
    j->cap = cap;
    __atomic_store_n(&j->bins, b, __ATOMIC_RELEASE);
    return true;
}
bool sinkBin(void* u, uint32_t i, float peak, float rms) {
    Job* j = (Job*)u;
    if (i < j->cap) {
        j->bins[2 * i] = (uint16_t)(peak >= 1 ? 65535 : peak * 65535 + 0.5f);
        j->bins[2 * i + 1] = (uint16_t)(rms >= 1 ? 65535 : rms * 65535 + 0.5f);
        j->written = i + 1;
        if ((i & 255) == 255) __atomic_store_n(&j->done, i + 1, __ATOMIC_RELEASE);
    }
    return !j->cancel;
}

// "<dir>/<bundle file name without .bundle>.pmw"
void cachePath(const Job* j, int which, char* out, int cap) {
    const char* src = j->entry[which][0] ? j->entry[which] : j->file[which];
    const char* base = src;
    for (const char* p = src; *p; p++)
        if (*p == '/') base = p + 1;
    int n = snprintf(out, cap, "%s/%s", j->cacheDir, base);
    if (n > 7 && n < cap && !strcmp(out + n - 7, ".bundle")) out[n - 7] = 0;
    snprintf(out + strlen(out), cap - strlen(out), ".pmw");
}

bool loadCache(Job* j, int which) {
    char path[700];
    cachePath(j, which, path, sizeof path);
    int fd = open(path, 0);
    if (fd < 0) return false;
    uint8_t h[12];
    bool ok = read(fd, h, 12) == 12 && !memcmp(h, "PMW1", 4);
    uint32_t rate = 0, n = 0;
    if (ok) {
        memcpy(&rate, h + 4, 4);
        memcpy(&n, h + 8, 4);
        ok = rate > 0 && n > 16 && n < (1u << 24);
    }
    uint16_t* b = ok ? (uint16_t*)malloc((size_t)n * 4) : nullptr;
    Fd f{fd};
    ok = b && fdReadAt(&f, 12, b, (size_t)n * 4);
    close(fd);
    if (!ok) {
        free(b);
        return false;
    }
    j->rate = (int)rate;
    j->cap = n;
    __atomic_store_n(&j->bins, b, __ATOMIC_RELEASE);
    __atomic_store_n(&j->done, n, __ATOMIC_RELEASE);
    return true;
}

void saveCache(Job* j, int which, uint32_t n) {
    char path[700], tmp[720];
    cachePath(j, which, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int fd = open(tmp, PM_O_REPLACE, 0644);
    if (fd < 0) return;
    uint8_t h[12] = {'P', 'M', 'W', '1'};
    uint32_t rate = (uint32_t)j->rate;
    memcpy(h + 4, &rate, 4);
    memcpy(h + 8, &n, 4);
    bool ok = write(fd, h, 12) == 12 && write(fd, j->bins, (size_t)n * 4) == (ssize_t)n * 4;
    close(fd);
    if (ok) rename(tmp, path);
}

void* worker(void* arg) {
    Job* j = (Job*)arg;
    char why[160] = "no bundle file";
    bool ok = false;
    for (int i = 0; i < j->nFiles && !ok && !j->cancel; i++)
        if (loadCache(j, i)) {
            ok = true;
            LOG("wave: %s from the cache (%u bins)", j->key, j->done);
        }
    for (int i = 0; i < j->nFiles && !ok && !j->cancel; i++) {
        int fd = open(j->file[i], 0);
        if (fd < 0) {
            snprintf(why, sizeof why, "cannot open %s", j->file[i]);
            continue;
        }
        Fd f{fd};
        long size = lseek(fd, 0, 2);
        WaveFile wf{&f, (uint64_t)(size > 0 ? size : 0), fdReadAt};
        size_t n = 0;
        uint8_t* b = size > 0 ? waveReadBundle(&wf, j->entry[i], &n, why, sizeof why) : nullptr;
        close(fd);
        if (!b) continue;
        WaveSink s{j, sinkBegin, sinkBin};
        ok = waveDecode(b, n, &s, why, sizeof why);
        free(b);
        if (ok) {
            uint32_t total = j->written;
            __atomic_store_n(&j->done, total, __ATOMIC_RELEASE);
            mkdir(j->cacheDir, 0755);
            saveCache(j, i, total);
            LOG("wave: %s decoded, %u bins at %d Hz", j->key, total, j->rate);
        } else if (j->bins && !j->cancel) {  // a failed candidate: start the next one clean
            __atomic_store_n(&j->done, 0u, __ATOMIC_RELEASE);
            j->written = 0;
        }
    }
    if (!ok && !j->cancel) LOG("wave: %s: no waveform (%s)", j->key, why);
    __atomic_store_n(&j->state, ok ? 2 : -1, __ATOMIC_RELEASE);
    release(j);
    return nullptr;
}

// ---------- main thread: the key -> its bundle file(s) ----------
struct {
    bool tried, ok;
    const MethodInfo *instance, *resolve, *getLocs;
} A;

bool addrReady() {
    if (A.tried) return A.ok;
    A.tried = true;
    Il2CppClass* K = findClass("UnityEngine.AddressableAssets", "Addressables");
    Il2CppClass* Impl = findClass("UnityEngine.AddressableAssets", "AddressablesImpl");
    A.instance = K ? findMethod(K, "get_Instance", 0) : nullptr;
    A.resolve = K ? findMethod(K, "ResolveInternalId", 1) : nullptr;
    A.getLocs = Impl ? findMethod(Impl, "GetResourceLocations", 3) : nullptr;
    A.ok = A.instance && A.resolve && A.getLocs;
    if (!A.ok)
        LOG("wave: Addressables not found (instance %d, resolve %d, locations %d)", A.instance != nullptr,
            A.resolve != nullptr, A.getLocs != nullptr);
    return A.ok;
}

// elements of a List<T> or a T[]
int listItems(Obj* o, void*** out) {
    *out = nullptr;
    if (!o) return 0;
    Il2CppClass* k = il2cpp_object_get_class(o);
    FieldInfo* fi = il2cpp_class_get_field_from_name(k, "_items");
    FieldInfo* fs = il2cpp_class_get_field_from_name(k, "_size");
    if (fi && fs) {
        Il2CppArray* a = fld<Il2CppArray*>(o, (int)il2cpp_field_get_offset(fi));
        int n = fld<int>(o, (int)il2cpp_field_get_offset(fs));
        if (!a || n <= 0) return 0;
        *out = arrayData(a);
        return n;
    }
    const char* name = il2cpp_class_get_name(k);
    size_t len = strlen(name);
    if (len > 2 && name[len - 1] == ']') {  // an array
        Il2CppArray* a = (Il2CppArray*)o;
        *out = arrayData(a);
        return (int)a->max_length;
    }
    return 0;
}

Obj* call0(Obj* o, const char* method) {
    if (!o) return nullptr;
    const MethodInfo* m = findMethod(il2cpp_object_get_class(o), method, 0);
    bool ok = false;
    Obj* r = m ? invokeObj(m, o, nullptr, &ok) : nullptr;
    return ok ? r : nullptr;
}

bool startsWith(const char* s, const char* p) { return strlen(s) >= strlen(p) && !memcmp(s, p, strlen(p)); }

const char* strstr_pm(const char* s, const char* sub) {
    size_t n = strlen(sub);
    for (; *s; s++)
        if (!memcmp(s, sub, n)) return s;
    return nullptr;
}

// an internal id -> file (+ zip entry); false for what we cannot read (a download URL, ...)
bool splitPath(const char* id, char* file, int fileCap, char* entry, int entryCap) {
    entry[0] = 0;
    const char* p = id;
    bool jar = false;
    if (startsWith(p, "jar:")) {
        jar = true;
        p += 4;
    }
    if (startsWith(p, "file://")) p += 7;
    if (p[0] != '/') return false;
    const char* bang = jar ? strstr_pm(p, "!/") : nullptr;
    if (jar && !bang) return false;
    int fl = bang ? (int)(bang - p) : (int)strlen(p);
    if (fl >= fileCap) return false;
    memcpy(file, p, (size_t)fl);
    file[fl] = 0;
    if (bang) snprintf(entry, entryCap, "%s", bang + 2);
    return true;
}

void findBundles(Job* j) {
    j->nFiles = 0;
    if (!addrReady()) return;
    bool ok = false;
    Obj* impl = invokeObj(A.instance, nullptr, nullptr, &ok);
    if (!ok || !impl) return;
    Obj* locs = nullptr;
    void* args[] = {str(j->key), nullptr, &locs};
    invokeObj(A.getLocs, impl, args, &ok);
    void** items;
    int n = ok ? listItems(locs, &items) : 0;
    for (int i = 0; i < n && j->nFiles < 4; i++) {
        void** deps;
        int nd = listItems(call0((Obj*)items[i], "get_Dependencies"), &deps);
        for (int d = 0; d < nd && j->nFiles < 4; d++) {
            Il2CppString* id = (Il2CppString*)call0((Obj*)deps[d], "get_InternalId");
            if (!id) continue;
            void* ra[] = {id};
            Il2CppString* res = (Il2CppString*)invokeObj(A.resolve, nullptr, ra, &ok);
            char path[768];
            if (utf8(ok && res ? res : id, path, sizeof path) <= 0) continue;
            size_t len = strlen(path);
            if (len < 8 || strcmp(path + len - 7, ".bundle") != 0) continue;
            if (!splitPath(path, j->file[j->nFiles], sizeof j->file[0], j->entry[j->nFiles], sizeof j->entry[0])) {
                LOG("wave: bundle not readable here: %s", path);
                continue;
            }
            j->nFiles++;
        }
    }
    if (!j->nFiles) LOG("wave: no bundle for %s (%d locations)", j->key, n);
}

}  // namespace

// ---------- API ----------
void waveRequest(const char* key) {
    if (!key || !key[0]) return;
    if (g_job && !strcmp(g_job->key, key) && g_job->state != -1) return;
    if (g_job) {
        g_job->cancel = 1;
        release(g_job);
        g_job = nullptr;
    }
    Job* j = (Job*)calloc(1, sizeof(Job));
    if (!j) return;
    snprintf(j->key, sizeof j->key, "%s", key);
    j->refs = 1;
    j->state = 1;
    findBundles(j);
    const MethodInfo* pdp = findMethod(findClass("UnityEngine", "Application"), "get_persistentDataPath", 0);
    char dir[400] = "";
    if (pdp) utf8(callm<Il2CppString*>(pdp), dir, 380);
    snprintf(j->cacheDir, sizeof j->cacheDir, "%s/PhiMeWave", dir);
    g_job = j;
    if (!j->nFiles || !dir[0]) {
        j->state = -1;
        return;
    }
    LOG("wave: %s -> %s%s%s", key, j->file[0], j->entry[0][0] ? " ! " : "", j->entry[0]);
    j->refs = 2;
    void* th;
    if (pthread_create(&th, nullptr, worker, j) == 0) pthread_detach(th);
    else {
        j->refs = 1;
        j->state = -1;
    }
}

int waveState() { return g_job ? __atomic_load_n(&g_job->state, __ATOMIC_ACQUIRE) : 0; }

uint32_t waveVersion() { return g_job ? __atomic_load_n(&g_job->done, __ATOMIC_ACQUIRE) : 0; }

bool waveData(WaveData* out) {
    if (!g_job) return false;
    uint32_t n = __atomic_load_n(&g_job->done, __ATOMIC_ACQUIRE);
    uint16_t* b = __atomic_load_n(&g_job->bins, __ATOMIC_ACQUIRE);
    if (!b || !n) return false;
    out->bins = b;
    out->n = n;
    out->rate = g_job->rate;
    // the loudness reference from the whole song once it is there; while decoding, from what is there so far
    static uint32_t refFor = 0;
    static const Job* refJob = nullptr;
    static float ref = 1;
    if (refJob != g_job || refFor != n) {
        ref = waveRef(b, n);
        refFor = n;
        refJob = g_job;
    }
    out->ref = ref;
    return true;
}
