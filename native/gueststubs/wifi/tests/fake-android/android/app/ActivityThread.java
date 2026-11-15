package android.app;

import android.content.Context;

public class ActivityThread {
    public static ActivityThread current;
    public static int systemMainCalls;
    public Context context = new Context();
    public static ActivityThread currentActivityThread() { return current; }
    public static ActivityThread systemMain() {
        systemMainCalls++;
        current = new ActivityThread();
        return current;
    }
    public Context getSystemContext() { return context; }
}
