package app.aemu.stub;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.nio.file.Paths;

/** One-byte state consumed by the API 36 engine before guest Internet traffic. */
final class RadioNetworkGate {
    static final String PATH_PROPERTY = "app.aemu.networkGatePath";
    static final Path PATH = Paths.get("/dev/aemu_network_gate");
    private static volatile RadioNetworkGate defaultGate;
    private final Path path;

    RadioNetworkGate() {
        this(PATH);
    }

    RadioNetworkGate(Path path) {
        if (path == null) throw new IllegalArgumentException("network gate path required");
        this.path = path;
    }

    void setConnected(boolean connected) throws IOException {
        File file = path.toFile();
        try (FileOutputStream output = new FileOutputStream(file, false)) {
            output.write((connected ? "1\n" : "0\n").getBytes(StandardCharsets.US_ASCII));
            output.getFD().sync();
        }
    }

    static RadioNetworkGate defaultGate() {
        RadioNetworkGate current = defaultGate;
        if (current != null) return current;
        synchronized (RadioNetworkGate.class) {
            current = defaultGate;
            if (current == null) {
                String override = System.getProperty(PATH_PROPERTY);
                defaultGate = current = new RadioNetworkGate(override == null ? PATH : Paths.get(override));
            }
            return current;
        }
    }
}
