package app.aemu.stub;

import android.os.IBinder;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

/** Isolated one-service VINTF experiment. Never used for production registration. */
final class RadioCanaryRegistration implements AutoCloseable {
    static final String PREFIX = "android.hardware.radio.config.IRadioConfig/rme";
    interface Backend {
        boolean declared(String name) throws Exception;
        boolean exists(String name) throws Exception;
        void add(String name, IBinder binder) throws Exception;
        boolean ready(String name, IBinder binder) throws Exception;
    }
    static final class StartFailure extends Exception {
        StartFailure(String phase, Throwable cause) { super(phase, cause); }
    }
    private final Backend backend;
    private final IBinder binder;
    private final String name;
    private final long timeoutMs;
    private final ExecutorService worker = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "radio-canary-registry"); t.setDaemon(true); return t;
    });
    private boolean started, ready, exitRequired, closed;

    RadioCanaryRegistration(Backend backend, String token, IBinder binder, long timeoutMs) {
        if (backend == null || token == null || !token.matches("[0-9a-f]{32}") || binder == null ||
                timeoutMs < 1 || timeoutMs > 30000) throw new IllegalArgumentException("invalid canary arguments");
        this.backend = backend; this.binder = binder; this.timeoutMs = timeoutMs; this.name = PREFIX + token;
    }
    private <T> T bounded(Callable<T> operation, long deadline) throws Exception {
        long remaining = deadline - System.nanoTime();
        if (remaining <= 0) throw new java.util.concurrent.TimeoutException();
        Future<T> result = worker.submit(operation);
        try { return result.get(remaining, TimeUnit.NANOSECONDS); }
        catch (Exception e) { result.cancel(true); throw e; }
    }
    synchronized void start() throws StartFailure {
        if (started || closed) throw new IllegalStateException("already started or closed");
        started = true;
        long deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeoutMs);
        String phase = "declaration";
        try {
            if (!bounded(() -> backend.declared(name), deadline)) throw new IllegalStateException("not declared");
            phase = "collision";
            if (bounded(() -> backend.exists(name), deadline)) throw new IllegalStateException("name occupied");
            phase = "publish";
            bounded(() -> { backend.add(name, binder); return null; }, deadline);
            phase = "ready";
            while (!bounded(() -> backend.ready(name, binder), deadline)) {
                long left = deadline - System.nanoTime();
                if (left <= 0) throw new java.util.concurrent.TimeoutException();
                TimeUnit.NANOSECONDS.sleep(Math.min(left, TimeUnit.MILLISECONDS.toNanos(10)));
            }
            ready = true;
        } catch (Exception e) {
            exitRequired = true; worker.shutdownNow();
            if (e instanceof InterruptedException) Thread.currentThread().interrupt();
            throw new StartFailure(phase, e);
        }
    }
    synchronized boolean isReady() { return ready; }
    synchronized boolean mustExitProcess() { return exitRequired; }
    @Override public synchronized void close() {
        if (started) exitRequired = true;
        ready = false; closed = true; worker.shutdownNow();
    }
}
