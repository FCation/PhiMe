package phime;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.net.Uri;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Download-only access to the Phigros cloud save, without the game's TapTap SDK (which refuses a re-signed APK:
 * "signature not match"). The same calls the community tools use (PhiLogin, PhigrosLibrary):
 *
 *   TapTap device login: POST oauth2/v1/device/code -> a QR link; poll POST oauth2/v1/token until the user
 *     confirms in the TapTap app -> kid + mac_key; GET open.tapapis.cn/account/basic-info/v1 (MAC signed)
 *     -> openid + unionid
 *   LeanCloud (the game's save server): POST 1.1/users with that authData -> sessionToken;
 *     GET 1.1/users/me, GET 1.1/classes/_GameSave (newest of this user) -> gameFile.url -> the save zip
 *
 * Nothing is ever uploaded. The sessionToken is kept in the game's private files dir so the next pull needs no
 * scan. Native side: cmd() in, event() out (any thread):
 *   kind 1 done (a = zip path, b = info text), 0 cancelled, -1 failed (b = why),
 *   2 QR ready (a = link, b = Qr.encode(link)), 3 progress (b = text)
 */
public final class Cloud implements Runnable {
    static native void event(int id, int kind, String a, String b);

    static final String CLIENT = "rAK3FfdieFob2Nn8Am";
    static final String LC_KEY = "Qr9AEqtuoSVS3zeD6iVbM4ZC0AtkJcQ89tywVyi0";
    static final String LC = "https://rak3ffdi.cloud.tds1.tapapis.cn/1.1/";
    static final String TAP_UA = "TapTapAndroidSDK/3.16.5";
    static final String LC_UA = "LeanCloud-CSharp-SDK/1.0.3";
    static final String[] CODE_URLS = {"https://www.taptap.cn/oauth2/v1/device/code",
                                       "https://accounts.tapapis.cn/oauth2/v1/device/code"};
    static final String[] TOKEN_URLS = {"https://www.taptap.com/oauth2/v1/token",
                                        "https://accounts.tapapis.cn/oauth2/v1/token"};
    static final int OP_PULL = 0, OP_PASTE = 1, OP_CANCEL = 2, OP_OPEN_TAPTAP = 3, OP_OPEN_BROWSER = 4,
                     OP_LOGOUT = 5, OP_LANG = 6, OP_LOGIN = 7;
    static final long MAX_ZIP = 16L << 20;

    static volatile int current;  // the running job; anything else = cancelled
    static volatile boolean en;   // English messages (the mod's language, set with OP_LANG)

    static String t(String zh, String english) {
        return en ? english : zh;
    }
    static volatile String qrLink;

    final Activity act;
    final int id;
    final String zipPath;
    final String pasted;  // a sessionToken from the clipboard, or null
    final boolean loginOnly;  // OP_LOGIN: stop at a working sessionToken (event 4), no download

    Cloud(Activity a, int id, String zipPath, String pasted, boolean loginOnly) {
        this.act = a;
        this.id = id;
        this.zipPath = zipPath;
        this.pasted = pasted;
        this.loginOnly = loginOnly;
    }

    // ======================= entry =======================
    public static void cmd(final Activity a, final int id, final int op, final String arg) {
        try {
            switch (op) {
                case OP_PULL:
                    begin(a, id, arg, null, false);
                    break;
                case OP_LOGIN:
                    begin(a, id, null, null, true);
                    break;
                case OP_PASTE:
                    a.runOnUiThread(new Runnable() {
                        public void run() {
                            String t = clipboard(a);
                            if (t == null) {
                                send(id, -1, null, t("剪贴板里没有凭证。先复制 sessionToken（25 位字母和数字），再点「粘贴」。", "No token on the clipboard. Copy the sessionToken (25 letters and digits) first, then tap Paste."));
                                return;
                            }
                            begin(a, id, arg, t, false);
                        }
                    });
                    break;
                case OP_CANCEL:
                    if (current == id) current = 0;
                    break;
                case OP_OPEN_TAPTAP:
                case OP_OPEN_BROWSER:
                    a.runOnUiThread(new Runnable() {
                        public void run() {
                            open(a, id, op == OP_OPEN_TAPTAP);
                        }
                    });
                    break;
                case OP_LOGOUT:
                    new File(a.getFilesDir(), "PhiMeCloud.txt").delete();
                    break;
                case OP_LANG:
                    en = "en".equals(arg);
                    break;
            }
        } catch (Throwable e) {
            send(id, -1, null, String.valueOf(e));
        }
    }

    static void begin(Activity a, int id, String zipPath, String pasted, boolean loginOnly) {
        current = id;
        qrLink = null;
        Thread t = new Thread(new Cloud(a, id, zipPath, pasted, loginOnly), "PhiMeCloud");
        t.setDaemon(true);
        t.start();
    }

    static void send(int id, int kind, String a, String b) {
        try {
            event(id, kind, a, b);
        } catch (Throwable ignored) {
        }
    }

    static String clipboard(Activity a) {
        try {
            ClipboardManager cm = (ClipboardManager) a.getSystemService("clipboard");
            ClipData c = cm != null ? cm.getPrimaryClip() : null;
            CharSequence s = c != null && c.getItemCount() > 0 ? c.getItemAt(0).getText() : null;
            if (s == null) return null;
            String t = s.toString().trim();
            // "sessionToken: xxx" or a bot's message: take the longest run of letters / digits
            String best = "";
            int start = -1;
            for (int i = 0; i <= t.length(); i++) {
                char ch = i < t.length() ? t.charAt(i) : ' ';
                boolean w = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
                if (w && start < 0) start = i;
                if (!w && start >= 0) {
                    if (i - start > best.length()) best = t.substring(start, i);
                    start = -1;
                }
            }
            return best.length() >= 20 && best.length() <= 40 ? best : null;
        } catch (Throwable e) {
            return null;
        }
    }

    static void open(Activity a, int id, boolean taptap) {
        String link = qrLink;
        if (link == null) return;
        if (taptap) {
            try {
                Intent it = new Intent("android.intent.action.VIEW", Uri.parse(link));
                it.setPackage("com.taptap");
                a.startActivity(it);
                return;
            } catch (Throwable e) {
                send(id, 3, null, t("没找到 TapTap App，改用浏览器打开。", "TapTap is not installed; opening the browser instead."));
            }
        }
        try {
            a.startActivity(new Intent("android.intent.action.VIEW", Uri.parse(link)));
        } catch (Throwable e) {
            send(id, 3, null, t("没能打开浏览器（" + e + "）", "Could not open a browser (" + e + ")"));
        }
    }

    // ======================= the job =======================
    static final class Cancelled extends RuntimeException {}

    static final class Fail extends Exception {
        Fail(String m) {
            super(m);
        }
    }

    void check() {
        if (current != id) throw new Cancelled();
    }

    void progress(String s) {
        check();
        send(id, 3, null, s);
    }

    public void run() {
        try {
            String token = pasted;
            JSONObject me = null;
            if (token == null) token = loadToken();
            if (token != null) {
                progress(t("正在连接云端…", "Connecting…"));
                me = me(token);
                if (me == null) {
                    if (pasted != null) throw new Fail(t("这个凭证无效或已经过期，换一个新的再试试。", "This token is invalid or expired; try a fresh one."));
                    saveToken(null);  // expired: log in again
                    token = null;
                }
            }
            if (token == null) {
                token = login();
                me = me(token);
                if (me == null) throw new Fail(t("登录成功了，但云端不认这个账号的凭证。", "Logged in, but the save server does not accept this account's token."));
            }
            saveToken(token);
            String objectId = me.optString("objectId"), nick = me.optString("nickname");
            if (loginOnly) {  // the game's own SDK logs in with it (account.cpp)
                check();
                send(id, 4, token, nick);
                return;
            }
            progress(t("正在查找云存档…", "Looking for the cloud save…"));
            String where = "{\"user\":{\"__type\":\"Pointer\",\"className\":\"_User\",\"objectId\":" +
                           JSONObject.quote(objectId) + "}}";
            Resp r = http("GET", LC + "classes/_GameSave?where=" + enc(where) + "&order=-updatedAt&limit=1",
                          lcHeaders(token), null);
            JSONObject j = r.json();
            JSONArray res = j != null ? j.optJSONArray("results") : null;
            if (r.code != 200 || res == null) throw new Fail(t("云端返回了看不懂的结果（HTTP " + r.code + "）。", "The server sent something unexpected (HTTP " + r.code + ")."));
            JSONObject save = res.length() > 0 ? res.optJSONObject(0) : null;
            JSONObject file = save != null ? save.optJSONObject("gameFile") : null;
            String url = file != null ? file.optString("url") : "";
            if (save == null || url.length() == 0) throw new Fail(t("这个账号（" + nick + "）在云端还没有存档。", "This account (" + nick + ") has no cloud save yet."));
            if (url.startsWith("http://")) url = "https://" + url.substring(7);
            progress(t("正在下载存档…", "Downloading…"));
            int n = download(url, zipPath);
            StringBuilder info = new StringBuilder();
            info.append(t("账号：", "Account: ")).append(nick.length() > 0 ? nick : t("（没有昵称）", "(no nickname)"));
            info.append(t("\n云存档更新于 ", "\nCloud save from ")).append(localTime(save.optString("updatedAt")));
            String rks = rks(save.optString("summary"));
            if (rks != null) info.append(t("，RKS ", ", RKS ")).append(rks);
            info.append(t("（", " (")).append((n + 1023) / 1024).append(t(" KB）", " KB)"));
            check();
            send(id, 1, zipPath, info.toString());
        } catch (Cancelled e) {
            send(id, 0, null, null);
        } catch (Fail e) {
            if (current == id) send(id, -1, null, e.getMessage());
        } catch (Throwable e) {
            if (current == id) send(id, -1, null, t("网络出错了：", "Network error: ") + e);
        } finally {
            if (current == id) current = 0;
        }
    }

    // ---- TapTap device login -> a LeanCloud sessionToken ----
    String login() throws Exception {
        progress(t("正在向 TapTap 申请登录二维码…", "Asking TapTap for a login code…"));
        String info = enc("{\"device_id\":\"" + deviceId() + "\"}");
        String[] tap = {"Content-Type", "application/x-www-form-urlencoded", "User-Agent", TAP_UA};
        JSONObject data = null;
        String err = null;
        for (String u : CODE_URLS) {
            try {
                Resp r = http("POST", u, tap, "client_id=" + CLIENT +
                              "&response_type=device_code&scope=basic_info&version=1.2.0&platform=unity&info=" + info);
                JSONObject j = r.json();
                data = j != null && j.optBoolean("success") ? j.optJSONObject("data") : null;
                if (data != null && data.optString("device_code").length() > 0) break;
                err = "HTTP " + r.code + " " + r.head();
                data = null;
            } catch (IOException e) {
                err = String.valueOf(e);
            }
            check();
        }
        if (data == null) throw new Fail(t("TapTap 没给登录二维码（" + err + "）。接口可能变了。", "TapTap gave no login code (" + err + "). The API may have changed."));
        String code = data.optString("device_code"), link = data.optString("qrcode_url");
        if (link.length() == 0) link = data.optString("verification_url");
        int interval = Math.max(2, data.optInt("interval", 5)), life = data.optInt("expires_in", 300);
        qrLink = link;
        check();
        send(id, 2, link, Qr.encode(link));

        long deadline = System.currentTimeMillis() + life * 1000L;
        String body = "grant_type=device_token&client_id=" + CLIENT + "&secret_type=hmac-sha-1&code=" + enc(code) +
                      "&version=1.0&platform=unity&info=" + info;
        int host = 0, netErrors = 0;
        boolean scanned = false;
        String lastOther = "";
        JSONObject tok = null;
        while (tok == null) {
            for (long t = System.currentTimeMillis() + interval * 1000L; System.currentTimeMillis() < t;) {
                check();
                Thread.sleep(200);
            }
            if (System.currentTimeMillis() > deadline) throw new Fail(t("二维码过期了，再点一次「拉取」换个新的吧。", "The code expired; tap Pull again for a new one."));
            JSONObject j;
            try {
                Resp r = http("POST", TOKEN_URLS[host], tap, body);
                j = r.json();
                if (j == null) throw new IOException("HTTP " + r.code + " " + r.head());
                netErrors = 0;
            } catch (IOException e) {
                if (++netErrors >= 3) {
                    if (host + 1 < TOKEN_URLS.length) {
                        host++;
                        netErrors = 0;
                        continue;
                    }
                    throw new Fail(t("等待授权时网络出错了：", "Network error while waiting: ") + e);
                }
                continue;
            }
            check();
            JSONObject d = j.optJSONObject("data");
            if (j.optBoolean("success") && d != null && d.optString("mac_key").length() > 0) {
                tok = d;
                break;
            }
            String e = d != null ? d.optString("error") : "";
            if ("authorization_pending".equals(e)) continue;
            if ("authorization_waiting".equals(e)) {
                if (!scanned) progress(t("已扫码，请在 TapTap 里点确认授权…", "Scanned; confirm in TapTap…"));
                scanned = true;
                continue;
            }
            if ("slow_down".equals(e)) {
                interval += 2;
                continue;
            }
            if ("expired_token".equals(e) || "invalid_grant".equals(e))
                throw new Fail(t("二维码过期了，再点一次「拉取」换个新的吧。", "The code expired; tap Pull again for a new one."));
            if ("access_denied".equals(e)) throw new Fail(t("TapTap 里拒绝了授权。", "Authorization was declined in TapTap."));
            // a state not seen before (e.g. scanned, not confirmed yet): keep waiting until the code expires
            if (!e.equals(lastOther)) {
                lastOther = e;
                progress(t("TapTap：", "TapTap: ") + (e.length() > 0 ? e : t("等待中", "waiting")) + "…");
            }
        }

        progress(t("已授权，正在登录云存档…", "Authorized; logging in to the save server…"));
        String kid = tok.optString("kid"), macKey = tok.optString("mac_key");
        String access = tok.optString("access_token");
        if (access.length() == 0) access = kid;
        String path = "/account/basic-info/v1?client_id=" + CLIENT;
        Resp r = http("GET", "https://open.tapapis.cn" + path,
                      new String[] {"User-Agent", TAP_UA, "Authorization", mac(kid, macKey, "GET", path, "open.tapapis.cn")},
                      null);
        JSONObject j = r.json();
        JSONObject acc = j != null ? j.optJSONObject("data") : null;
        if (acc == null || acc.optString("openid").length() == 0)
            throw new Fail(t("读不到 TapTap 账号信息（HTTP " + r.code + " " + r.head() + "）。", "Could not read the TapTap account (HTTP " + r.code + " " + r.head() + ")."));
        String auth = "{\"authData\":{\"taptap\":{\"kid\":" + JSONObject.quote(kid) + ",\"access_token\":" +
                      JSONObject.quote(access) + ",\"token_type\":\"mac\",\"mac_key\":" + JSONObject.quote(macKey) +
                      ",\"mac_algorithm\":\"hmac-sha-1\",\"openid\":" + JSONObject.quote(acc.optString("openid")) +
                      ",\"unionid\":" + JSONObject.quote(acc.optString("unionid")) + "}}}";
        r = http("POST", LC + "users", lcHeaders(null), auth);
        j = r.json();
        String st = j != null ? j.optString("sessionToken") : "";
        if (st.length() == 0) throw new Fail(t("云存档服务器没有给登录凭证（HTTP " + r.code + " " + r.head() + "）。", "The save server gave no token (HTTP " + r.code + " " + r.head() + ")."));
        return st;
    }

    static String mac(String kid, String key, String method, String pathQuery, String host) throws Exception {
        long ts = System.currentTimeMillis() / 1000;
        long nonce = (long) (Math.random() * 4294967295.0);
        String s = ts + "\n" + nonce + "\n" + method + "\n" + pathQuery + "\n" + host + "\n443\n\n";
        javax.crypto.Mac m = javax.crypto.Mac.getInstance("HmacSHA1");
        m.init(new javax.crypto.spec.SecretKeySpec(key.getBytes("UTF-8"), "HmacSHA1"));
        String sig = java.util.Base64.getEncoder().encodeToString(m.doFinal(s.getBytes("UTF-8")));
        return "MAC id=\"" + kid + "\",ts=\"" + ts + "\",nonce=\"" + nonce + "\",mac=\"" + sig + "\"";
    }

    // users/me for this token; null = the token is not (or no longer) valid
    JSONObject me(String token) throws Exception {
        Resp r = http("GET", LC + "users/me", lcHeaders(token), null);
        check();
        JSONObject j = r.json();
        if (r.code == 200 && j != null && j.optString("objectId").length() > 0) return j;
        if (r.code >= 400 && r.code < 500) return null;
        throw new Fail(t("云存档服务器出错了（HTTP " + r.code + " " + r.head() + "）。", "The save server failed (HTTP " + r.code + " " + r.head() + ")."));
    }

    static String[] lcHeaders(String token) {
        if (token == null)
            return new String[] {"X-LC-Id", CLIENT, "X-LC-Key", LC_KEY, "User-Agent", LC_UA, "Content-Type",
                                 "application/json", "Accept", "application/json"};
        return new String[] {"X-LC-Id", CLIENT, "X-LC-Key", LC_KEY, "User-Agent", LC_UA, "Accept", "application/json",
                             "X-LC-Session", token};
    }

    // ---- the summary: [save version u8][challenge rank u16][rks float] ... (little endian) ----
    static String rks(String b64) {
        try {
            byte[] b = java.util.Base64.getDecoder().decode(b64);
            if (b.length < 7) return null;
            int bits = (b[3] & 0xFF) | (b[4] & 0xFF) << 8 | (b[5] & 0xFF) << 16 | (b[6] & 0xFF) << 24;
            float v = Float.intBitsToFloat(bits);
            if (!(v >= 0 && v <= 20)) return null;
            return String.format(java.util.Locale.ROOT, "%.2f", v);
        } catch (Throwable e) {
            return null;
        }
    }

    static String localTime(String iso) {
        try {
            java.text.SimpleDateFormat in = new java.text.SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss", java.util.Locale.ROOT);
            in.setTimeZone(java.util.TimeZone.getTimeZone("UTC"));
            java.util.Date d = in.parse(iso.length() > 19 ? iso.substring(0, 19) : iso);
            return new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm", java.util.Locale.ROOT).format(d);
        } catch (Throwable e) {
            return iso;
        }
    }

    // ---- files ----
    File store(String name) {
        return new File(act.getFilesDir(), name);
    }

    String loadToken() {
        String t = readText(store("PhiMeCloud.txt"));
        return t != null && t.length() >= 20 ? t : null;
    }

    void saveToken(String t) {
        File f = store("PhiMeCloud.txt");
        if (t == null) f.delete();
        else writeText(f, t);
    }

    String deviceId() {
        File f = store("PhiMeCloudDevice.txt");
        String d = readText(f);
        if (d == null || d.length() < 8) {
            d = java.util.UUID.randomUUID().toString().replace("-", "");
            writeText(f, d);
        }
        return d;
    }

    static String readText(File f) {
        try {
            InputStream in = new FileInputStream(f);
            try {
                return new String(readAll(in, 4096), "UTF-8").trim();
            } finally {
                in.close();
            }
        } catch (Throwable e) {
            return null;
        }
    }

    static void writeText(File f, String s) {
        try {
            OutputStream o = new FileOutputStream(f);
            try {
                o.write(s.getBytes("UTF-8"));
            } finally {
                o.close();
            }
        } catch (Throwable ignored) {
        }
    }

    // ---- http ----
    static final class Resp {
        int code;
        byte[] body;

        JSONObject json() {
            try {
                return new JSONObject(new String(body, "UTF-8"));
            } catch (Throwable e) {
                return null;
            }
        }

        String head() {
            try {
                String s = new String(body, "UTF-8").replace('\n', ' ');
                return s.length() > 120 ? s.substring(0, 120) + "…" : s;
            } catch (Throwable e) {
                return "";
            }
        }
    }

    static String enc(String s) throws Exception {
        return URLEncoder.encode(s, "UTF-8");
    }

    static HttpURLConnection connect(String method, String url, String[] headers) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setRequestMethod(method);
        c.setConnectTimeout(15000);
        c.setReadTimeout(20000);
        c.setUseCaches(false);
        for (int i = 0; headers != null && i + 1 < headers.length; i += 2) c.setRequestProperty(headers[i], headers[i + 1]);
        return c;
    }

    static Resp http(String method, String url, String[] headers, String body) throws IOException {
        HttpURLConnection c = connect(method, url, headers);
        try {
            if (body != null) {
                c.setDoOutput(true);
                OutputStream o = c.getOutputStream();
                o.write(body.getBytes("UTF-8"));
                o.close();
            }
            Resp r = new Resp();
            r.code = c.getResponseCode();
            InputStream in = r.code >= 400 ? c.getErrorStream() : c.getInputStream();
            r.body = in != null ? readAll(in, 1 << 20) : new byte[0];
            if (in != null) in.close();
            return r;
        } finally {
            c.disconnect();
        }
    }

    int download(String url, String path) throws Exception {
        HttpURLConnection c = connect("GET", url, new String[] {"User-Agent", LC_UA});
        try {
            int code = c.getResponseCode();
            if (code != 200) throw new Fail(t("存档文件下载失败（HTTP " + code + "）。", "The save download failed (HTTP " + code + ")."));
            InputStream in = c.getInputStream();
            byte[] b;
            try {
                b = readAll(in, MAX_ZIP);
            } finally {
                in.close();
            }
            if (b.length < 4 || b[0] != 'P' || b[1] != 'K') throw new Fail(t("下载到的不是 zip 存档。", "The download is not a zip save."));
            check();
            OutputStream o = new FileOutputStream(path);
            try {
                o.write(b);
            } finally {
                o.close();
            }
            return b.length;
        } finally {
            c.disconnect();
        }
    }

    static byte[] readAll(InputStream in, long max) throws IOException {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] buf = new byte[16384];
        int n;
        while ((n = in.read(buf)) > 0) {
            o.write(buf, 0, n);
            if (o.size() > max) throw new IOException("太大了");
        }
        return o.toByteArray();
    }
}
