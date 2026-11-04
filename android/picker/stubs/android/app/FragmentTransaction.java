package android.app;
public abstract class FragmentTransaction {
    public abstract FragmentTransaction add(Fragment f, String tag);
    public abstract FragmentTransaction remove(Fragment f);
    public abstract int commitAllowingStateLoss();
}
