package app.aemu.stub;

import android.content.AttributionSource;
import android.content.Context;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/** Reads settings through ActivityManager's external-provider API for this standalone process. */
final class WifiSettingsReader {
    private static final String AUTHORITY = "settings";

    private WifiSettingsReader() { }

    static int satelliteMode(Context context) {
        return globalInt(context, "satellite_mode_enabled");
    }

    static int airplaneMode(Context context) {
        return globalInt(context, "airplane_mode_on");
    }

    private static int globalInt(Context context, String setting) {
        if (context == null) throw new IllegalArgumentException("guest context required");
        try {
            Class<?> activityManager = Class.forName("android.app.ActivityManager");
            Class<?> managerInterface = Class.forName("android.app.IActivityManager");
            Object manager = invoke(activityManager.getMethod("getService"), null);
            if (manager == null) throw new IllegalStateException("activity service unavailable");
            Method acquire = managerInterface.getMethod("getContentProviderExternal",
                    String.class, int.class, IBinder.class, String.class);
            Method release = managerInterface.getMethod("removeContentProviderExternalAsUser",
                    String.class, IBinder.class, int.class);
            IBinder token = new Binder();
            Object holder = invoke(acquire, manager, AUTHORITY, 0, token, "redmagic-wifi");
            if (holder == null) throw new IllegalStateException("settings provider unavailable");
            try (ProviderLease ignored = new ProviderLease(manager, release, token)) {
                Object provider = holder.getClass().getField("provider").get(holder);
                if (provider == null) throw new IllegalStateException("settings provider unavailable");
                Class<?> providerInterface = Class.forName("android.content.IContentProvider");
                Method call = providerInterface.getMethod("call", AttributionSource.class,
                        String.class, String.class, String.class, Bundle.class);
                Bundle result = (Bundle) invoke(call, provider, context.getAttributionSource(),
                        AUTHORITY, "GET_global", setting, null);
                if (result == null || !result.containsKey("value"))
                    throw new IllegalStateException(setting + " response unavailable");
                Object raw = result.get("value");
                if (raw != null && !(raw instanceof String))
                    throw new IllegalStateException("invalid " + setting + " response type");
                String value = (String) raw;
                // A successful response explicitly containing null is an unset setting,
                // for which Settings.Global.getInt(..., 0) also returns zero.
                if (value == null) return 0;
                try { return Integer.parseInt(value); }
                catch (NumberFormatException failure) {
                    throw new IllegalStateException("invalid " + setting + " value", failure);
                }
            }
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException("settings provider API unavailable", failure);
        }
    }

    private static Object invoke(Method method, Object receiver, Object... args)
            throws ReflectiveOperationException {
        try { return method.invoke(receiver, args); }
        catch (InvocationTargetException failure) {
            Throwable cause = failure.getCause();
            if (cause instanceof RuntimeException) throw (RuntimeException) cause;
            if (cause instanceof Error) throw (Error) cause;
            throw failure;
        }
    }

    private static final class ProviderLease implements AutoCloseable {
        private final Object manager;
        private final Method release;
        private final IBinder token;

        ProviderLease(Object manager, Method release, IBinder token) {
            this.manager = manager;
            this.release = release;
            this.token = token;
        }

        @Override public void close() {
            try { invoke(release, manager, AUTHORITY, token, 0); }
            catch (ReflectiveOperationException failure) {
                throw new IllegalStateException("settings provider release failed", failure);
            }
        }
    }
}
