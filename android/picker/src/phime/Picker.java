package phime;

import android.app.Activity;
import android.app.Application;
import android.app.Fragment;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The system file picker for PhiMe (Storage Access Framework). Built into a dex that the mod carries inside
 * libPhiMeMod.so and loads with InMemoryDexClassLoader, so no manifest change is needed: a headless fragment
 * starts the picker and gets its onActivityResult.
 *
 * mode 0 = open: the chosen file is copied to `path`.
 * mode 1 = save: the file at `path` is written to the place the user chose (suggested name `name`).
 * The result goes to the native `result` (any thread): status 1 done, 0 cancelled, -1 failed
 * (name = the chosen file's name, or the error).
 */
public final class Picker extends Fragment {
    static native void result(int id, int status, String path, String name);

    static final int REQ = 0x5A1;
    static final long MAX_BYTES = 64L << 20;
    static volatile Picker pending;
    static boolean guarded;

    Activity host;
    int id, mode;
    String[] mimes;
    String name, path;
    boolean launched, reported;

    public static void start(Activity a, int id, int mode, String[] mimes, String name, String path) {
        a.runOnUiThread(new Starter(a, id, mode, mimes, name, path));
    }

    static final class Starter implements Runnable {
        final Activity a;
        final int id, mode;
        final String[] mimes;
        final String name, path;

        Starter(Activity a, int id, int mode, String[] mimes, String name, String path) {
            this.a = a;
            this.id = id;
            this.mode = mode;
            this.mimes = mimes;
            this.name = name;
            this.path = path;
        }

        public void run() {
            try {
                if (!guarded) {
                    a.getApplication().registerActivityLifecycleCallbacks(new Guard());
                    guarded = true;
                }
                Picker p = new Picker();
                p.id = id;
                p.mode = mode;
                p.mimes = mimes;
                p.name = name;
                p.path = path;
                pending = p;
                a.getFragmentManager().beginTransaction().add(p, "PhiMePicker").commitAllowingStateLoss();
            } catch (Throwable e) {
                pending = null;
                send(id, -1, null, String.valueOf(e));
            }
        }
    }

    static void send(int id, int status, String path, String name) {
        try {
            result(id, status, path, name);
        } catch (Throwable ignored) {
        }
    }

    @Override
    public void onAttach(Activity a) {
        super.onAttach(a);
        host = a;
    }

    @Override
    public void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (launched || path == null) {  // recreated without our fields: nothing to do
            finish(0, null, null);
            return;
        }
        launched = true;
        Intent it;
        if (mode == 0) {
            it = new Intent("android.intent.action.OPEN_DOCUMENT");
            it.addCategory("android.intent.category.OPENABLE");
            it.setType("*/*");
            if (mimes != null && mimes.length > 0) it.putExtra("android.intent.extra.MIME_TYPES", mimes);
        } else {
            it = new Intent("android.intent.action.CREATE_DOCUMENT");
            it.addCategory("android.intent.category.OPENABLE");
            it.setType(mimes != null && mimes.length > 0 ? mimes[0] : "application/octet-stream");
            it.putExtra("android.intent.extra.TITLE", name);
        }
        try {
            startActivityForResult(it, REQ);
        } catch (Throwable e) {
            finish(-1, null, "没有可用的文件选择器（" + e + "）");
        }
    }

    @Override
    public void onActivityResult(int req, int code, Intent data) {
        if (req != REQ) return;
        Uri u = code == -1 && data != null ? data.getData() : null;
        if (u == null) {
            finish(0, null, null);
            return;
        }
        new Thread(new Copier(this, u), "PhiMePicker").start();
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        if (!reported) finish(0, null, null);
    }

    static final class Copier implements Runnable {
        final Picker p;
        final Uri u;

        Copier(Picker p, Uri u) {
            this.p = p;
            this.u = u;
        }

        public void run() {
            InputStream in = null;
            OutputStream out = null;
            try {
                ContentResolver cr = p.host.getContentResolver();
                String shown = displayName(cr, u);
                if (p.mode == 0) {
                    in = cr.openInputStream(u);
                    out = new FileOutputStream(p.path);
                } else {
                    in = new FileInputStream(p.path);
                    out = cr.openOutputStream(u, "w");
                }
                if (in == null || out == null) throw new java.io.IOException("打不开这个文件");
                byte[] buf = new byte[65536];
                long total = 0;
                int n;
                while ((n = in.read(buf)) > 0) {
                    total += n;
                    if (total > MAX_BYTES) throw new java.io.IOException("文件太大了");
                    out.write(buf, 0, n);
                }
                out.close();
                out = null;
                p.finish(1, p.mode == 0 ? p.path : null, shown != null ? shown : p.name);
            } catch (Throwable e) {
                p.finish(-1, null, String.valueOf(e));
            } finally {
                try {
                    if (in != null) in.close();
                } catch (Throwable ignored) {
                }
                try {
                    if (out != null) out.close();
                } catch (Throwable ignored) {
                }
            }
        }
    }

    static String displayName(ContentResolver cr, Uri u) {
        Cursor c = null;
        try {
            c = cr.query(u, new String[] {"_display_name"}, null, null, null);
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Throwable ignored) {
        } finally {
            try {
                if (c != null) c.close();
            } catch (Throwable ignored) {
            }
        }
        return null;
    }

    void finish(int status, String outPath, String shown) {
        synchronized (this) {
            if (reported) return;
            reported = true;
        }
        if (pending == this) pending = null;
        send(id, status, outPath, shown);
        final Activity a = host;
        if (a != null) a.runOnUiThread(new Remover(this));
    }

    static final class Remover implements Runnable {
        final Picker p;

        Remover(Picker p) {
            this.p = p;
        }

        public void run() {
            try {
                if (p.isAdded()) p.host.getFragmentManager().beginTransaction().remove(p).commitAllowingStateLoss();
            } catch (Throwable ignored) {
            }
        }
    }

    // While a picker is open the game's activity may save its state (and the process may be killed). Fragments
    // saved there would be re-created from the app's class loader, which does not know this class: drop them.
    static final class Guard implements Application.ActivityLifecycleCallbacks {
        public void onActivitySaveInstanceState(Activity a, Bundle out) {
            Picker p = pending;
            if (p != null && p.host == a && out != null) out.remove("android:fragments");
        }

        public void onActivityCreated(Activity a, Bundle b) {}

        public void onActivityStarted(Activity a) {}

        public void onActivityResumed(Activity a) {}

        public void onActivityPaused(Activity a) {}

        public void onActivityStopped(Activity a) {}

        public void onActivityDestroyed(Activity a) {}
    }
}
