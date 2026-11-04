// The system file picker, for the 存档 page's import / export buttons.
//
// iOS: UIDocumentPickerViewController (open a copy / export a copy), driven through the Objective-C runtime
//      (dlsym'd objc_msgSend, a delegate class made at runtime). Unity runs on the main thread there, so it is
//      presented straight from the button.
// Android: the Storage Access Framework (OPEN_DOCUMENT / CREATE_DOCUMENT). A result only reaches an Activity or a
//      Fragment, so a small Java class (android/picker, phime.Picker: a headless Fragment) is carried in this .so as
//      a dex and loaded with InMemoryDexClassLoader (Android 8+); no manifest change, so it works with a pushed
//      mod. JNI by slot number (no jni.h), JavaVM from the loader (PhiMe_JavaVM) or JNI_GetCreatedJavaVMs.
//
// Results come back on another thread / outside the frame; pickerPoll() (per frame) hands them to the caller.
#include "mod.h"

// ---- the result, handed over to the main thread ----
static PickDone g_done = nullptr;
static int g_id = 0, g_mode = 0;
static long g_startedAt = 0;
static int g_ready = 0, g_rStatus = 0;
static char g_rPath[512], g_rName[256];

static void copyStr(char* out, int cap, const char* s) {
    snprintf(out, cap, "%s", s ? s : "");
}
static void post(int id, int status, const char* path, const char* name) {
    if (id != g_id || __atomic_load_n(&g_ready, __ATOMIC_ACQUIRE)) return;
    g_rStatus = status;
    copyStr(g_rPath, sizeof g_rPath, path);
    copyStr(g_rName, sizeof g_rName, name);
    __atomic_store_n(&g_ready, 1, __ATOMIC_RELEASE);
}
void pickerPoll() {
    if (!__atomic_load_n(&g_ready, __ATOMIC_ACQUIRE)) return;
    PickDone d = g_done;
    g_done = nullptr;
    int st = g_rStatus;
    char path[512], name[256];
    copyStr(path, sizeof path, g_rPath);
    copyStr(name, sizeof name, g_rName);
    __atomic_store_n(&g_ready, 0, __ATOMIC_RELEASE);
    LOG("picker: %s -> %d %s %s", g_mode ? "save" : "open", st, path, name);
    if (d) d(st, path[0] ? path : nullptr, name[0] ? name : nullptr);
}
// a new request; 0 = one was started a moment ago (a double tap), ignore this one
static int begin(int mode, PickDone done) {
    long now = time(nullptr);
    if (g_done && now - g_startedAt < 2) return 0;
    __atomic_store_n(&g_ready, 0, __ATOMIC_RELEASE);
    g_done = done;
    g_mode = mode;
    g_startedAt = now;
    return ++g_id;
}
static const char* lastPart(const char* p) {
    const char* n = p;
    for (const char* q = p; *q; q++)
        if (*q == '/') n = q + 1;
    return n;
}

#if defined(__APPLE__)
// ======================= iOS =======================
typedef void* ObjId;
typedef void* SEL;
static struct {
    bool tried, ok;
    ObjId (*getClass)(const char*);
    SEL (*sel)(const char*);
    void* send;
    ObjId (*allocPair)(ObjId, const char*, size_t);
    void (*regPair)(ObjId);
    bool (*addMethod)(ObjId, SEL, void*, const char*);
    void* (*getProto)(const char*);
    bool (*addProto)(ObjId, void*);
    ObjId delegate;
} O;

template <class R = ObjId, class... A>
static R msg(ObjId o, const char* s, A... a) {
    return ((R(*)(ObjId, SEL, A...))O.send)(o, O.sel(s), a...);
}
static ObjId cls(const char* n) { return O.getClass(n); }
static ObjId nsstr(const char* s) { return msg(cls("NSString"), "stringWithUTF8String:", s); }
static ObjId nsarray(ObjId* items, int n) {
    return msg(cls("NSArray"), "arrayWithObjects:count:", items, (unsigned long)n);
}
static const char* urlPath(ObjId url) {
    ObjId p = url ? msg(url, "path") : nullptr;
    return p ? msg<const char*>(p, "UTF8String") : nullptr;
}

static void dPicked(ObjId, SEL, ObjId, ObjId urls) {
    ObjId u = urls && msg<unsigned long>(urls, "count") > 0 ? msg(urls, "firstObject") : nullptr;
    const char* p = urlPath(u);
    if (!p) {
        post(g_id, -1, nullptr, T("系统没有给出文件位置", "The system gave no file location"));
        return;
    }
    post(g_id, 1, g_mode == 0 ? p : nullptr, lastPart(p));
}
static void dCancelled(ObjId, SEL, ObjId) { post(g_id, 0, nullptr, nullptr); }

