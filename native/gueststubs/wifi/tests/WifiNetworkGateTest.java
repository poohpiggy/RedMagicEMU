package app.aemu.stub;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

public final class WifiNetworkGateTest {
    public static void main(String[] args) throws Exception {
        Path directory = Files.createTempDirectory("rme-wifi-gate-");
        try {
            Path state = directory.resolve("network-gate");
            WifiNetworkGate gate = new WifiNetworkGate(state);
            gate.setConnectedForTest(false);
            check("0\n".equals(new String(Files.readAllBytes(state), StandardCharsets.US_ASCII)),
                    "disabled Wi-Fi must close the guest network gate");
            gate.setConnectedForTest(true);
            check("1\n".equals(new String(Files.readAllBytes(state), StandardCharsets.US_ASCII)),
                    "connected Wi-Fi must open the guest network gate");
            gate.setConnectedForTest(false);
            check("0\n".equals(new String(Files.readAllBytes(state), StandardCharsets.US_ASCII)),
                    "disconnect must overwrite an earlier open gate");

            String coordinator = new String(Files.readAllBytes(
                    Paths.get("src/app/aemu/stub/WifiConnectionCoordinator.java")), StandardCharsets.UTF_8);
            check(coordinator.contains("WifiNetworkGate.setConnected(false)"),
                    "network transition is not wired to close the gate");
            check(coordinator.contains("WifiNetworkGate.setConnected(true)"),
                    "network transition is not wired to open the gate");

            String stub = new String(Files.readAllBytes(
                    Paths.get("src/app/aemu/stub/WifiStub.java")), StandardCharsets.UTF_8);
            check(stub.contains("WifiNetworkGate.setConnected(false)"),
                    "Wi-Fi startup must fail closed before Ethernet handoff");
            System.out.println("WifiNetworkGateTest passed");
        } finally {
            Files.deleteIfExists(directory.resolve("network-gate"));
            Files.deleteIfExists(directory);
        }
    }

    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
