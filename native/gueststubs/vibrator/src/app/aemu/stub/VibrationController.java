package app.aemu.stub;

import java.io.IOException;
import java.util.Objects;

/** Unconnected experiment: output is disabled until a trusted authorization path exists. */
public final class VibrationController implements AutoCloseable {
    public static final int MAX_DURATION_MS = 5000;

    /** Implementations must report failure and enforce the duration independently on the host. */
    public interface Sink {
        void play(int durationMs) throws IOException;
        void cancel() throws IOException;
    }

    public VibrationController(Sink sink) { Objects.requireNonNull(sink); }

    static void requireAuthorizedCaller() {
        throw new SecurityException("trusted Guest authorization unavailable");
    }

    public synchronized void on(int callingUid, int durationMs) throws IOException {
        requireAuthorizedCaller();
    }

    public synchronized void off(int callingUid) throws IOException {
        requireAuthorizedCaller();
    }

    @Override public synchronized void close() throws IOException { /* no output can have started */ }
}