static bool objcInit() {
    if (O.tried) return O.ok;
    O.tried = true;
    O.getClass = (ObjId(*)(const char*))dlsym(PM_RTLD_DEFAULT, "objc_getClass");
    O.sel = (SEL(*)(const char*))dlsym(PM_RTLD_DEFAULT, "sel_registerName");
    O.send = dlsym(PM_RTLD_DEFAULT, "objc_msgSend");
    O.allocPair = (ObjId(*)(ObjId, const char*, size_t))dlsym(PM_RTLD_DEFAULT, "objc_allocateClassPair");
    O.regPair = (void (*)(ObjId))dlsym(PM_RTLD_DEFAULT, "objc_registerClassPair");
    O.addMethod = (bool (*)(ObjId, SEL, void*, const char*))dlsym(PM_RTLD_DEFAULT, "class_addMethod");
    O.getProto = (void* (*)(const char*))dlsym(PM_RTLD_DEFAULT, "objc_getProtocol");
    O.addProto = (bool (*)(ObjId, void*))dlsym(PM_RTLD_DEFAULT, "class_addProtocol");
    if (!O.getClass || !O.sel || !O.send || !O.allocPair || !O.regPair || !O.addMethod) {
        LOG("picker: objc runtime not found");
        return false;
    }
    if (!cls("UIDocumentPickerViewController") || !cls("UIApplication")) {
        LOG("picker: UIDocumentPickerViewController missing");
        return false;
    }
    ObjId k = O.allocPair(cls("NSObject"), "PhiMePickerDelegate", 0);
    if (k) {
        O.addMethod(k, O.sel("documentPicker:didPickDocumentsAtURLs:"), (void*)dPicked, "v@:@@");
        O.addMethod(k, O.sel("documentPickerWasCancelled:"), (void*)dCancelled, "v@:@");
        void* proto = O.getProto ? O.getProto("UIDocumentPickerDelegate") : nullptr;
        if (proto && O.addProto) O.addProto(k, proto);
        O.regPair(k);
    } else {
        k = cls("PhiMePickerDelegate");
    }
    O.delegate = k ? msg(msg(k, "alloc"), "init") : nullptr;  // kept for good (the picker holds it weakly)
    O.ok = O.delegate != nullptr;
    LOG("picker: iOS document picker %s", O.ok ? "ready" : "FAILED");
    return O.ok;
}

static ObjId topController() {
    ObjId vc = nullptr;
    typedef ObjId (*GetVC)();
    GetVC unityVC = (GetVC)dlsym(PM_RTLD_DEFAULT, "UnityGetGLViewController");
    if (unityVC) vc = unityVC();
    if (!vc) {
        ObjId app = msg(cls("UIApplication"), "sharedApplication");
        ObjId win = app ? msg(app, "keyWindow") : nullptr;
        if (!win && app) win = msg(msg(app, "windows"), "firstObject");
        vc = win ? msg(win, "rootViewController") : nullptr;
    }
    for (int i = 0; vc && i < 8; i++) {
        ObjId p = msg(vc, "presentedViewController");
        if (!p || msg<bool>(p, "isBeingDismissed")) break;
        vc = p;
    }
    return vc;
}

static bool present(ObjId picker) {
    ObjId vc = topController();
    if (!picker || !vc) {
        LOG("picker: nothing to present on");
        if (picker) msg<void>(picker, "release");
        return false;
    }
    msg<void>(picker, "setDelegate:", O.delegate);
    msg<void>(vc, "presentViewController:animated:completion:", picker, true, (ObjId) nullptr);
    msg<void>(picker, "release");  // the presenting controller keeps it
    return true;
}

bool pickerAvailable() { return objcInit(); }

