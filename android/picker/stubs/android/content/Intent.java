package android.content;
public class Intent {
    public Intent(String action) {}
    public Intent(String action, android.net.Uri u) {}
    public Intent addCategory(String c) { return this; }
    public Intent setType(String t) { return this; }
    public Intent setPackage(String p) { return this; }
    public Intent addFlags(int f) { return this; }
    public Intent putExtra(String k, String v) { return this; }
    public Intent putExtra(String k, String[] v) { return this; }
    public android.net.Uri getData() { return null; }
}
