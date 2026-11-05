// iOS side of the TapTap QR login (cloud.cpp has the cards; android/picker/src/phime/Cloud.java is the Android twin).
// Apple-ID signing changes the bundle ID, and the game's own TapTap login then fails with "-101 sdk_not_matched".
// The same calls the community tools use, natively:
//   TapTap device login: POST oauth2/v1/device/code -> a QR link; poll POST oauth2/v1/token until the user confirms
//     in TapTap -> kid + mac_key; GET open.tapapis.cn/account/basic-info/v1 (MAC signed) -> openid + unionid
//   LeanCloud (the game's save server): POST 1.1/users with that authData -> sessionToken; GET 1.1/users/me checks it
// One background thread per login; events go to cloud.cpp (cloudPush). The sessionToken is kept in
// Library/PhiMeCloud.txt (app-private; Documents is shared through the Files app), so the next login needs no scan.
// HTTP is NSURLSession through the Objective-C runtime, everything looked up with dlsym (no new link stubs): one
// request at a time, its completion handler a hand-made global block.
#include "mod.h"

#if defined(__APPLE__)
char* qrEncode(const char* text);  // qr.cpp
void cloudPush(int id, int kind, const char* a, const char* b);  // cloud.cpp
extern "C" int unlink(const char* path);

namespace {

enum { EV_FAIL = -1, EV_CANCELLED = 0, EV_QR = 2, EV_PROGRESS = 3, EV_TOKEN = 4 };
const char* CLIENT = "rAK3FfdieFob2Nn8Am";
const char* LC_KEY = "Qr9AEqtuoSVS3zeD6iVbM4ZC0AtkJcQ89tywVyi0";
const char* LC = "https://rak3ffdi.cloud.tds1.tapapis.cn/1.1/";
const char* TAP_UA = "TapTapAndroidSDK/3.16.5";  // the same as the Android side (the server accepts it there)
const char* LC_UA = "LeanCloud-CSharp-SDK/1.0.3";
const char* CODE_URLS[2] = {"https://www.taptap.cn/oauth2/v1/device/code", "https://accounts.tapapis.cn/oauth2/v1/device/code"};
const char* TOKEN_URLS[2] = {"https://www.taptap.com/oauth2/v1/token", "https://accounts.tapapis.cn/oauth2/v1/token"};

// ---------------- Objective-C runtime ----------------
typedef void* id;
typedef void* SEL;
struct {
    bool tried, ok;
    id (*getClass)(const char*);
    SEL (*sel)(const char*);
    void* msgSend;
    void* (*poolPush)();
    void (*poolPop)(void*);
    void* globalBlock;
    void (*hmac)(uint32_t alg, const void* key, size_t keyLen, const void* data, size_t len, void* out);
    uint32_t (*random)();
} O;

bool objcReady() {
    if (O.tried) return O.ok;
    O.tried = true;
    void* h = PM_RTLD_DEFAULT;
    O.getClass = (id(*)(const char*))dlsym(h, "objc_getClass");
    O.sel = (SEL(*)(const char*))dlsym(h, "sel_registerName");
    O.msgSend = dlsym(h, "objc_msgSend");
    O.poolPush = (void* (*)())dlsym(h, "objc_autoreleasePoolPush");
    O.poolPop = (void (*)(void*))dlsym(h, "objc_autoreleasePoolPop");
    O.globalBlock = dlsym(h, "_NSConcreteGlobalBlock");
    O.hmac = (void (*)(uint32_t, const void*, size_t, const void*, size_t, void*))dlsym(h, "CCHmac");
    O.random = (uint32_t(*)())dlsym(h, "arc4random");
    O.ok = O.getClass && O.sel && O.msgSend && O.poolPush && O.poolPop && O.globalBlock && O.hmac && O.random &&
           O.getClass("NSURLSession");
    LOG("cloud(iOS): runtime %s", O.ok ? "ready" : "MISSING (no login)");
    return O.ok;
}
// objc_msgSend called with the method's exact prototype (arm64 needs the real types)
template <typename R, typename... A>
R msg(id obj, const char* selector, A... a) {
    return ((R(*)(id, SEL, A...))O.msgSend)(obj, O.sel(selector), a...);
}
id cls(const char* n) { return O.getClass(n); }
id nsstr(const char* s) { return msg<id>(cls("NSString"), "stringWithUTF8String:", s); }
const char* cstr(id s) { return s ? msg<const char*>(s, "UTF8String") : nullptr; }

// ---------------- HTTP ----------------
struct Resp {
    int code;  // HTTP status, -1 = no answer (err says why)
    char* body;
    int len;
    char err[200];
};
void respFree(Resp& r) {
    free(r.body);
    r.body = nullptr;
}
Resp* volatile g_out = nullptr;
volatile int g_done = 0;
Resp g_late;  // a late answer after we gave up lands here
int g_httpLock = 0;

// the completion handler: void (^)(NSData*, NSURLResponse*, NSError*)
struct BlockDesc {
    unsigned long reserved, size;
};
struct Block {
    void* isa;
    int flags, reserved;
    void (*invoke)(Block*, id, id, id);
    BlockDesc* desc;
};
void onHttpDone(Block*, id data, id resp, id error) {
    Resp* r = g_out;
    if (data) {
        const void* b = msg<const void*>(data, "bytes");
        unsigned long n = msg<unsigned long>(data, "length");
        r->body = (char*)malloc(n + 1);
        if (r->body) {
            if (n) memcpy(r->body, b, n);
            r->body[n] = 0;
            r->len = (int)n;
        }
    }
    if (resp && msg<bool>(resp, "respondsToSelector:", O.sel("statusCode"))) r->code = (int)msg<long>(resp, "statusCode");
    if (error) {
        const char* d = cstr(msg<id>(error, "localizedDescription"));
        snprintf(r->err, sizeof r->err, "%s", d ? d : "network error");
        if (!r->code) r->code = -1;
    }
    __atomic_store_n(&g_done, 1, __ATOMIC_RELEASE);
}
BlockDesc g_desc = {0, sizeof(Block)};
Block g_block = {nullptr, 1 << 28 /* BLOCK_IS_GLOBAL */, 0, onHttpDone, &g_desc};

// headers: name, value pairs, null-terminated
Resp http(const char* method, const char* url, const char* const* headers, const char* body) {
    Resp r = {};
    while (__atomic_exchange_n(&g_httpLock, 1, __ATOMIC_ACQUIRE)) usleep(1000);
    void* pool = O.poolPush();
    id u = msg<id>(cls("NSURL"), "URLWithString:", nsstr(url));
    if (!u) {
        r.code = -1;
        snprintf(r.err, sizeof r.err, "bad URL");
    } else {
        id req = msg<id>(cls("NSMutableURLRequest"), "requestWithURL:", u);
        msg<void>(req, "setHTTPMethod:", nsstr(method));
        msg<void>(req, "setTimeoutInterval:", 20.0);
        for (int i = 0; headers && headers[i] && headers[i + 1]; i += 2)
            msg<void>(req, "setValue:forHTTPHeaderField:", nsstr(headers[i + 1]), nsstr(headers[i]));
        if (body) {
            id d = msg<id>(cls("NSData"), "dataWithBytes:length:", (const void*)body, (unsigned long)strlen(body));
            msg<void>(req, "setHTTPBody:", d);
        }
        g_block.isa = O.globalBlock;
        g_out = &r;
        __atomic_store_n(&g_done, 0, __ATOMIC_RELEASE);
        id session = msg<id>(cls("NSURLSession"), "sharedSession");
        id task = msg<id>(session, "dataTaskWithRequest:completionHandler:", req, (void*)&g_block);
        msg<void>(task, "resume");
        int waited = 0;
        while (!__atomic_load_n(&g_done, __ATOMIC_ACQUIRE) && waited < 25000) {
            usleep(10000);
            waited += 10;
        }
        if (!__atomic_load_n(&g_done, __ATOMIC_ACQUIRE)) {
            msg<void>(task, "cancel");
            for (int i = 0; i < 300 && !__atomic_load_n(&g_done, __ATOMIC_ACQUIRE); i++) usleep(10000);
            if (!__atomic_load_n(&g_done, __ATOMIC_ACQUIRE)) g_out = &g_late;  // never write into our stack later
            r.code = -1;
            snprintf(r.err, sizeof r.err, "timed out");
        }
    }
    O.poolPop(pool);
    __atomic_store_n(&g_httpLock, 0, __ATOMIC_RELEASE);
    return r;
}

// ---------------- small helpers ----------------
// the value of the first "key" in a JSON text: strings unescaped (\uXXXX -> UTF-8), other values as written
bool jget(const char* json, const char* key, char* out, int cap) {
    out[0] = 0;
    if (!json) return false;
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    size_t pl = strlen(pat);
    for (const char* p = json; *p; p++) {
        if (memcmp(p, pat, pl)) continue;
        const char* q = p + pl;
        while (*q == ' ' || *q == '\n' || *q == '\r' || *q == '\t') q++;
        if (*q != ':') continue;
        q++;
        while (*q == ' ' || *q == '\n' || *q == '\r' || *q == '\t') q++;
        int n = 0;
        if (*q == '"') {
            for (q++; *q && *q != '"' && n < cap - 4; q++) {
                if (*q != '\\') {
                    out[n++] = *q;
                    continue;
                }
                q++;
                if (*q == 'u') {
                    unsigned cp = 0;
                    for (int i = 1; i <= 4 && q[i]; i++) {
                        char c = q[i];
                        cp = cp * 16 + (c >= '0' && c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
                    }
                    q += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && q[1] == '\\' && q[2] == 'u') {  // surrogate pair
                        unsigned lo = 0;
                        for (int i = 3; i <= 6 && q[i]; i++) {
                            char c = q[i];
                            lo = lo * 16 + (c >= '0' && c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        q += 6;
                    }
                    if (cp < 0x80) out[n++] = (char)cp;
                    else if (cp < 0x800 && n < cap - 3) {
                        out[n++] = (char)(0xC0 | cp >> 6);
                        out[n++] = (char)(0x80 | (cp & 63));
                    } else if (cp < 0x10000 && n < cap - 4) {
                        out[n++] = (char)(0xE0 | cp >> 12);
                        out[n++] = (char)(0x80 | ((cp >> 6) & 63));
                        out[n++] = (char)(0x80 | (cp & 63));
                    } else if (n < cap - 5) {
                        out[n++] = (char)(0xF0 | cp >> 18);
                        out[n++] = (char)(0x80 | ((cp >> 12) & 63));
                        out[n++] = (char)(0x80 | ((cp >> 6) & 63));
                        out[n++] = (char)(0x80 | (cp & 63));
                    }
                } else {
                    out[n++] = *q == 'n' ? '\n' : *q == 't' ? '\t' : *q;
                }
            }
            if (*q && *q != '"') LOG("cloud(iOS): the value of %s was cut at %d bytes (buffer too small)", key, n);
        } else {
            while (*q && *q != ',' && *q != '}' && *q != ']' && *q != ' ' && n < cap - 1) out[n++] = *q++;
        }
        out[n] = 0;
        return true;
    }
    return false;
}
int jint(const char* json, const char* key, int def) {
    char b[24];
    if (!jget(json, key, b, sizeof b) || !b[0]) return def;
    int v = 0, s = 1, i = 0;
    if (b[0] == '-') s = -1, i = 1;
    for (; b[i] >= '0' && b[i] <= '9'; i++) v = v * 10 + (b[i] - '0');
    return v * s;
}
void urlEnc(const char* s, char* out, int cap) {
    static const char* hex = "0123456789ABCDEF";
    int n = 0;
    for (; *s && n < cap - 4; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '*')
            out[n++] = (char)c;
        else if (c == ' ')
            out[n++] = '+';
        else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        }
    }
    out[n] = 0;
}
void base64(const uint8_t* in, int n, char* out) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        unsigned v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = t[v >> 18];
        out[o++] = t[(v >> 12) & 63];
        out[o++] = i + 1 < n ? t[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
}
// a JSON string literal for a value (quotes and backslashes escaped)
void jquote(const char* s, char* out, int cap) {
    int n = 0;
    out[n++] = '"';
    for (; *s && n < cap - 3; s++) {
        if (*s == '"' || *s == '\\') out[n++] = '\\';
        out[n++] = *s;
    }
    out[n++] = '"';
    out[n] = 0;
}

// ---------------- files (Library: private to the app) ----------------
void storePath(char* out, int cap, const char* name) {
    const char* home = getenv("HOME");
    snprintf(out, cap, "%s/Library/%s", home ? home : ".", name);
}
bool readText(const char* name, char* out, int cap) {
    char p[512];
    storePath(p, sizeof p, name);
    out[0] = 0;
    int fd = open(p, 0);
    if (fd < 0) return false;
    ssize_t n = read(fd, out, cap - 1);
    close(fd);
    if (n <= 0) return false;
    out[n] = 0;
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == ' ' || out[n - 1] == '\r')) out[--n] = 0;
    return n > 0;
}
void writeText(const char* name, const char* s) {
    char p[512];
    storePath(p, sizeof p, name);
    int fd = open(p, PM_O_REPLACE, 0600);
    if (fd < 0) return;
    write(fd, s, strlen(s));
    close(fd);
}
void deviceId(char* out) {  // 32 hex digits, made once
    if (readText("PhiMeCloudDevice.txt", out, 40) && strlen(out) >= 8) return;
    for (int i = 0; i < 4; i++) snprintf(out + i * 8, 9, "%08x", O.random());
    writeText("PhiMeCloudDevice.txt", out);
}