bool pickerOpen(int type, const char*, PickDone done) {
    if (!objcInit()) return false;
    int id = begin(0, done);
    if (!id) return true;
    ObjId P = cls("UIDocumentPickerViewController"), UT = cls("UTType");
    const char* ids[2] = {"public.zip-archive", nullptr};
    if (type == PICK_JSON) ids[0] = "public.json", ids[1] = "public.text";
    ObjId picker = nullptr;
    if (UT && msg<bool>(P, "instancesRespondToSelector:", O.sel("initForOpeningContentTypes:asCopy:"))) {
        ObjId t[2];
        int n = 0;
        for (const char* s : ids)
            if (s)
                if (ObjId u = msg(UT, "typeWithIdentifier:", nsstr(s))) t[n++] = u;
        picker = msg(msg(P, "alloc"), "initForOpeningContentTypes:asCopy:", nsarray(t, n), true);
    } else {
        ObjId t[2];
        int n = 0;
        for (const char* s : ids)
            if (s) t[n++] = nsstr(s);
        picker = msg(msg(P, "alloc"), "initWithDocumentTypes:inMode:", nsarray(t, n), (unsigned long)0);  // Import
    }
    if (!present(picker)) {
        g_done = nullptr;
        return false;
    }
    return true;
}

// The Files app at this folder (shareddocuments://<path>). The app's folder only shows there when the IPA has
// UIFileSharingEnabled + LSSupportsOpeningDocumentsInPlace (package_ipa.ps1 turns both on).
bool openFolder(const char* path) {
    if (!objcInit()) return false;
    char u[700];
    snprintf(u, sizeof u, "shareddocuments://%s", path);
    ObjId s = nsstr(u);
    ObjId allowed = msg(cls("NSCharacterSet"), "URLFragmentAllowedCharacterSet");
    if (allowed) {
        ObjId enc = msg(s, "stringByAddingPercentEncodingWithAllowedCharacters:", allowed);
        if (enc) s = enc;
    }
    ObjId url = msg(cls("NSURL"), "URLWithString:", s);
    ObjId app = msg(cls("UIApplication"), "sharedApplication");
    if (!url || !app) return false;
    ObjId opts = msg(cls("NSDictionary"), "dictionary");
    msg<void>(app, "openURL:options:completionHandler:", url, opts, (ObjId) nullptr);
    LOG("folder: asked the Files app to open %s", u);
    return true;
}

bool pickerSave(const char* path, int, PickDone done) {
    if (!objcInit()) return false;
    int id = begin(1, done);
    if (!id) return true;
    ObjId P = cls("UIDocumentPickerViewController");
    ObjId url = msg(cls("NSURL"), "fileURLWithPath:", nsstr(path));
    ObjId picker;
    if (msg<bool>(P, "instancesRespondToSelector:", O.sel("initForExportingURLs:asCopy:"))) {
        ObjId one[1] = {url};
        picker = msg(msg(P, "alloc"), "initForExportingURLs:asCopy:", nsarray(one, 1), true);
    } else {
        picker = msg(msg(P, "alloc"), "initWithURL:inMode:", url, (unsigned long)2);  // ExportToService
    }
    if (!present(picker)) {
        g_done = nullptr;
        return false;
    }
    return true;
}

#else
// ======================= Android =======================
#include "../android/picker/picker_dex.inc"
#include "jni_lite.h"

static void* g_vm = nullptr;
static void* javaVM() {
    if (g_vm) return g_vm;
    typedef void* (*LoaderVM)();
    void* h = dlopen("libmain.so", PM_RTLD_NOW | PM_RTLD_NOLOAD);  // our loader keeps it (newer APKs)
    LoaderVM lv = h ? (LoaderVM)dlsym(h, "PhiMe_JavaVM") : nullptr;
    if (lv) g_vm = lv();
    if (!g_vm) {
        typedef int (*Created)(void**, int, int*);
        const char* libs[] = {nullptr, "libnativehelper.so", "libart.so"};  // public since Android 12 / older
        for (const char* l : libs) {
            void* lib = l ? dlopen(l, PM_RTLD_NOW) : PM_RTLD_DEFAULT;
            Created c = lib || !l ? (Created)dlsym(lib, "JNI_GetCreatedJavaVMs") : nullptr;
            int n = 0;
            if (c && c(&g_vm, 1, &n) == 0 && n > 0 && g_vm) break;
            g_vm = nullptr;
        }
    }
    LOG("picker: JavaVM %s", g_vm ? "found" : "NOT found");
    return g_vm;
}
static bool envNow(J& j) {
    void* vm = javaVM();
    if (!vm) return false;
    void** fns = *(void***)vm;
    if (((int (*)(void*, void**, int))fns[6])(vm, &j.e, 0x10006) != 0) {  // GetEnv
        j.e = nullptr;
        if (((int (*)(void*, void**, void*))fns[4])(vm, &j.e, nullptr) != 0) j.e = nullptr;  // AttachCurrentThread
    }
    return j.e != nullptr;
}

