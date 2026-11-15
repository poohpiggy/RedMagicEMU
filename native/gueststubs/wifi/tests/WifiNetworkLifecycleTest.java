package app.aemu.stub;

public final class WifiNetworkLifecycleTest {
    private static final class Fake implements WifiNetworkLifecycle.Factory, WifiNetworkLifecycle.Connection {
        int starts;
        int stops;
        boolean failStart;
        boolean failStop;
        boolean uncertainStart;
        @Override public WifiNetworkLifecycle.Connection start() throws Exception {
            starts++;
            if (uncertainStart) throw new WifiNetworkLifecycle.UncertainStartException(
                    this, new Exception("connect failed"), new Exception("cleanup failed"));
            if (failStart) throw new Exception("start failed");
            return this;
        }
        @Override public void stop() throws Exception {
            stops++;
            if (failStop) throw new Exception("stop failed");
        }
    }
    private static void check(boolean condition) {
        if (!condition) throw new AssertionError();
    }
    public static void main(String[] args) throws Exception {
        Fake backend = new Fake();
        WifiNetworkLifecycle network = new WifiNetworkLifecycle(backend);
        backend.failStart = true;
        try { network.setConnected(true); throw new AssertionError(); } catch (Exception expected) { }
        check(!network.isConnected() && backend.starts == 1);
        backend.failStart = false;
        network.setConnected(true);
        network.setConnected(true);
        check(network.isConnected() && backend.starts == 2);
        backend.failStop = true;
        try { network.setConnected(false); throw new AssertionError(); } catch (Exception expected) { }
        check(!network.isConnected() && network.status() == WifiNetworkLifecycle.Status.UNKNOWN
                && network.hasUncertainAgent() && backend.stops == 1);
        try { network.setConnected(true); throw new AssertionError(); } catch (IllegalStateException expected) { }
        check(backend.starts == 2);
        try { network.setConnected(false); throw new AssertionError(); } catch (IllegalStateException expected) { }
        check(backend.stops == 1);
        Fake uncertain = new Fake();
        uncertain.uncertainStart = true;
        WifiNetworkLifecycle network2 = new WifiNetworkLifecycle(uncertain);
        try { network2.setConnected(true); throw new AssertionError(); } catch (WifiNetworkLifecycle.UncertainStartException expected) { }
        check(network2.status() == WifiNetworkLifecycle.Status.UNKNOWN && network2.hasUncertainAgent());
        try { network2.setConnected(true); throw new AssertionError(); } catch (IllegalStateException expected) { }
        check(uncertain.starts == 1);
        WifiNetworkLifecycle nullStart = new WifiNetworkLifecycle(new WifiNetworkLifecycle.Factory() {
            public WifiNetworkLifecycle.Connection start() { return null; }
        });
        try { nullStart.setConnected(true); throw new AssertionError(); } catch (IllegalStateException expected) { }
        check(nullStart.status() == WifiNetworkLifecycle.Status.UNKNOWN && !nullStart.hasUncertainAgent());
        try { nullStart.setConnected(true); throw new AssertionError(); } catch (IllegalStateException expected) { }
        System.out.println("WifiNetworkLifecycleTest passed");
    }
}
