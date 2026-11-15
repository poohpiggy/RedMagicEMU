package app.aemu.stub;

import android.content.Context;

/**
 * Experimental standalone-guest bootstrap. ActivityThread.systemMain has
 * process-wide initialization side effects; this must only be called by the
 * Wi-Fi guest process after its startup ordering has been verified. It does
 * not create or register a Binder service.
 */
final class WifiSystemContext {
    private WifiSystemContext() { }

    static Context obtain() throws ReflectiveOperationException {
        Class<?> activityThread = Class.forName("android.app.ActivityThread");
        Object thread = activityThread.getMethod("currentActivityThread").invoke(null);
        if (thread == null) thread = activityThread.getMethod("systemMain").invoke(null);
        if (thread == null) throw new IllegalStateException("no guest ActivityThread");
        Object context = activityThread.getMethod("getSystemContext").invoke(thread);
        if (!(context instanceof Context)) throw new IllegalStateException("no guest system Context");
        return (Context) context;
    }
}