// phime.Picker.result(int id, int status, String path, String name), on a Java thread
static void jniResult(void* env, JObj, int id, int status, JObj path, JObj name) {
    J j;
    j.e = env;
    const char* p = j.chars(path);
    const char* n = j.chars(name);
    post(id, status, p, n);
    j.release(path, p);
    j.release(name, n);
}

bool jniEnv(J& j) { return envNow(j); }

static JObj g_cls = nullptr, g_start = nullptr, g_loader = nullptr;
static bool g_tried = false, g_ok = false;

static JObj activity(J& j) {
    JObj up = j.findClass("com/unity3d/player/UnityPlayer");
    if (j.exc() || !up) return nullptr;
    JObj fid = j.staticField(up, "currentActivity", "Landroid/app/Activity;");
    if (j.exc() || !fid) return nullptr;
    JObj a = j.getStatic(up, fid);
    return j.exc() ? nullptr : a;
}

static bool loadPicker(J& j) {
    JObj act = activity(j);
    if (!act) {
        LOG("picker: no Unity activity");
        return false;
    }
    JObj ctx = j.findClass("android/content/Context");
    JObj getCL = ctx ? j.method(ctx, "getClassLoader", "()Ljava/lang/ClassLoader;") : nullptr;
    JObj parent = getCL ? j.callObj(act, getCL, nullptr) : nullptr;
    if (j.exc() || !parent) return false;
    JObj imd = j.findClass("dalvik/system/InMemoryDexClassLoader");
    if (j.exc() || !imd) {
        LOG("picker: InMemoryDexClassLoader missing (Android 8+ only), using the import folder");
        return false;
    }
    JObj ctor = j.method(imd, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    void* dex = malloc(sizeof kPickerDex);  // kept for good: ART may read the buffer later
    if (!ctor || j.exc() || !dex) return false;
    memcpy(dex, kPickerDex, sizeof kPickerDex);
    JValue a[2];
    a[0].l = j.directBuffer(dex, (long)sizeof kPickerDex);
    a[1].l = parent;
    JObj loader = a[0].l ? j.newObject(imd, ctor, a) : nullptr;
    if (j.exc() || !loader) return false;
    g_loader = j.globalRef(loader);  // phime.Cloud lives in the same dex
    JObj clCls = j.findClass("java/lang/ClassLoader");
    JObj loadClass = clCls ? j.method(clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
    if (j.exc() || !loadClass) return false;
    JValue n[1];
    n[0].l = j.newString("phime.Picker");
    JObj k = j.callObj(loader, loadClass, n);
    if (j.exc() || !k) return false;
    JNINativeMethod nm = {"result", "(IILjava/lang/String;Ljava/lang/String;)V", (void*)jniResult};
    if (j.registerNatives(k, &nm, 1) != 0 || j.exc()) return false;
    g_start = j.staticMethod(k, "start", "(Landroid/app/Activity;II[Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    if (j.exc() || !g_start) return false;
    g_cls = j.globalRef(k);
    return g_cls != nullptr;
}

static bool setup(J& j) {
    if (g_tried) return g_ok;
    g_tried = true;
    if (j.pushFrame(32) != 0) {
        j.exc();
        return false;
    }
    g_ok = loadPicker(j);
    j.popFrame();
    LOG("picker: Android document picker %s (dex %d bytes)", g_ok ? "ready" : "FAILED", (int)sizeof kPickerDex);
    return g_ok;
}

bool pickerAvailable() {
    J j;
    return envNow(j) && setup(j);
}

JObj jniActivity(J& j) { return activity(j); }

JObj jniModClass(J& j, const char* name) {
    if (!setup(j) || !g_loader) return nullptr;
    JObj clCls = j.findClass("java/lang/ClassLoader");
    JObj loadClass = clCls ? j.method(clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
    if (j.exc() || !loadClass) return nullptr;
    JValue n[1];
    n[0].l = j.newString(name);
    JObj k = j.callObj(g_loader, loadClass, n);
    if (j.exc() || !k) return nullptr;
    return j.globalRef(k);
}

static bool startJava(int mode, const char* const* mimes, int nm, const char* name, const char* path, PickDone done) {
    J j;
    if (!envNow(j) || !setup(j)) return false;
    int id = begin(mode, done);
    if (!id) return true;
    if (j.pushFrame(16) != 0) {
        j.exc();
        g_done = nullptr;
        return false;
    }
    bool ok = false;
    JObj act = activity(j);
    JObj strCls = j.findClass("java/lang/String");
    JObj arr = strCls ? j.newArray(nm, strCls) : nullptr;
    if (act && arr && !j.exc()) {
        for (int i = 0; i < nm; i++) j.setElement(arr, i, j.newString(mimes[i]));
        JValue a[6];
        a[0].l = act;
        a[1].i = id;
        a[2].i = mode;
        a[3].l = arr;
        a[4].l = j.newString(name);
        a[5].l = j.newString(path);
        j.callStaticVoid(g_cls, g_start, a);
        ok = !j.exc();
    }
    j.popFrame();
    if (!ok) g_done = nullptr;
    return ok;
}

bool pickerOpen(int type, const char* copyTo, PickDone done) {
    static const char* const zip[] = {"application/zip", "application/x-zip-compressed", "application/octet-stream"};
    static const char* const json[] = {"application/json", "text/plain", "application/octet-stream"};
    return startJava(0, type == PICK_JSON ? json : zip, 3, "", copyTo, done);
}

bool pickerSave(const char* path, int type, PickDone done) {
    const char* mime = type == PICK_JSON ? "application/json" : "application/zip";
    return startJava(1, &mime, 1, lastPart(path), path, done);
}

// A file manager at this folder: ACTION_VIEW on the external storage provider's document for it (the system Files
// app takes a directory document; MT and others often take resource/folder), else the storage root.
bool openFolder(const char* path) {
    J j;
    if (!envNow(j)) return false;
    size_t pl = strlen(path);
    const char* rel = pl > 8 && !memcmp(path, "/sdcard/", 8)                 ? path + 8
                      : pl > 20 && !memcmp(path, "/storage/emulated/0/", 20) ? path + 20
                                                                             : nullptr;
    if (!rel) return false;
    if (j.pushFrame(32) != 0) {
        j.exc();
        return false;
    }
    bool ok = false;
    const char* how = "";
    JObj act = activity(j);
    JObj dc = j.findClass("android/provider/DocumentsContract");
    JObj ic = dc && !j.exc() ? j.findClass("android/content/Intent") : nullptr;
    if (act && ic && !j.exc()) {
        JObj buildDoc = j.staticMethod(dc, "buildDocumentUri", "(Ljava/lang/String;Ljava/lang/String;)Landroid/net/Uri;");
        JObj buildRoot = j.staticMethod(dc, "buildRootUri", "(Ljava/lang/String;Ljava/lang/String;)Landroid/net/Uri;");
        JObj ctor = j.method(ic, "<init>", "(Ljava/lang/String;)V");
        JObj setDT = j.method(ic, "setDataAndType", "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;");
        JObj addFlags = j.method(ic, "addFlags", "(I)Landroid/content/Intent;");
        JObj ac = j.findClass("android/app/Activity");
        JObj start = ac ? j.method(ac, "startActivity", "(Landroid/content/Intent;)V") : nullptr;
        if (!j.exc() && buildDoc && buildRoot && ctor && setDT && addFlags && start) {
            char doc[600];
            snprintf(doc, sizeof doc, "primary:%s", rel);
            JValue a[2];
            a[0].l = j.newString("com.android.externalstorage.documents");
            a[1].l = j.newString(doc);
            JObj docUri = j.callStaticObj(dc, buildDoc, a);
            a[1].l = j.newString("primary");
            JObj rootUri = j.exc() ? nullptr : j.callStaticObj(dc, buildRoot, a);
            j.exc();
            struct Try {
                JObj uri;
                const char* mime;
            } tries[3] = {{docUri, "vnd.android.document/directory"}, {docUri, "resource/folder"},
                          {rootUri, "vnd.android.document/root"}};
            for (const Try& t : tries) {
                if (!t.uri) continue;
                JValue v[2];
                v[0].l = j.newString("android.intent.action.VIEW");
                JObj in = j.newObject(ic, ctor, v);
                if (j.exc() || !in) continue;
                v[0].l = t.uri;
                v[1].l = j.newString(t.mime);
                j.callObj(in, setDT, v);
                v[0].i = 0x10000000 | 1 | 2;  // NEW_TASK | GRANT_READ | GRANT_WRITE
                j.callObj(in, addFlags, v);
                if (j.exc()) continue;
                v[0].l = in;
                j.callVoid(act, start, v);
                if (!j.exc()) {
                    ok = true;
                    how = t.mime;
                    break;
                }
            }
        }
    }
    j.exc();
    j.popFrame();
    LOG("folder: %s %s%s", path, ok ? "opened as " : "could not be opened", how);
    return ok;
}
#endif
