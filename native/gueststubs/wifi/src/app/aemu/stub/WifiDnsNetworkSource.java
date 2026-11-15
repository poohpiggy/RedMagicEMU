package app.aemu.stub;

import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.NetworkRequest;
import android.os.Handler;
import java.util.concurrent.atomic.AtomicLong;

/** Reads the target Guest's actual Wi-Fi network; it never creates a network. */
final class WifiDnsNetworkSource implements WifiDnsGuard.Source, AutoCloseable {
    private final ConnectivityManager manager;
    private final AtomicLong generation = new AtomicLong();
    private volatile boolean closed;
    private final ConnectivityManager.NetworkCallback wifi = new Callback();
    private final ConnectivityManager.NetworkCallback defaults = new Callback();

    private final class Callback extends ConnectivityManager.NetworkCallback {
        @Override public void onAvailable(Network n) { generation.incrementAndGet(); }
        @Override public void onLost(Network n) { generation.incrementAndGet(); }
        @Override public void onCapabilitiesChanged(Network n, NetworkCapabilities caps) { generation.incrementAndGet(); }
        @Override public void onLinkPropertiesChanged(Network n, LinkProperties properties) { generation.incrementAndGet(); }
    }

    WifiDnsNetworkSource(ConnectivityManager manager, Handler handler) {
        if (manager == null || handler == null) throw new IllegalArgumentException();
        this.manager = manager;
        manager.registerNetworkCallback(new NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build(), wifi, handler);
        try { manager.registerDefaultNetworkCallback(defaults, handler); }
        catch (RuntimeException | Error failure) { manager.unregisterNetworkCallback(wifi); throw failure; }
    }

    @Override public WifiDnsGuard.State read() {
        if (closed) return null;
        long before = generation.get();
        Network active = manager.getActiveNetwork();
        Network[] all = manager.getAllNetworks();
        if (all == null) return null;
        Network target = null;
        for (Network n : all) {
            NetworkCapabilities cap = manager.getNetworkCapabilities(n);
            LinkProperties link = manager.getLinkProperties(n);
            NetworkInfo info = manager.getNetworkInfo(n);
            if (link == null || link.getInterfaceName() == null) return null;
            if (!"eth0".equals(link.getInterfaceName())) continue;
            if (target != null || cap == null || !cap.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
                    || info == null || !info.isConnected()) return null;
            target = n;
        }
        if (target == null || closed || before != generation.get()) return null;
        long handle = target.getNetworkHandle();
        long id = handle >>> 32;
        if ((handle & 0xffffffffL) != 0xcafed00dL || id == 0 || id > 65535) return null;
        return new WifiDnsGuard.State((int)id, target.equals(active), before);
    }

    @Override public boolean report(boolean available) {
        WifiDnsGuard.State state = read();
        if (state == null) return false;
        Network[] all = manager.getAllNetworks();
        if (all == null) return false;
        Network target = null;
        for (Network n : all) if ((n.getNetworkHandle() >>> 32) == state.id) {
            if (target != null) return false;
            target = n;
        }
        if (target == null || closed || state.generation != generation.get()) return false;
        manager.reportNetworkConnectivity(target, available);
        return true;
    }

    @Override public void close() {
        if (closed) return;
        closed = true;
        generation.incrementAndGet();
        try { manager.unregisterNetworkCallback(wifi); }
        finally { manager.unregisterNetworkCallback(defaults); }
    }
}
