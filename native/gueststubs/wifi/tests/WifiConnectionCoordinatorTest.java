package app.aemu.stub;

import java.nio.file.Files;
import java.nio.file.Path;

public final class WifiConnectionCoordinatorTest {
    private static final class Fake implements WifiNetworkLifecycle.Factory, WifiNetworkLifecycle.Connection {
        boolean failStart, failStop;
        int starts, stops;
        public WifiNetworkLifecycle.Connection start() throws Exception {
            starts++;
            if (failStart) throw new Exception("start failed");
            return this;
        }
        public void stop() throws Exception {
            stops++;
            if (failStop) throw new Exception("stop failed");
        }
    }
    private static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) throws Exception {
        Path gatePath = Files.createTempFile("rme-coordinator-gate-", ".state");
        System.setProperty(WifiNetworkGate.PATH_PROPERTY, gatePath.toString());
        try {
        VirtualWifiState state = new VirtualWifiState("RedMagicEMU", "");
        Fake backend = new Fake();
        WifiNetworkLifecycle network = new WifiNetworkLifecycle(backend);
        WifiConnectionCoordinator coordinator = new WifiConnectionCoordinator(state, network);
        coordinator.setEnabled(true);
        backend.failStart = true;
        try { coordinator.connect("RedMagicEMU", ""); throw new AssertionError(); }
        catch (Exception expected) { }
        check(state.isEnabled() && !state.isConnected() && !state.hasSavedNetwork() && !network.isConnected());
        backend.failStart = false;
        check(coordinator.connect("RedMagicEMU", "") == VirtualWifiState.Result.SUCCESS);
        check(state.isConnected() && state.hasSavedNetwork() && network.isConnected());
        check(coordinator.connect("RedMagicEMU", "password") == VirtualWifiState.Result.UNSUPPORTED_SECURITY);
        check(state.isConnected() && network.isConnected() && backend.starts == 2);
        backend.failStop = true;
        try { coordinator.setEnabled(false); throw new AssertionError(); } catch (Exception expected) { }
        check(state.isEnabled() && state.isConnected() && state.hasSavedNetwork()
                && coordinator.networkStatus() == WifiNetworkLifecycle.Status.UNKNOWN);
        try { coordinator.forget(); throw new AssertionError(); } catch (IllegalStateException expected) { }
        check(state.isConnected() && state.hasSavedNetwork() && backend.starts == 2);
        state = new VirtualWifiState("RedMagicEMU", "");
        backend = new Fake();
        network = new WifiNetworkLifecycle(backend);
        coordinator = new WifiConnectionCoordinator(state, network);
        coordinator.setEnabled(true);
        coordinator.connect("RedMagicEMU", "");
        coordinator.disconnect();
        check(!state.isConnected() && !network.isConnected() && state.hasSavedNetwork());
        check(coordinator.reconnect() == VirtualWifiState.Result.SUCCESS);
        check(state.isConnected() && network.isConnected());
        coordinator.setHostInternet(VirtualWifiState.HostInternet.UNAVAILABLE);
        check(state.isConnected() && network.isConnected());
        coordinator.forget();
        check(!state.isConnected() && !state.hasSavedNetwork() && !network.isConnected());
        check(coordinator.reconnect() == VirtualWifiState.Result.NETWORK_NOT_FOUND);
        check(backend.starts == 2 && backend.stops == 2);
        System.out.println("WifiConnectionCoordinatorTest passed");
        } finally {
            Files.deleteIfExists(gatePath);
        }
    }
}
