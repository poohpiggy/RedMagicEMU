package app.aemu.stub;

public final class WifiAirplaneModeObserverTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        final boolean[] wifi = { true };
        final int[] calls = { 0 };
        final boolean[] ready = { true };
        WifiAirplaneModeObserver observer = new WifiAirplaneModeObserver(() -> wifi[0], enabled -> {
            calls[0]++;
            if (!ready[0]) return false;
            wifi[0] = enabled;
            return true;
        });
        observer.observe(false);
        check(wifi[0] && calls[0] == 0, "initial sample must preserve Wi-Fi");
        observer.observe(true);
        check(!wifi[0] && calls[0] == 1, "airplane mode did not disable Wi-Fi");
        wifi[0] = true; // User deliberately enables Wi-Fi during airplane mode.
        observer.observe(true);
        check(wifi[0] && calls[0] == 1, "repeated observation undid manual enable");
        observer.observe(false);
        check(wifi[0], "leaving airplane mode must preserve the user choice");
        ready[0] = false;
        observer.observe(true);
        check(wifi[0], "failed transition must not report a change");
        ready[0] = true;
        observer.observe(true);
        check(!wifi[0] && calls[0] == 3, "failed transition was not retried");

        WifiAirplaneModeObserver restarted = new WifiAirplaneModeObserver(() -> wifi[0], enabled -> {
            throw new AssertionError("restart must preserve an existing manual choice");
        });
        restarted.observe(true);
        restarted.observe(true);

        final boolean[] initiallyOn = { true };
        WifiAirplaneModeObserver restoreOnExit = new WifiAirplaneModeObserver(
                () -> initiallyOn[0], enabled -> { initiallyOn[0] = enabled; return true; });
        restoreOnExit.observe(false);
        restoreOnExit.observe(true);
        check(!initiallyOn[0], "airplane mode must disable previously enabled Wi-Fi");
        restoreOnExit.observe(false);
        check(initiallyOn[0], "exiting airplane mode must restore previously enabled Wi-Fi");

        final boolean[] initiallyOff = { false };
        WifiAirplaneModeObserver stayOff = new WifiAirplaneModeObserver(
                () -> initiallyOff[0], enabled -> { initiallyOff[0] = enabled; return true; });
        stayOff.observe(false);
        stayOff.observe(true);
        stayOff.observe(false);
        check(!initiallyOff[0], "previously disabled Wi-Fi must stay disabled");

        final boolean[] retryWifi = { true }, retryReady = { true };
        WifiAirplaneModeObserver retryRestore = new WifiAirplaneModeObserver(
                () -> retryWifi[0], enabled -> {
                    if (!retryReady[0]) return false;
                    retryWifi[0] = enabled;
                    return true;
                });
        retryRestore.observe(false);
        retryRestore.observe(true);
        retryReady[0] = false;
        retryRestore.observe(false);
        check(!retryWifi[0], "failed restore must not report Wi-Fi enabled");
        retryReady[0] = true;
        retryRestore.observe(false);
        check(retryWifi[0], "failed restore must be retried");
        System.out.println("WIFI_AIRPLANE_OBSERVER_OK");
    }
}
