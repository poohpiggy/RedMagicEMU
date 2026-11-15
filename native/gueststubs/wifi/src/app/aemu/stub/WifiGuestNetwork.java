package app.aemu.stub;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;

/**
 * Guest-side network assembly, to be used only after the real Wi-Fi Binder
 * implementation has authenticated callers and Ethernet has been removed.
 * This object does not register a Binder service or stop Ethernet itself.
 */
final class WifiGuestNetwork {
    interface WifiInfoSource { Object get() throws Exception; }
    interface FailureListener {
        void onFailure(Exception failure);
        void onNetworkUnwanted();
    }

    private final WifiNetworkLifecycle lifecycle;
    private final WifiConnectionCoordinator coordinator;

    WifiGuestNetwork(final Context context, Looper looper, final LinkProperties link,
            final VirtualWifiState state, final Object transitionLock, final WifiInfoSource info,
            final FailureListener failureListener) {
        if (context == null || looper == null || link == null || link.getInterfaceName() == null
                || link.getInterfaceName().isEmpty() || state == null || transitionLock == null
                || info == null || failureListener == null) {
            throw new IllegalArgumentException("guest network dependencies are required");
        }
        final Handler handler = new Handler(looper);
        final Looper agentLooper = looper;
        lifecycle = new WifiNetworkLifecycle(new WifiNetworkLifecycle.Factory() {
            @Override public WifiNetworkLifecycle.Connection start() throws Exception {
                if (!waitForAvailableInterface(context, link.getInterfaceName(), 15000))
                    throw new IllegalStateException("guest interface unavailable for Wi-Fi registration");
                Object wifiInfo = info.get();
                if (wifiInfo == null) throw new IllegalStateException("guest WifiInfo unavailable");
                final VirtualWifiNetworkAgent[] owner = new VirtualWifiNetworkAgent[1];
                owner[0] = VirtualWifiNetworkAgent.start(context, agentLooper, link, wifiInfo, new Runnable() {
                    @Override public void run() {
                        if (!handler.post(new Runnable() {
                            @Override public void run() {
                                synchronized (transitionLock) {
                                    Exception error = null;
                                    synchronized (state) {
                                        // An old agent's queued callback must not disconnect its replacement.
                                        if (owner[0] == null || lifecycle.connectedAgent() != owner[0]) return;
                                        try { coordinator.disconnect(); }
                                        catch (Exception failure) { error = failure; }
                                    }
                                    if (error == null) failureListener.onNetworkUnwanted();
                                    else failureListener.onFailure(error);
                                }
                            }
                        })) failureListener.onFailure(new IllegalStateException("guest network looper stopped"));
                    }
                });
                return owner[0];
            }
        });
        coordinator = new WifiConnectionCoordinator(state, lifecycle);
    }

    void setEnabled(boolean enabled) throws Exception { coordinator.setEnabled(enabled); }
    VirtualWifiState.Result connect(String ssid, String credential) throws Exception { return coordinator.connect(ssid, credential); }
    void disconnect() throws Exception { coordinator.disconnect(); }
    VirtualWifiState.Result reconnect() throws Exception { return coordinator.reconnect(); }
    void forget() throws Exception { coordinator.forget(); }
    void setHostInternet(VirtualWifiState.HostInternet status) { coordinator.setHostInternet(status); }
    WifiNetworkLifecycle.Status networkStatus() { return coordinator.networkStatus(); }

    Network network() {
        WifiNetworkLifecycle.Connection agent = lifecycle.connectedAgent();
        return agent instanceof VirtualWifiNetworkAgent ? ((VirtualWifiNetworkAgent) agent).network() : null;
    }

    private enum InterfaceState { AVAILABLE, BUSY, UNKNOWN }

    private static InterfaceState interfaceState(Context context, String iface) {
        ConnectivityManager manager = context.getSystemService(ConnectivityManager.class);
        if (manager == null) throw new IllegalStateException("guest connectivity service unavailable");
        Network[] networks = manager.getAllNetworks();
        if (networks == null) throw new IllegalStateException("guest networks unavailable");
        boolean unknown = false;
        for (Network network : networks) {
            LinkProperties properties = manager.getLinkProperties(network);
            if (properties == null || properties.getInterfaceName() == null) {
                unknown = true;
                continue;
            }
            if (iface.equals(properties.getInterfaceName()))
                return InterfaceState.BUSY;
        }
        return unknown ? InterfaceState.UNKNOWN : InterfaceState.AVAILABLE;
    }

    static void requireAvailableInterface(Context context, String iface) {
        InterfaceState state = interfaceState(context, iface);
        if (state == InterfaceState.UNKNOWN) throw new IllegalStateException("guest network interface is unknown");
        if (state == InterfaceState.BUSY) throw new IllegalStateException("guest interface already belongs to a network");
    }

    static boolean waitForAvailableInterface(Context context, String iface, long timeoutMs) throws InterruptedException {
        if (timeoutMs < 0) throw new IllegalArgumentException("negative timeout");
        long deadline = SystemClock.elapsedRealtime() + timeoutMs;
        for (;;) {
            InterfaceState state = interfaceState(context, iface);
            if (state == InterfaceState.AVAILABLE) return true;
            long left = deadline - SystemClock.elapsedRealtime();
            if (left <= 0) {
                System.out.println("aemu-wifi: guest interface did not become available: " + state);
                return false;
            }
            Thread.sleep(Math.min(left, 100));
        }
    }
}
