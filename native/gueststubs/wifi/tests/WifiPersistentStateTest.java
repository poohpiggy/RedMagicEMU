package app.aemu.stub;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Tests the guest's persisted switch and saved-network state on real files. */
public final class WifiPersistentStateTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    private static WifiPersistentState makeStore(File file, String ssid, String bssid, boolean android) {
        return android ? new WifiPersistentState(file, ssid, bssid)
                : new WifiPersistentState(file, ssid, bssid, directory -> { });
    }

    public static void main(String[] args) throws Exception {
        File dir = (args.length == 0 ? Files.createTempDirectory("rme-wifi-state-")
                : Files.createTempDirectory(new File(args[0]).toPath(), "rme-wifi-state-")).toFile();
        try {
            File file = new File(dir, "wifi.state");
            boolean android = args.length != 0;
            WifiPersistentState store = makeStore(file, "RedMagicEMU", "02:00:00:00:00:01", android);
            WifiPersistentState.Snapshot initial = store.read();
            check(initial.enabled && initial.saved, "first boot defaults");
            for (boolean enabled : new boolean[]{ false, true }) for (boolean saved : new boolean[]{ false, true }) {
                store.write(enabled, saved);
                WifiPersistentState.Snapshot state = store.read();
                check(state.enabled == enabled && state.saved == saved, "round trip");
            }
            store.write(false, false);
            WifiPersistentState.Snapshot changed = makeStore(file, "Other AP", "02:00:00:00:00:01", android).read();
            check(!changed.enabled && changed.saved, "new hotspot must not inherit forgotten state");
            Files.write(file.toPath(), "bad\n".getBytes(StandardCharsets.US_ASCII));
            try { store.read(); throw new AssertionError("corrupt state accepted"); }
            catch (IOException expected) { }
            File missing = new File(new File(dir, "missing"), "wifi.state");
            try { makeStore(missing, "x", "y", android).read(); throw new AssertionError("missing parent treated as first boot"); }
            catch (IOException expected) { }
            try { makeStore(missing, "x", "y", android).write(true, true); throw new AssertionError("missing parent accepted"); }
            catch (IOException expected) { }
            File uncertain = new File(dir, "sync-failure.state");
            WifiPersistentState failingSync = new WifiPersistentState(uncertain, "x", "y", directory -> {
                throw new IOException("injected directory sync failure");
            });
            try { failingSync.write(false, false); throw new AssertionError("directory sync failure accepted"); }
            catch (WifiPersistentState.DurabilityException expected) { }
            System.out.println("Wi-Fi persisted state tests passed");
        } finally {
            File[] entries = dir.listFiles();
            if (entries != null) for (File entry : entries) Files.deleteIfExists(entry.toPath());
            Files.deleteIfExists(dir.toPath());
        }
    }
}
