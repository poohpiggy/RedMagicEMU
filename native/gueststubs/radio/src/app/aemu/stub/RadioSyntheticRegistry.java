package app.aemu.stub;

import android.os.IBinder;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

/** Experimental registry: only synthetic test names, never production Radio names. */
final class RadioSyntheticRegistry implements AutoCloseable {
    interface Backend {
        boolean exists(String name) throws Exception;
        void add(String name, IBinder binder) throws Exception;
        boolean ready(String name, IBinder expected) throws Exception;
    }
    static final class StartFailure extends Exception {
        StartFailure(String phase, Throwable cause) { super(phase, cause); }
    }
    static final String[] SUFFIXES = {"config", "modem", "sim", "network", "data", "messaging", "voice"};
    private final Backend backend;
    private final IBinder[] binders;
    private final String[] names = new String[7];
    private final long timeoutMs;
    private final ExecutorService worker = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "radio-synthetic-registry");
        t.setDaemon(true);
        return t;
    });
    private boolean started, ready, exitRequired, closed;

    RadioSyntheticRegistry(Backend backend, String token, IBinder[] binders, long timeoutMs) {
        if (backend == null || token == null || !token.matches("[0-9a-f]{32}") ||
                binders == null || binders.length != 7 || timeoutMs < 1 || timeoutMs > 30000)
            throw new IllegalArgumentException("invalid synthetic registry arguments");
        this.binders = binders.clone();
        for (int i = 0; i < 7; i++) {
            if (this.binders[i] == null) throw new IllegalArgumentException("null binder");
            for (int j = 0; j < i; j++) if (this.binders[i] == this.binders[j])
                throw new IllegalArgumentException("duplicate binder");
            names[i] = "app.redmagicemu.test.radio." + token + "." + SUFFIXES[i];
        }
        this.backend = backend;
        this.timeoutMs = timeoutMs;
    }

    private <T> T bounded(Callable<T> operation, long deadline) throws Exception {
        long remaining = deadline - System.nanoTime();
        if (remaining <= 0) throw new java.util.concurrent.TimeoutException();
        Future<T> future = worker.submit(operation);
        try { return future.get(remaining, TimeUnit.NANOSECONDS); }
        catch (Exception e) { future.cancel(true); throw e; }
    }

    synchronized void start() throws StartFailure {
        if (started || closed) throw new IllegalStateException("already started or closed");
        started = true;
        long deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeoutMs);
        String phase = "precheck";
        try {
            for (String name : names) if (bounded(() -> backend.exists(name), deadline))
                throw new IllegalStateException("name occupied");
            phase = "publish";
            for (int i = 0; i < names.length; i++) {
                final int n = i;
                bounded(() -> { backend.add(names[n], binders[n]); return null; }, deadline);
            }
            phase = "ready";
            for (int i = 0; i < names.length; i++) {
                final int n = i;
                while (!bounded(() -> backend.ready(names[n], binders[n]), deadline)) {
                    long remaining = deadline - System.nanoTime();
                    if (remaining <= 0) throw new java.util.concurrent.TimeoutException();
                    TimeUnit.NANOSECONDS.sleep(Math.min(remaining, TimeUnit.MILLISECONDS.toNanos(10)));
                }
            }
            ready = true;
        } catch (Exception e) {
            exitRequired = true;
            worker.shutdownNow();
            if (e instanceof InterruptedException) Thread.currentThread().interrupt();
            throw new StartFailure(phase, e);
        }
    }
    synchronized boolean isReady() { return ready; }
    synchronized boolean mustExitProcess() { return exitRequired; }
    @Override public synchronized void close() {
        ready = false;
        if (started) exitRequired = true;
        closed = true;
        worker.shutdownNow();
    }
}
