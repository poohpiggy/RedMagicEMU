package app.aemu.stub;

import android.os.IBinder;
import android.os.SystemClock;

/** Waits for the Ethernet handoff service without changing either network. */
final class WifiControlWaiter {
    interface Finder {
        IBinder find() throws Exception;
        boolean isReady(IBinder control) throws Exception;
    }
    interface Clock {
        long elapsedRealtime();
        void sleep(long milliseconds) throws InterruptedException;
    }

    static final Clock SYSTEM_CLOCK = new Clock() {
        @Override public long elapsedRealtime() { return SystemClock.elapsedRealtime(); }
        @Override public void sleep(long milliseconds) throws InterruptedException { Thread.sleep(milliseconds); }
    };

    private WifiControlWaiter() { }

    static IBinder await(Finder finder, Clock clock, long timeoutMs) throws Exception {
        if (finder == null || clock == null || timeoutMs < 0)
            throw new IllegalArgumentException("control wait dependencies and timeout are required");
        long started = clock.elapsedRealtime();
        for (;;) {
            if (Thread.currentThread().isInterrupted())
                throw new InterruptedException("Ethernet control wait interrupted");
            long elapsed = clock.elapsedRealtime() - started;
            if (elapsed < 0) throw new IllegalStateException("control wait clock moved backwards");
            if (elapsed > timeoutMs)
                throw new IllegalStateException("Ethernet control unavailable; existing network preserved");
            IBinder control;
            try {
                control = finder.find();
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                throw interrupted;
            }
            if (Thread.currentThread().isInterrupted())
                throw new InterruptedException("Ethernet control wait interrupted");
            elapsed = clock.elapsedRealtime() - started;
            if (elapsed < 0) throw new IllegalStateException("control wait clock moved backwards");
            if (elapsed > timeoutMs)
                throw new IllegalStateException("Ethernet control unavailable; existing network preserved");
            if (control != null && control.isBinderAlive()) {
                boolean ready;
                try {
                    ready = finder.isReady(control);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw interrupted;
                }
                if (Thread.currentThread().isInterrupted())
                    throw new InterruptedException("Ethernet control wait interrupted");
                elapsed = clock.elapsedRealtime() - started;
                if (elapsed < 0) throw new IllegalStateException("control wait clock moved backwards");
                if (elapsed > timeoutMs)
                    throw new IllegalStateException("Ethernet control unavailable; existing network preserved");
                if (ready && control.isBinderAlive()) return control;
            }
            if (elapsed >= timeoutMs)
                throw new IllegalStateException("Ethernet control unavailable; existing network preserved");
            try {
                clock.sleep(Math.min(100L, timeoutMs - elapsed));
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                throw interrupted;
            }
        }
    }
}
