package app.aemu.stub;

import android.os.IBinder;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/** Read-only Guest probe for the currently registered Wi-Fi service's RSSI mapping. */
public final class WifiSignalLevelProbe {
    private WifiSignalLevelProbe() { }

    private static int level(Method method, Object service, int rssi) throws Exception {
        try {
            Object result = method.invoke(service, Integer.valueOf(rssi));
            if (!(result instanceof Integer)) throw new IllegalStateException("unexpected signal level type");
            return ((Integer) result).intValue();
        } catch (InvocationTargetException failure) {
            Throwable cause = failure.getCause();
            if (cause instanceof Exception) throw (Exception) cause;
            if (cause instanceof Error) throw (Error) cause;
            throw failure;
        }
    }

    public static void main(String[] args) throws Exception {
        Class<?> registry = Class.forName("android.os.ServiceManager");
        IBinder binder = (IBinder) registry.getMethod("checkService", String.class).invoke(null, "wifi");
        if (binder == null || !binder.isBinderAlive()) throw new IllegalStateException("Wi-Fi service unavailable");
        Class<?> stub = Class.forName("android.net.wifi.IWifiManager$Stub");
        Object service = stub.getMethod("asInterface", IBinder.class).invoke(null, binder);
        if (service == null) throw new IllegalStateException("Wi-Fi interface unavailable");
        Class<?> contract = Class.forName("android.net.wifi.IWifiManager");
        Method method = contract.getMethod("calculateSignalLevel", int.class);
        if (method.getReturnType() != int.class) throw new IllegalStateException("unexpected method return type");
        System.out.println("WIFI_SIGNAL_PROBE signature=" + method.toGenericString());
        int min = level(method, service, Integer.MIN_VALUE);
        int max = level(method, service, Integer.MAX_VALUE);
        System.out.println("WIFI_SIGNAL_PROBE min=" + min + " max=" + max);
        if (min < 0 || max < min || max > 64)
            throw new IllegalStateException("unexpected signal level range");
        // For the monotonic threshold mapping, find each level transition over
        // the entire integer domain and verify both sides of the boundary.
        for (int target = min + 1; target <= max; target++) {
            long low = Integer.MIN_VALUE, high = Integer.MAX_VALUE;
            while (low < high) {
                long mid = low + (high - low) / 2;
                if (level(method, service, (int) mid) >= target) high = mid;
                else low = mid + 1;
            }
            int boundary = (int) low;
            if (boundary == Integer.MIN_VALUE) throw new IllegalStateException("invalid signal boundary");
            int below = level(method, service, boundary - 1);
            int at = level(method, service, boundary);
            if (below >= target || at < target) throw new IllegalStateException("inconsistent signal boundary");
            System.out.println("WIFI_SIGNAL_PROBE threshold=" + boundary + " below=" + below + " at=" + at);
        }
        int previous = min;
        for (int rssi = -160; rssi <= 20; rssi++) {
            int current = level(method, service, rssi);
            if (current < previous || current > max) throw new IllegalStateException("invalid or nonmonotonic signal level");
            if (rssi == -160 || current != previous || rssi == -100 || rssi == -75 || rssi == -45 || rssi == 20)
                System.out.println("WIFI_SIGNAL_PROBE rssi=" + rssi + " level=" + current);
            previous = current;
        }
        if (max < previous) throw new IllegalStateException("invalid maximum signal level");
        System.out.println("WIFI_SIGNAL_PROBE_OK");
    }
}
