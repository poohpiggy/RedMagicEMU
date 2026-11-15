/* Modified for AEmulator Plus, 2026-11-01: Android 8 - 17. GPL-3.0; see LICENSE and NOTICE.md.
 * Modified for RedMagicEMU, 2026-11-10: target API 36, one-time service replacement and fail-closed network handoff. */
package app.aemu.stub;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.os.IBinder;
import android.os.Binder;
import android.os.Looper;
import android.os.Parcel;
import android.os.SystemClock;
import java.lang.reflect.Method;
import java.net.InetAddress;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicReference;

/** Target guest entry point for the virtual Wi-Fi service. */
public final class WifiStub {
    private static final boolean SERVICE_IMPLEMENTATION_READY = true;
    private static final IBinder HANDOFF_SESSION = new Binder();
    private static volatile WifiGuestNetwork network;

    static synchronized void attachEarlyNetwork(WifiGuestNetwork value) {
        if (value == null || network != null) throw new IllegalStateException("guest network already assigned");
        network = value;
    }

    private WifiStub() { }

    static void requireServiceReady() {
        if (!SERVICE_IMPLEMENTATION_READY) throw new IllegalStateException("guest Wi-Fi service is incomplete");
    }

    public static void main(String[] args) throws Exception {
        try {
            if (!SERVICE_IMPLEMENTATION_READY) throw new IllegalStateException("guest Wi-Fi service is incomplete");
            EarlyWifiPublishExperiment.main(args);
        } catch (Exception failure) {
            System.out.println("aemu-wifi: startup failed: " + failure.getClass().getName());
            failure.printStackTrace(System.out);
            System.out.flush();
            throw failure;
        } catch (Error failure) {
            System.out.println("aemu-wifi: startup failed: " + failure.getClass().getName());
            failure.printStackTrace(System.out);
            System.out.flush();
            throw failure;
        }
    }

    private static IBinder checkService(String name) throws Exception {
        return (IBinder) Class.forName("android.os.ServiceManager").getMethod("checkService", String.class).invoke(null, name);
    }