// ---------------- the login job ----------------
volatile int g_current = 0;  // the running job; anything else = cancelled

struct Fail {
    char why[400];
};
bool alive(int id) { return __atomic_load_n(&g_current, __ATOMIC_ACQUIRE) == id; }
void progress(int id, const char* s) {
    if (alive(id)) cloudPush(id, EV_PROGRESS, nullptr, s);
}
const char* head(const Resp& r) {  // the start of an answer, for messages
    static char b[130];
    snprintf(b, sizeof b, "%s", r.body ? r.body : r.err);
    for (char* p = b; *p; p++)
        if (*p == '\n') *p = ' ';
    return b;
}
// sleep in small steps; false = cancelled
bool nap(int id, int ms) {
    for (int t = 0; t < ms; t += 200) {
        if (!alive(id)) return false;
        usleep(200000);
    }
    return alive(id);
}

// users/me for this token: 1 valid (nick filled), 0 not valid any more, -1 the server failed (f filled)
int me(const char* token, char* nick, int nickCap, Fail& f) {
    const char* h[] = {"X-LC-Id", CLIENT, "X-LC-Key", LC_KEY, "User-Agent", LC_UA, "Accept", "application/json",
                       "X-LC-Session", token, nullptr};
    char url[200];
    snprintf(url, sizeof url, "%susers/me", LC);
    Resp r = http("GET", url, h, nullptr);
    char oid[64];
    int res;
    if (r.code == 200 && jget(r.body, "objectId", oid, sizeof oid) && oid[0]) {
        jget(r.body, "nickname", nick, nickCap);
        res = 1;
    } else if (r.code >= 400 && r.code < 500) {
        res = 0;
    } else {
        snprintf(f.why, sizeof f.why, T("云存档服务器出错了（HTTP %d %s）。", "The save server failed (HTTP %d %s)."), r.code, head(r));
        res = -1;
    }
    respFree(r);
    return res;
}

