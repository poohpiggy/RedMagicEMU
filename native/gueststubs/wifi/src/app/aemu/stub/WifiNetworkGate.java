package app.aemu.stub;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.nio.file.Paths;

/** One-byte state file consumed by the API 36 engine before guest Internet traffic. */
final class WifiNetworkGate {
    static final String PATH_PROPERTY = "app.aemu.networkGatePath";
    static final Path PATH = Paths.get("/dev/aemu_network_gate");
    private static volatile WifiNetworkGate defaultGate;
    private final Path path;

    WifiNetworkGate(Path path) {
        if (path == null) throw new IllegalArgumentException("network gate path required");
        this.path = path;
    }

    static void setConnected(boolean connected) throws IOException {
        defaultGate().set(connected);
    }

    void setConnectedForTest(boolean connected) throws IOException {
        set(connected);
    }

    private void set(boolean connected) throws IOException {
        File file = path.toFile();
        try (FileOutputStream output = new FileOutputStream(file, false)) {
            output.write((connected ? "1\n" : "0\n").getBytes(StandardCharsets.US_ASCII));
            output.getFD().sync();
        }
    }

    private static WifiNetworkGate defaultGate() {
        WifiNetworkGate current = defaultGate;
        if (current != null) return current;
        synchronized (WifiNetworkGate.class) {
            current = defaultGate;
            if (current == null) {
                String override = System.getProperty(PATH_PROPERTY);
                defaultGate = current = new WifiNetworkGate(override == null ? PATH : Paths.get(override));
            }
            return current;
        }
    }
}
