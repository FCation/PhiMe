package android.app;
public class Application extends android.content.Context {
    public interface ActivityLifecycleCallbacks {
        void onActivityCreated(Activity a, android.os.Bundle b);
        void onActivityStarted(Activity a);
        void onActivityResumed(Activity a);
        void onActivityPaused(Activity a);
        void onActivityStopped(Activity a);
        void onActivitySaveInstanceState(Activity a, android.os.Bundle b);
        void onActivityDestroyed(Activity a);
    }
    public void registerActivityLifecycleCallbacks(ActivityLifecycleCallbacks c) {}
}
