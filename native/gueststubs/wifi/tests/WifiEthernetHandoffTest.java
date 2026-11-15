package app.aemu.stub;

public final class WifiEthernetHandoffTest {
    static final class Backend implements WifiEthernetHandoff.Backend {
        int stops, starts, cleanups, restores;
        boolean oldGone = true, failStart, cleanupGone = true, restored = true;
        public Object oldNetwork() { return "old"; }
        public void stopEthernet() { stops++; }
        public boolean waitGone(Object network) { return oldGone; }
        public void startWifi() throws Exception { starts++; if (failStart) throw new Exception("wifi failed"); }
        public boolean cleanupWifi() { cleanups++; return cleanupGone; }
        public void restoreEthernet() { restores++; }
        public boolean waitEthernetRestored() { return restored; }
    }
    static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) throws Exception {
        Backend ok = new Backend(); new WifiEthernetHandoff(ok).run();
        check(ok.stops == 1 && ok.starts == 1 && ok.restores == 0);
        Backend failed = new Backend(); failed.failStart = true;
        try { new WifiEthernetHandoff(failed).run(); throw new AssertionError(); }
        catch (WifiEthernetHandoff.FailedAndRestored expected) { }
        check(failed.cleanups == 1 && failed.restores == 1);
        Backend pending = new Backend(); pending.oldGone = false;
        try { new WifiEthernetHandoff(pending).run(); throw new AssertionError(); }
        catch (WifiEthernetHandoff.Uncertain expected) { }
        check(pending.starts == 0 && pending.restores == 0);
        Backend unknown = new Backend(); unknown.failStart = true; unknown.cleanupGone = false;
        try { new WifiEthernetHandoff(unknown).run(); throw new AssertionError(); }
        catch (WifiEthernetHandoff.Uncertain expected) { }
        check(unknown.cleanups == 1 && unknown.restores == 0);
        Backend restoreFail = new Backend(); restoreFail.failStart = true; restoreFail.restored = false;
        try { new WifiEthernetHandoff(restoreFail).run(); throw new AssertionError(); }
        catch (WifiEthernetHandoff.Uncertain expected) { }
        check(restoreFail.restores == 1);
        System.out.println("WifiEthernetHandoffTest passed");
    }
}