    private static boolean isOurs(IBinder binder, WifiService service) throws Exception {
        if (binder == service) return true;
        if (binder == null) return false;
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        try {
            return binder.transact(WifiService.PROBE, data, reply, 0) && reply.readInt() == WifiService.PROBE_ANSWER;
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    private static void run() throws Exception {
        Refl.log("stage: prepare guest looper; guest pid=" + android.os.Process.myPid());
        Looper.prepare();
        final Looper looper = Looper.myLooper();
        final WifiConfig cfg = WifiConfig.load();
        if (cfg == null) throw new IllegalStateException("guest Wi-Fi configuration unavailable");
        Refl.log("stage: obtain guest system context");
        Context context = WifiSystemContext.obtain();
        Refl.log("stage: wait for boot completion");
        Method property = Class.forName("android.os.SystemProperties").getMethod("get", String.class);
        for (int i = 0; i < 600 && !"1".equals(property.invoke(null, "sys.boot_completed")); i++) Thread.sleep(500);
        if (!"1".equals(property.invoke(null, "sys.boot_completed"))) throw new IllegalStateException("guest boot incomplete");
        Refl.log("stage: construct Wi-Fi Binder");
        // The engine rejects Internet sends while this byte is not exactly "1".
        // Start closed so a failed handoff cannot leave the removed Ethernet
        // placeholder usable through the guest's direct host sockets.
        WifiNetworkGate.setConnected(false);
        final WifiPersistentState stateStore = new WifiPersistentState(
                new java.io.File("/data/system/redmagic-wifi-state"), cfg.ssid, cfg.bssid);
        final WifiService service = new WifiService(cfg, looper, context, stateStore);
        Refl.log("stage: boot completed; check connectivity");
        IBinder connectivity = checkService("connectivity");
        if (connectivity == null) throw new IllegalStateException("guest connectivity unavailable");
        Refl.log("stage: construct network link properties");
        LinkProperties link = VirtualWifiLinkProperties.create("eth0", InetAddress.getByName(cfg.ip), cfg.prefix,
                InetAddress.getByName(cfg.gateway), Arrays.asList(InetAddress.getByName(cfg.dns1), InetAddress.getByName(cfg.dns2)));
        final AtomicReference<Object> candidateWifiInfo = new AtomicReference<Object>();
        final WifiGuestNetwork guest = new WifiGuestNetwork(context, looper, link, new VirtualWifiState(cfg.ssid, cfg.psk),
                service.networkTransitionLock(),
                new WifiGuestNetwork.WifiInfoSource() {
                    @Override public Object get() { return candidateWifiInfo.get(); }
                }, new WifiGuestNetwork.FailureListener() {
                    @Override public void onFailure(Exception failure) { service.onNetworkFailure(failure); }
                    @Override public void onNetworkUnwanted() { service.onNetworkUnwanted(); }
                });
        network = guest;
        final WifiService.Listener listener = new WifiService.Listener() {
            @Override public synchronized boolean onConnectionChanged(boolean connected, Object wifiInfo) {
                candidateWifiInfo.set(wifiInfo);
                try {
                    if (connected) {
                        guest.setEnabled(true);
                        return guest.connect(cfg.ssid, cfg.psk) == VirtualWifiState.Result.SUCCESS;
                    }
                    guest.disconnect();
                    return true;
                } catch (Exception failure) {
                    if (guest.networkStatus() == WifiNetworkLifecycle.Status.UNKNOWN) service.onNetworkFailure(failure);
                    Refl.log("Wi-Fi network transition failed: " + failure);
                    return false;
                } finally {
                    candidateWifiInfo.set(null);
                }
            }
        };
        service.setListener(listener);
        Refl.log("stage: check existing Wi-Fi and Ethernet control services");
        final Class<?> manager = Class.forName("android.os.ServiceManager");
        Refl.log("stage: query original Wi-Fi Binder");
        final IBinder previous = checkService("wifi");
        Refl.log("stage: original Wi-Fi Binder query returned");
        Refl.log("stage: verify original Wi-Fi Binder is alive");
        if (previous == null || !previous.isBinderAlive()) throw new IllegalStateException("original Wi-Fi service unavailable for rollback");
        Refl.log("stage: original Wi-Fi Binder is alive; wait for Ethernet control");
        final Context guestContext = context;
        final IBinder control = WifiControlWaiter.await(new WifiControlWaiter.Finder() {
            @Override public IBinder find() throws Exception { return checkService("redmagicemu.network.control"); }
            @Override public boolean isReady(IBinder candidate) throws Exception {
                ControlStatus status = controlStatus(candidate);
                return WifiControlReadiness.isReady(status.state, status.network,
                        guestContext.getSystemService(ConnectivityManager.class));
            }
        }, WifiControlWaiter.SYSTEM_CLOCK, 30000);
        Refl.log("stage: Ethernet control query returned");
        if (!previous.isBinderAlive()) throw new IllegalStateException("original Wi-Fi service unavailable for rollback");
        new WifiEthernetHandoff(new WifiEthernetHandoff.Backend() {
            private Network oldNetwork;
            private boolean replacementAttempted;
            @Override public Object oldNetwork() throws Exception {
                Refl.log("stage: request Ethernet control status");
                ControlStatus status = controlStatus(control);
                if (!WifiControlReadiness.isReady(status.state, status.network,
                        guestContext.getSystemService(ConnectivityManager.class)))
                    throw new IllegalStateException("Ethernet network not ready; existing network preserved");
                oldNetwork = status.network;
                return oldNetwork;
            }
            @Override public void stopEthernet() throws Exception {
                Refl.log("stage: request Ethernet stop");
                if (controlCommand(control, IBinder.FIRST_CALL_TRANSACTION + 1, previous, service) != 1)
                    throw new IllegalStateException("Ethernet stop not accepted");
            }
            @Override public boolean waitGone(Object old) throws Exception {
                Refl.log("stage: wait for Ethernet removal");
                if (!waitForNetwork(guestContext, (Network) old, false)) return false;
                return WifiGuestNetwork.waitForAvailableInterface(guestContext, "eth0", 15000);
            }
            @Override public void startWifi() throws Exception {
                if (service.isConnected()) {
                    Refl.log("stage: register Wi-Fi network");
                    if (!listener.onConnectionChanged(true, service.wifiInfo(true)))
                        throw new IllegalStateException("initial Wi-Fi network registration failed");
                    Network current = guest.network();
                    if (current == null || !waitForNetwork(guestContext, current, true))
                        throw new IllegalStateException("Wi-Fi network registration not confirmed");
                } else {
                    Refl.log("stage: Wi-Fi starts disconnected; no network to register");
                }
                // Publish and announce under the transition lock so a newly published client cannot
                // queue a later state change before the initial state has been announced.
                // These broadcasts inform existing listeners; they cannot replace an old Binder held by a client.
                synchronized (service.networkTransitionLock()) {
                    replacementAttempted = true;
                    Refl.log("stage: replace Wi-Fi Binder once");
                    manager.getMethod("addService", String.class, IBinder.class).invoke(null, "wifi", service);
                    Refl.log("stage: Wi-Fi Binder addService returned");
                    if (!isOurs(checkService("wifi"), service)) throw new IllegalStateException("Wi-Fi service replacement not confirmed");
                    Refl.log("stage: Wi-Fi Binder ownership confirmed");
                    try {
                        WifiStateBroadcasts.publishInitial(guestContext, service.isEnabled(), service.isConnected());
                    } catch (RuntimeException failure) {
                        // A failed notification does not undo the already confirmed network. Make it visible in logs.
                        Refl.log("initial Wi-Fi state announcement failed: " + failure);
                    }
                }
            }
            @Override public boolean cleanupWifi() throws Exception {
                Refl.log("stage: clean up Wi-Fi after failed handoff");
                if (replacementAttempted) {
                    IBinder currentWifi = checkService("wifi");
                    if (!previous.equals(currentWifi)) {
                        if (!isOurs(currentWifi, service)) return false;
                        // ServiceManager does not offer atomic compare-and-replace; this remains best effort.
                        manager.getMethod("addService", String.class, IBinder.class).invoke(null, "wifi", previous);
                    }
                    if (!previous.equals(checkService("wifi"))) return false;
                }
                if (guest.networkStatus() == WifiNetworkLifecycle.Status.UNKNOWN) return false;
                Network current = guest.network();
                guest.disconnect();
                if (current != null && !waitForNetwork(guestContext, current, false)) return false;
                return WifiGuestNetwork.waitForAvailableInterface(guestContext, "eth0", 15000);
            }
            @Override public void restoreEthernet() throws Exception {
                Refl.log("stage: request Ethernet restoration");
                if (controlCommand(control, IBinder.FIRST_CALL_TRANSACTION + 2, null, null) != 0)
                    throw new IllegalStateException("Ethernet restore not accepted");
            }
            @Override public boolean waitEthernetRestored() throws Exception {
                Refl.log("stage: confirm Ethernet restoration");
                ControlStatus status = controlStatus(control);
                return status.state == 0 && status.network != null && waitForNetwork(guestContext, status.network, true);
            }
        }).run();
        Refl.log("Wi-Fi service replaced once; existing clients may retain the old Binder");
        Looper.loop();
    }

    private static final class ControlStatus {
        final int state;
        final Network network;
        ControlStatus(int state, Network network) { this.state = state; this.network = network; }
    }

    private static ControlStatus controlStatus(IBinder control) throws Exception {
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken("app.redmagicemu.INetworkControl");
            if (!control.transact(IBinder.FIRST_CALL_TRANSACTION, data, reply, 0))
                throw new IllegalStateException("Ethernet status unavailable");
            reply.readException();
            return new ControlStatus(reply.readInt(), (Network) reply.readParcelable(Network.class.getClassLoader()));
        } finally { data.recycle(); reply.recycle(); }
    }

    private static int controlCommand(IBinder control, int code, IBinder original, IBinder replacement) throws Exception {
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken("app.redmagicemu.INetworkControl");
            data.writeStrongBinder(HANDOFF_SESSION);
            if (code == IBinder.FIRST_CALL_TRANSACTION + 1) {
                data.writeStrongBinder(original);
                data.writeStrongBinder(replacement);
            }
            if (!control.transact(code, data, reply, 0)) throw new IllegalStateException("Ethernet control unavailable");
            reply.readException();
            return reply.readInt();
        } finally { data.recycle(); reply.recycle(); }
    }

    private static boolean waitForNetwork(Context context, Network wanted, boolean present) throws Exception {
        Refl.log("stage: obtain ConnectivityManager for network wait");
        ConnectivityManager manager = context.getSystemService(ConnectivityManager.class);
        if (manager == null) throw new IllegalStateException("guest connectivity unavailable");
        Refl.log("stage: query initial network list");
        long deadline = SystemClock.elapsedRealtime() + 15000;
        boolean first = true;
        do {
            Network[] networks = manager.getAllNetworks();
            if (first) { Refl.log("stage: initial network list returned"); first = false; }
            if (networks == null) throw new IllegalStateException("guest network list unavailable");
            boolean found = false;
            for (Network network : networks) if (wanted.equals(network)) { found = true; break; }
            if (found == present) return true;
            Thread.sleep(100);
        } while (SystemClock.elapsedRealtime() < deadline);
        return false;
    }

    /** Returns null whenever registration is absent or its outcome is unknown. */
    static Network network() {
        WifiGuestNetwork current = network;
        return current == null ? null : current.network();
    }
}
