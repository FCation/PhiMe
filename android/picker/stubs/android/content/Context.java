package android.content;
public abstract class Context {
    public ContentResolver getContentResolver() { return null; }
    public ClassLoader getClassLoader() { return null; }
    public java.io.File getFilesDir() { return null; }
    public Object getSystemService(String name) { return null; }
    public void startActivity(Intent i) {}
}