// TapTap device login -> a LeanCloud sessionToken; false = failed (f) or cancelled
// the long strings of one login (one block per login thread: a cancelled login may still be running)
struct LoginBufs {
    char kid[2048], macKey[512], access[2048], authz[2600], openid[300], unionid[300];
    char qk[2100], qa[2100], qm[600], qo[320], qu[320], auth[6000];
};
struct FreeBufs {
    LoginBufs* p;
    ~FreeBufs() { free(p); }
};
bool login(int id, char* token, int tokenCap, Fail& f) {
    LoginBufs* B = (LoginBufs*)calloc(1, sizeof(LoginBufs));
    FreeBufs keepUntilReturn{B};
    if (!B) {
        snprintf(f.why, sizeof f.why, "%s", T("内存不足。", "Out of memory."));
        return false;
    }
    progress(id, T("正在向 TapTap 申请登录二维码…", "Asking TapTap for a login code…"));
    char dev[48], infoRaw[96], info[200];
    deviceId(dev);
    snprintf(infoRaw, sizeof infoRaw, "{\"device_id\":\"%s\"}", dev);
    urlEnc(infoRaw, info, sizeof info);
    const char* tap[] = {"Content-Type", "application/x-www-form-urlencoded", "User-Agent", TAP_UA, nullptr};
    char body[600], code[256] = "", link[600] = "", err[200] = "";
    int interval = 5, life = 300;
    snprintf(body, sizeof body, "client_id=%s&response_type=device_code&scope=basic_info&version=1.2.0&platform=unity&info=%s",
             CLIENT, info);
    for (int u = 0; u < 2 && !code[0]; u++) {
        Resp r = http("POST", CODE_URLS[u], tap, body);
        char ok[16];
        if (r.code == 200 && jget(r.body, "success", ok, sizeof ok) && !strcmp(ok, "true") &&
            jget(r.body, "device_code", code, sizeof code) && code[0]) {
            if (!jget(r.body, "qrcode_url", link, sizeof link) || !link[0]) jget(r.body, "verification_url", link, sizeof link);
            interval = jint(r.body, "interval", 5);
            life = jint(r.body, "expires_in", 300);
        } else {
            code[0] = 0;
            snprintf(err, sizeof err, "HTTP %d %s", r.code, head(r));
        }
        respFree(r);
        if (!alive(id)) return false;
    }
    if (!code[0] || !link[0]) {
        snprintf(f.why, sizeof f.why, T("TapTap 没给登录二维码（%s）。接口可能变了。", "TapTap gave no login code (%s). The API may have changed."), err);
        return false;
    }
    if (interval < 2) interval = 2;
    char* qr = qrEncode(link);
    if (alive(id)) cloudPush(id, EV_QR, link, qr);
    free(qr);

    char codeEnc[400];
    urlEnc(code, codeEnc, sizeof codeEnc);
    snprintf(body, sizeof body, "grant_type=device_token&client_id=%s&secret_type=hmac-sha-1&code=%s&version=1.0&platform=unity&info=%s",
             CLIENT, codeEnc, info);
    long deadline = time(nullptr) + life;
    int host = 0, netErrors = 0;
    bool scanned = false;
    // kid and access_token are long (200+ characters): 1.2.0 cut kid at 127 and TapTap answered 400 InvalidArgument
    char(&kid)[2048] = B->kid;
    char(&macKey)[512] = B->macKey;
    char(&access)[2048] = B->access;
    char lastOther[64] = "";
    while (!macKey[0]) {
        if (!nap(id, interval * 1000)) return false;
        if (time(nullptr) > deadline) {
            snprintf(f.why, sizeof f.why, "%s", T("二维码过期了，再点一次「登录」换个新的吧。", "The code expired; tap Log in again for a new one."));
            return false;
        }
        Resp r = http("POST", TOKEN_URLS[host], tap, body);
        if (!r.body || r.body[0] != '{') {
            char e[160];
            snprintf(e, sizeof e, "HTTP %d %s", r.code, head(r));
            respFree(r);
            if (++netErrors >= 3) {
                if (host + 1 < 2) {
                    host++;
                    netErrors = 0;
                    continue;
                }
                snprintf(f.why, sizeof f.why, T("等待授权时网络出错了：%s", "Network error while waiting: %s"), e);
                return false;
            }
            continue;
        }
        netErrors = 0;
        char ok[16] = "", e[64] = "";
        jget(r.body, "success", ok, sizeof ok);
        if (!strcmp(ok, "true") && jget(r.body, "mac_key", macKey, sizeof macKey) && macKey[0]) {
            jget(r.body, "kid", kid, sizeof kid);
            jget(r.body, "access_token", access, sizeof access);
            respFree(r);
            break;
        }
        macKey[0] = 0;
        jget(r.body, "error", e, sizeof e);
        respFree(r);
        if (!strcmp(e, "authorization_pending")) continue;
        if (!strcmp(e, "authorization_waiting")) {
            if (!scanned) progress(id, T("已扫码，请在 TapTap 里点确认授权…", "Scanned; confirm in TapTap…"));
            scanned = true;
            continue;
        }
        if (!strcmp(e, "slow_down")) {
            interval += 2;
            continue;
        }
        if (!strcmp(e, "expired_token") || !strcmp(e, "invalid_grant")) {
            snprintf(f.why, sizeof f.why, "%s", T("二维码过期了，再点一次「登录」换个新的吧。", "The code expired; tap Log in again for a new one."));
            return false;
        }
        if (!strcmp(e, "access_denied")) {
            snprintf(f.why, sizeof f.why, "%s", T("TapTap 里拒绝了授权。", "Authorization was declined in TapTap."));
            return false;
        }
        if (strcmp(e, lastOther)) {  // a state not seen before: keep waiting until the code expires
            snprintf(lastOther, sizeof lastOther, "%s", e);
            char m[120];
            snprintf(m, sizeof m, T("TapTap：%s…", "TapTap: %s…"), e[0] ? e : T("等待中", "waiting"));
            progress(id, m);
        }
    }

    progress(id, T("已授权，正在登录云存档…", "Authorized; logging in to the save server…"));
    if (!access[0]) snprintf(access, sizeof access, "%s", kid);
    LOG("cloud(iOS): authorized (kid %d chars, access_token %d, mac_key %d)", (int)strlen(kid), (int)strlen(access),
        (int)strlen(macKey));
    // the account's openid / unionid, MAC signed
    char(&authz)[2600] = B->authz;
    char path[120], s[400], sig[64];
    snprintf(path, sizeof path, "/account/basic-info/v1?client_id=%s", CLIENT);
    long ts = time(nullptr);
    unsigned nonce = O.random();
    snprintf(s, sizeof s, "%ld\n%u\nGET\n%s\nopen.tapapis.cn\n443\n\n", ts, nonce, path);
    uint8_t mac[20];
    O.hmac(0 /* kCCHmacAlgSHA1 */, macKey, strlen(macKey), s, strlen(s), mac);
    base64(mac, 20, sig);
    snprintf(authz, sizeof authz, "MAC id=\"%s\",ts=\"%ld\",nonce=\"%u\",mac=\"%s\"", kid, ts, nonce, sig);
    char url[200];
    snprintf(url, sizeof url, "https://open.tapapis.cn%s", path);
    const char* h1[] = {"User-Agent", TAP_UA, "Authorization", authz, nullptr};
    Resp r = http("GET", url, h1, nullptr);
    char(&openid)[300] = B->openid;
    char(&unionid)[300] = B->unionid;
    jget(r.body, "openid", openid, sizeof openid);
    jget(r.body, "unionid", unionid, sizeof unionid);
    if (!openid[0]) {
        LOG("cloud(iOS): basic-info answered HTTP %d: %.600s", r.code, r.body ? r.body : r.err);
        snprintf(f.why, sizeof f.why, T("读不到 TapTap 账号信息（HTTP %d %s）。", "Could not read the TapTap account (HTTP %d %s)."), r.code, head(r));
        respFree(r);
        return false;
    }
    respFree(r);
    if (!alive(id)) return false;
    // LeanCloud: authData -> sessionToken
    char(&qk)[2100] = B->qk;
    char(&qa)[2100] = B->qa;
    char(&qm)[600] = B->qm;
    char(&qo)[320] = B->qo;
    char(&qu)[320] = B->qu;
    char(&auth)[6000] = B->auth;
    jquote(kid, qk, sizeof qk);
    jquote(access, qa, sizeof qa);
    jquote(macKey, qm, sizeof qm);
    jquote(openid, qo, sizeof qo);
    jquote(unionid, qu, sizeof qu);
    snprintf(auth, sizeof auth,
             "{\"authData\":{\"taptap\":{\"kid\":%s,\"access_token\":%s,\"token_type\":\"mac\",\"mac_key\":%s,"
             "\"mac_algorithm\":\"hmac-sha-1\",\"openid\":%s,\"unionid\":%s}}}",
             qk, qa, qm, qo, qu);
    const char* h2[] = {"X-LC-Id", CLIENT, "X-LC-Key", LC_KEY, "User-Agent", LC_UA, "Content-Type", "application/json",
                        "Accept", "application/json", nullptr};
    snprintf(url, sizeof url, "%susers", LC);
    r = http("POST", url, h2, auth);
    jget(r.body, "sessionToken", token, tokenCap);
    if (!token[0]) {
        LOG("cloud(iOS): users answered HTTP %d: %.600s", r.code, r.body ? r.body : r.err);
        snprintf(f.why, sizeof f.why, T("云存档服务器没有给登录凭证（HTTP %d %s）。", "The save server gave no token (HTTP %d %s)."), r.code, head(r));
        respFree(r);
        return false;
    }
    respFree(r);
    return true;
}

