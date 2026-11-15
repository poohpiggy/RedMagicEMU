/* Modified for AEmulator Super Plus, 2026-10-11: hidden setDetailedState reached by reflection (builds with the public SDK).
 * Modified for AEmulator Plus, 2026-11-01: Android 8 - 17 (NetworkAgent without NetworkInfo, Ethernet fallback). GPL-3.0; see LICENSE and NOTICE.md.
 * Modified for RedMagicEMU, 2026-11-10: API 36-only agent, fail-closed interface conflict check, and existing guest routes. */
package app.aemu.stub;

import android.content.Context;
import android.content.ContextWrapper;
import android.net.LinkProperties;
import android.os.IBinder;
import android.os.Looper;
import java.net.InetAddress;
import java.util.Arrays;

/** API 36 adaptation of the upstream Wi-Fi network registration. */
final class WifiNet {
    static final class Identity {
        static final Identity WIFI = new Identity();
        static final Identity ETHERNET = new Identity();
        private Identity() { }
    }

    private static final class Ctx extends ContextWrapper {
        Object cm;
        Ctx() { super(null); }
        @Override public Context getApplicationContext() { return this; }
        @Override public String getPackageName() { return "android"; }
        @Override public Object getSystemService(String name) { return "connectivity".equals(name) ? cm : null; }
        @Override public String getSystemServiceName(Class<?> type) {
            return "android.net.ConnectivityManager".equals(type.getName()) ? "connectivity" : null;
        }
    }

    private static Ctx contextFor(IBinder binder) throws Exception {
        Class<?> iface = Class.forName("android.net.IConnectivityManager");
        Object service = Class.forName("android.net.IConnectivityManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, binder);
        Ctx ctx = new Ctx();
        ctx.cm = Refl.create("android.net.ConnectivityManager", new Class<?>[] {Context.class, iface}, ctx, service);
        if (ctx.cm == null) throw new IllegalStateException("no ConnectivityManager");
        return ctx;
    }

    static WifiLink register(Looper looper, IBinder connectivity, WifiConfig cfg,
            Identity identity, Object wifiInfo, Runnable onUnwanted) throws Exception {
        if (identity != Identity.WIFI) throw new UnsupportedOperationException("Ethernet fallback is not owned by Wi-Fi");
        if (wifiInfo == null) throw new IllegalStateException("guest WifiInfo unavailable");
        Ctx context = contextFor(connectivity);
        LinkProperties link = VirtualWifiLinkProperties.create("eth0", InetAddress.getByName(cfg.ip), cfg.prefix,
                InetAddress.getByName(cfg.gateway), Arrays.asList(InetAddress.getByName(cfg.dns1), InetAddress.getByName(cfg.dns2)));
        WifiGuestNetwork.requireAvailableInterface(context, link.getInterfaceName());
        return WifiNet2.start(context, looper, link, wifiInfo, onUnwanted);
    }

    static void verifyAvailable(IBinder connectivity) throws Exception {
        WifiGuestNetwork.requireAvailableInterface(contextFor(connectivity), "eth0");
    }
}
