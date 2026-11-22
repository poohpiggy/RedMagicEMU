package app.aemu.stub;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

final class LocationDemand {
    private static final long LEASE_MS = 4000;

    private final Path path;
    private boolean requested;
    private boolean needsClear = true;

    LocationDemand(Path path) {
        this.path = path;
    }

    boolean setRequested(boolean active) {
        if (requested == active) return false;
        requested = active;
        return true;
    }

    boolean isRequested() {
        return requested;
    }

    void refresh(long elapsedRealtimeMs) throws IOException {
        if (!requested && !needsClear) return;
        String message = requested ? "1 " + (elapsedRealtimeMs + LEASE_MS) + "\n" : "0\n";
        Path tmp = Files.createTempFile(path.getParent(), ".aemu-location-demand-", ".tmp");
        try {
            Files.write(tmp, message.getBytes(StandardCharsets.US_ASCII));
            Files.move(tmp, path, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            needsClear = requested;
        } finally {
            Files.deleteIfExists(tmp);
        }
    }
}