void* job(void* arg) {
    int id = (int)(intptr_t)arg;
    Fail f;
    f.why[0] = 0;
    char token[128] = "", nick[160] = "";
    bool ok = false;
    if (readText("PhiMeCloud.txt", token, sizeof token) && strlen(token) >= 20) {
        progress(id, T("正在连接云端…", "Connecting…"));
        int m = me(token, nick, sizeof nick, f);
        if (m == 1) ok = true;
        else if (m == 0) {
            char p[512];
            storePath(p, sizeof p, "PhiMeCloud.txt");
            unlink(p);  // expired: log in again
            token[0] = 0;
            LOG("cloud(iOS): the saved login has expired, a new scan is needed");
        }
    } else {
        token[0] = 0;
    }
    if (!ok && !f.why[0] && alive(id)) {
        if (login(id, token, sizeof token, f)) {
            int m = me(token, nick, sizeof nick, f);
            if (m == 1) ok = true;
            else if (m == 0) snprintf(f.why, sizeof f.why, "%s", T("登录成功了，但云端不认这个账号的凭证。", "Logged in, but the save server does not accept this account's token."));
        }
    }
    if (ok) writeText("PhiMeCloud.txt", token);
    if (!alive(id)) {
        cloudPush(id, EV_CANCELLED, nullptr, nullptr);
    } else if (ok) {
        cloudPush(id, EV_TOKEN, token, nick);
    } else {
        cloudPush(id, EV_FAIL, nullptr, f.why[0] ? f.why : T("登录失败了。", "The login failed."));
    }
    __atomic_compare_exchange_n(&g_current, &id, 0, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return nullptr;
}

}  // namespace

bool iosCloudStart(int id) {
    if (!objcReady()) return false;
    __atomic_store_n(&g_current, id, __ATOMIC_RELEASE);
    void* th = nullptr;
    if (pthread_create(&th, nullptr, job, (void*)(intptr_t)id) != 0) {
        __atomic_store_n(&g_current, 0, __ATOMIC_RELEASE);
        return false;
    }
    pthread_detach(th);
    return true;
}
void iosCloudCancel() { __atomic_store_n(&g_current, 0, __ATOMIC_RELEASE); }
void iosCloudForget() {
    char p[512];
    storePath(p, sizeof p, "PhiMeCloud.txt");
    unlink(p);
    LOG("cloud(iOS): saved login forgotten");
}
// the app's bundle ID ("" if unknown)
const char* iosBundleId() {
    static char b[200];
    static bool done = false;
    if (done) return b;
    done = true;
    if (!objcReady()) return b;
    void* pool = O.poolPush();
    const char* s = cstr(msg<id>(msg<id>(cls("NSBundle"), "mainBundle"), "bundleIdentifier"));
    snprintf(b, sizeof b, "%s", s ? s : "");
    O.poolPop(pool);
    return b;
}
#endif
