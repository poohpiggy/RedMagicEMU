package app.aemu.stub;

import android.net.Network;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;

/**
 * API 36 opt-in control of the existing Ethernet placeholder. A successful
 * stop means unregister was requested; ConnectivityService must independently
 * confirm that the returned Network has disappeared before another agent uses
 * its interface. The default NetStub invocation does not publish this Binder.
 */
final class TargetNetControl extends Binder {
    static final String DESCRIPTOR = "app.redmagicemu.INetworkControl";
    static final String SERVICE_NAME = "redmagicemu.network.control";
    static final int STATUS = IBinder.FIRST_CALL_TRANSACTION;
    static final int STOP = IBinder.FIRST_CALL_TRANSACTION + 1;
    static final int RESTORE = IBinder.FIRST_CALL_TRANSACTION + 2;
    static final int STOP_EARLY = IBinder.FIRST_CALL_TRANSACTION + 3;
    private static final int SESSION_PROOF = 0x41454d56;
    // Timed-out Binder calls can remain blocked; do not create unbounded workers on retries.
    private static final java.util.concurrent.Semaphore PROOF_WORKERS = new java.util.concurrent.Semaphore(3);
    private static final int ACTIVE = 0, STOP_REQUESTED = 1, UNKNOWN = 2, UNAVAILABLE = 3, RESTORING = 4;

    interface Lease {
        Network network();
        boolean networkRegistered() throws Exception;
        void stop() throws Exception;
        boolean canRestore() throws Exception;
        void restore() throws Exception;
    }

    interface WifiRegistry {
        IBinder current() throws Exception;
        void publish(IBinder service) throws Exception;
    }

    /** Best effort only: ServiceManager has no atomic compare-and-replace operation. */
    static boolean restoreWifiBinder(WifiRegistry registry, IBinder original, IBinder replacement) throws Exception {
        if (registry == null || original == null || replacement == null || !original.isBinderAlive()) return false;
        IBinder current = registry.current();
        if (original.equals(current)) return true;
        if (current != null && !replacement.equals(current)) return false;
        registry.publish(original);
        return original.equals(registry.current());
    }

    /** The interface changed between observation and registration; no registration was attempted. */
    static final class InterfaceBusyException extends Exception { }

    static void restoreWhenAvailable(Lease lease, long timeoutMs) throws Exception {
        if (timeoutMs < 0) throw new IllegalArgumentException("negative timeout");
        long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.MILLISECONDS.toNanos(timeoutMs);
        for (;;) {
            if (lease.canRestore()) {
                try {
                    lease.restore();
                    return;
                } catch (InterfaceBusyException retry) {
                    // The lease guarantees that it has not started an agent in this case.
                }
            }
            long left = deadline - System.nanoTime();
            if (left <= 0) throw new IllegalStateException("interface unavailable for Ethernet restore");
            long delay = Math.min(100L, Math.max(1L, java.util.concurrent.TimeUnit.NANOSECONDS.toMillis(left)));
            Thread.sleep(delay);
        }
    }

    static void waitForRegistration(Lease lease, long timeoutMs) throws Exception {
        if (timeoutMs < 0) throw new IllegalArgumentException("negative timeout");
        long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.MILLISECONDS.toNanos(timeoutMs);
        for (;;) {
            if (lease.network() != null && lease.networkRegistered()) return;
            long left = deadline - System.nanoTime();
            if (left <= 0) throw new IllegalStateException("Ethernet network registration unconfirmed");
            Thread.sleep(Math.min(100L, Math.max(1L, java.util.concurrent.TimeUnit.NANOSECONDS.toMillis(left))));
        }
    }

    private final Lease lease;
    private int state = ACTIVE;
    private boolean armed;
    private IBinder owner;
    private IBinder.DeathRecipient ownerDeath;
    private IBinder originalWifi;
    private IBinder replacementWifi;
    private boolean earlyOwner;
    private boolean uncertainRecoveryRunning;
    private boolean uncertainRestoreAttempted;
    private long uncertainRecoveryGeneration;

    private static final WifiRegistry WIFI_REGISTRY = new WifiRegistry() {
        @Override public IBinder current() throws Exception {
            return (IBinder) Class.forName("android.os.ServiceManager").getMethod("checkService", String.class)
                    .invoke(null, "wifi");
        }
        @Override public void publish(IBinder service) throws Exception {
            Class.forName("android.os.ServiceManager").getMethod("addService", String.class, IBinder.class)
                    .invoke(null, "wifi", service);
        }
    };

    /** Direct read of the service registry; static checkService may use a stale process cache. */
    private static IBinder currentWifiDirect() throws Exception {
        return bounded(new java.util.concurrent.Callable<IBinder>() {
            @Override public IBinder call() throws Exception {
                Class<?> sm = Class.forName("android.os.ServiceManager");
                java.lang.reflect.Method get = sm.getDeclaredMethod("getIServiceManager");
                get.setAccessible(true);
                Object manager = get.invoke(null);
                if (manager == null) throw new IllegalStateException("service manager unavailable");
                java.lang.reflect.Method check = Class.forName("android.os.IServiceManager").getMethod("checkService", String.class);
                return (IBinder) check.invoke(manager, "wifi");
            }
        });
    }

    private static <T> T bounded(java.util.concurrent.Callable<T> call) throws Exception {
        if (!PROOF_WORKERS.tryAcquire()) throw new IllegalStateException("early proof workers unavailable");
        java.util.concurrent.FutureTask<T> task = new java.util.concurrent.FutureTask<T>(new java.util.concurrent.Callable<T>() {
            @Override public T call() throws Exception { try { return call.call(); } finally { PROOF_WORKERS.release(); } }
        });
        Thread worker = new Thread(task, "redmagic-early-proof");
        worker.setDaemon(true);
        try { worker.start(); } catch (RuntimeException | Error failure) { PROOF_WORKERS.release(); throw failure; }
        return task.get(2, java.util.concurrent.TimeUnit.SECONDS);
    }

    private static byte[] requestProof(final IBinder service, final byte[] nonce) throws Exception {
        return bounded(new java.util.concurrent.Callable<byte[]>() {
            @Override public byte[] call() throws Exception {
                Parcel data = Parcel.obtain(), reply = Parcel.obtain();
                try {
                    data.writeByteArray(nonce);
                    if (!service.transact(SESSION_PROOF, data, reply, 0))
                        throw new IllegalStateException("proof transaction unsupported");
                    reply.readException();
                    byte[] result = reply.createByteArray();
                    if (result == null || result.length != 32) throw new IllegalStateException("invalid proof response");
                    return result;
                } finally { data.recycle(); reply.recycle(); }
            }
        });
    }

    /** Fresh proof ties the service and caller session; registry ownership is not atomic. */
    private static IBinder verifyEarlySession(IBinder session) throws Exception {
        if (session == null || !session.isBinderAlive()) throw new IllegalStateException("session unavailable");
        IBinder current = currentWifiDirect();
        if (current == null || !current.isBinderAlive()) throw new IllegalStateException("Wi-Fi service unavailable");
        byte[] nonce = new byte[32];
        new java.security.SecureRandom().nextBytes(nonce);
        byte[] expected = requestProof(session, nonce), observed = requestProof(current, nonce);
        if (!java.security.MessageDigest.isEqual(expected, observed) || !session.isBinderAlive() || !current.isBinderAlive())
            throw new IllegalStateException("Wi-Fi session proof unconfirmed");
        return current;
    }

    TargetNetControl(Lease lease) {
        this(lease, true);
    }

    private TargetNetControl(Lease lease, boolean armed) {
        if (lease == null || lease.network() == null) throw new IllegalArgumentException("registered network required");
        this.lease = lease;
        this.armed = armed;
    }

    static void publish(Lease lease) throws Exception {
        Class<?> manager = Class.forName("android.os.ServiceManager");
        java.lang.reflect.Method check = manager.getMethod("checkService", String.class);
        if (check.invoke(null, SERVICE_NAME) != null) throw new IllegalStateException("network control name already in use");
        TargetNetControl control = new TargetNetControl(lease, false);
        manager.getMethod("addService", String.class, IBinder.class).invoke(null, SERVICE_NAME, control);
        IBinder published = (IBinder) check.invoke(null, SERVICE_NAME);
        if (published == null) throw new IllegalStateException("network control was not published");
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(DESCRIPTOR);
            if (!published.transact(STATUS, data, reply, 0)) throw new IllegalStateException("network control probe failed");
            reply.readException();
            int reported = reply.readInt();
            Network registered = reply.readParcelable(Network.class.getClassLoader());
            if (reported != UNAVAILABLE || !lease.network().equals(registered))
                throw new IllegalStateException("network control probe mismatch");
            synchronized (control) { control.armed = true; }
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    @Override protected synchronized boolean onTransact(int code, Parcel data, Parcel reply, int flags)
            throws RemoteException {
        if (code == INTERFACE_TRANSACTION) {
            if (reply == null) return false;
            reply.writeString(DESCRIPTOR);
            return true;
        }
        if (code != STATUS && code != STOP && code != RESTORE && code != STOP_EARLY) return super.onTransact(code, data, reply, flags);
        if (Binder.getCallingUid() != 0) throw new SecurityException("network control requires guest root");
        if (data == null || reply == null) return false;
        data.enforceInterface(DESCRIPTOR);
        final IBinder session = code == STATUS ? null : data.readStrongBinder();
        final IBinder original = code == STOP ? data.readStrongBinder() : null;
        final IBinder replacement = code == STOP ? data.readStrongBinder() : null;
        data.enforceNoDataAvail();
        if (code != STATUS && !armed) throw new IllegalStateException("network control is not ready");
        if (code != STATUS && state == UNKNOWN) throw new IllegalStateException("Ethernet network outcome is unknown");
        if (code != STATUS && session == null) throw new IllegalArgumentException("handoff session required");
        if (code != STATUS && state == RESTORING) throw new IllegalStateException("Ethernet recovery in progress");
        if (code != STATUS && owner != null && !owner.equals(session)) throw new SecurityException("handoff owned by another session");
        if (code == STOP && state == ACTIVE) {
            try {
                if (original == null || replacement == null || original.equals(replacement)
                        || !original.isBinderAlive() || !replacement.isBinderAlive()
                        || !original.equals(WIFI_REGISTRY.current()))
                    throw new IllegalStateException("Wi-Fi handoff services unavailable");
            } catch (Exception failure) {
                throw new IllegalStateException("Wi-Fi handoff services unconfirmed", failure);
            }
            owner = session;
            originalWifi = original;
            replacementWifi = replacement;
            ownerDeath = new IBinder.DeathRecipient() {
                @Override public void binderDied() { recoverAfterDeath(session); }
            };
            try {
                state = STOP_REQUESTED;
                session.linkToDeath(ownerDeath, 0);
                lease.stop();
            } catch (Exception | Error failure) {
                state = UNKNOWN;
                throw new IllegalStateException("Ethernet unregister failed", failure);
            }
        }
        if (code == STOP_EARLY) {
            if (state == STOP_REQUESTED && !earlyOwner) throw new IllegalStateException("different handoff mode active");
            if (state == ACTIVE) {
                IBinder verified;
                try { verified = verifyEarlySession(session); }
                catch (Exception failure) { throw new IllegalStateException("early Wi-Fi session unconfirmed", failure); }
                owner = session; originalWifi = null; replacementWifi = verified; earlyOwner = true;
                ownerDeath = new IBinder.DeathRecipient() {
                    @Override public void binderDied() { recoverAfterDeath(session); }
                };
                try {
                    state = STOP_REQUESTED;
                    session.linkToDeath(ownerDeath, 0);
                    if (!session.isBinderAlive()) {
                        state = ACTIVE;
                        clearOwner();
                        throw new IllegalStateException("early session died before Ethernet stop");
                    }
                } catch (Exception failure) {
                    if (state != ACTIVE) { state = ACTIVE; clearOwner(); }
                    throw new IllegalStateException("early session death watch unconfirmed", failure);
                }
                // Registry validation and stop are not atomic. A later ownership change is possible;
                // session death triggers recovery, and callers independently confirm network state.
                System.out.println("aemu-net: early session verified; ownership can change before Ethernet stop");
                try { lease.stop(); }
                catch (Exception | Error failure) {
                    state = UNKNOWN;
                    // The unregister may have been queued before the exception. Watch even while
                    // the session is still alive; never assume today's visibility is final.
                    scheduleUncertainEarlyRecovery(session);
                    throw new IllegalStateException("Ethernet unregister outcome unknown", failure);
                }
            }
        }
        if (code == RESTORE && state == STOP_REQUESTED) {
            try {
                restoreWhenAvailable(lease, 15000);
                waitForRegistration(lease, 15000);
                if (!earlyOwner && (originalWifi == null || !originalWifi.isBinderAlive()
                        || !originalWifi.equals(WIFI_REGISTRY.current()))) {
                    state = UNKNOWN;
                    System.out.println("aemu-net: Ethernet restored; Wi-Fi Binder recovery unconfirmed");
                } else {
                    state = ACTIVE;
                    clearOwner();
                }
            } catch (Exception | Error failure) {
                state = UNKNOWN;
                System.out.println("aemu-net: Ethernet restore failed: " + failure.getClass().getName());
                failure.printStackTrace(System.out);
                System.out.flush();
                throw new IllegalStateException("Ethernet restore failed", failure);
            }
        }
        reply.writeNoException();
        reply.writeInt(armed ? state : UNAVAILABLE);
        if (code == STATUS) reply.writeParcelable(lease.network(), 0);
        return true;
    }

    private synchronized void clearOwner() {
        if (owner != null && ownerDeath != null) owner.unlinkToDeath(ownerDeath, 0);
        owner = null;
        ownerDeath = null;
        originalWifi = null;
        replacementWifi = null;
        earlyOwner = false;
        uncertainRestoreAttempted = false;
        uncertainRecoveryRunning = false;
        uncertainRecoveryGeneration++;
    }

    private void recoverAfterDeath(final IBinder dead) {
        synchronized (this) {
            if (earlyOwner && state == UNKNOWN && owner != null && owner.equals(dead)) {
                scheduleUncertainEarlyRecovery(dead);
                return;
            }
        }
        Thread recovery = new Thread(new Runnable() {
            @Override public void run() {
                recoverOwner(dead, 30000);
            }
        }, "redmagic-network-recovery");
        recovery.setDaemon(true);
        recovery.start();
    }

    /** STOP threw: unregister may complete later. Observe without touching the live Ethernet agent. */
    private synchronized void scheduleUncertainEarlyRecovery(final IBinder session) {
        if (!earlyOwner || state != UNKNOWN || owner == null || !owner.equals(session) || uncertainRecoveryRunning || uncertainRestoreAttempted) return;
        uncertainRecoveryRunning = true;
        final long generation = ++uncertainRecoveryGeneration;
        Thread worker = new Thread(new Runnable() {
            @Override public void run() {
                try { recoverUncertainEarly(session, 30000, generation); }
                finally { synchronized (TargetNetControl.this) {
                    if (uncertainRecoveryGeneration == generation) uncertainRecoveryRunning = false;
                } }
            }
        }, "redmagic-uncertain-ethernet-recovery");
        worker.setDaemon(true);
        try { worker.start(); }
        catch (RuntimeException | Error failure) {
            uncertainRecoveryRunning = false;
            System.out.println("aemu-net: uncertain Ethernet recovery worker unavailable");
        }
    }

    private void recoverUncertainEarly(IBinder session, long timeoutMs, long generation) {
        if (timeoutMs < 0) throw new IllegalArgumentException("negative timeout");
        final long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.MILLISECONDS.toNanos(timeoutMs);
        for (;;) {
            synchronized (this) {
                if (generation != uncertainRecoveryGeneration || !earlyOwner || state != UNKNOWN || owner == null || !owner.equals(session) || uncertainRestoreAttempted) return;
            }
            try {
                boolean oldPresent = bounded(new java.util.concurrent.Callable<Boolean>() {
                    @Override public Boolean call() throws Exception { return lease.networkRegistered(); }
                });
                // A visible old network may still be asynchronously unregistered later.
                if (!oldPresent) {
                    boolean available = bounded(new java.util.concurrent.Callable<Boolean>() {
                        @Override public Boolean call() throws Exception { return lease.canRestore(); }
                    });
                    if (available) {
                        synchronized (this) {
                            if (generation != uncertainRecoveryGeneration || !earlyOwner || state != UNKNOWN || owner == null || !owner.equals(session)) return;
                            state = RESTORING;
                        }
                        boolean attempted = false;
                        try {
                            // Re-check both observations immediately before the one allowed restore attempt.
                            boolean stillPresent = bounded(new java.util.concurrent.Callable<Boolean>() {
                                @Override public Boolean call() throws Exception { return lease.networkRegistered(); }
                            });
                            boolean stillAvailable = bounded(new java.util.concurrent.Callable<Boolean>() {
                                @Override public Boolean call() throws Exception { return lease.canRestore(); }
                            });
                            if (!stillPresent && stillAvailable) {
                                synchronized (this) { uncertainRestoreAttempted = true; }
                                // Connectivity observations and registration are not an atomic operation.
                                System.out.println("aemu-net: previous Ethernet absent and eth0 free; restore check is not atomic");
                                attempted = true;
                                bounded(new java.util.concurrent.Callable<Boolean>() {
                                    @Override public Boolean call() throws Exception { lease.restore(); return true; }
                                });
                                long confirmationEnd = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(15);
                                boolean registered = false;
                                do {
                                    registered = bounded(new java.util.concurrent.Callable<Boolean>() {
                                        @Override public Boolean call() throws Exception { return lease.networkRegistered(); }
                                    });
                                    if (registered) break;
                                    Thread.sleep(100);
                                } while (System.nanoTime() < confirmationEnd);
                                synchronized (this) {
                                    if (registered) { state = ACTIVE; clearOwner(); }
                                    else state = UNKNOWN;
                                }
                                System.out.println(registered ? "aemu-net: Ethernet recovered after uncertain unregister"
                                        : "aemu-net: Ethernet recovery registration unknown");
                                return;
                            }
                            synchronized (this) { state = UNKNOWN; }
                        } catch (Exception failure) {
                            synchronized (this) { state = UNKNOWN; }
                            if (attempted) {
                                // A timed-out or failed restore can still complete later; never repeat it.
                                System.out.println("aemu-net: Ethernet restore outcome unknown; not retrying");
                                return;
                            }
                        }
                    }
                }
            } catch (Exception failure) {
                // A failed/blocked query is not evidence that the old network is gone.
            }
            if (System.nanoTime() >= deadline) {
                System.out.println("aemu-net: uncertain Ethernet unregister observation expired");
                return;
            }
            try { Thread.sleep(100); }
            catch (InterruptedException interrupted) { Thread.currentThread().interrupt(); return; }
        }
    }

    void recoverOwner(IBinder dead, long timeoutMs) {
        if (timeoutMs < 0) throw new IllegalArgumentException("negative timeout");
        boolean wifiRestored = false;
        synchronized (this) {
            if (owner == null || !owner.equals(dead) || state != STOP_REQUESTED) return;
            state = RESTORING;
            if (earlyOwner) {
                wifiRestored = true; // early mode only requires confirmed Ethernet recovery
            } else try {
                wifiRestored = restoreWifiBinder(WIFI_REGISTRY, originalWifi, replacementWifi);
                if (!wifiRestored) System.out.println("aemu-net: original Wi-Fi Binder restoration unconfirmed");
            } catch (Exception failure) {
                System.out.println("aemu-net: original Wi-Fi Binder restoration failed: " + failure);
                failure.printStackTrace(System.out);
            }
        }
        long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.MILLISECONDS.toNanos(timeoutMs);
        do {
            boolean available;
            try { available = lease.canRestore(); }
            catch (Exception failure) {
                synchronized (this) { state = UNKNOWN; }
                System.out.println("aemu-net: could not check Ethernet interface: " + failure);
                failure.printStackTrace(System.out);
                return;
            }
            if (available) {
                synchronized (this) {
                    try {
                        restoreWhenAvailable(lease, 15000);
                        waitForRegistration(lease, 15000);
                        wifiRestored = wifiRestored && (earlyOwner || (originalWifi != null && originalWifi.isBinderAlive()
                                && originalWifi.equals(WIFI_REGISTRY.current())));
                        state = wifiRestored ? ACTIVE : UNKNOWN;
                        boolean wasEarly = earlyOwner;
                        if (wifiRestored) clearOwner();
                        System.out.println(wifiRestored
                                ? (wasEarly ? "aemu-net: Ethernet restored after early Wi-Fi session death"
                                            : "aemu-net: Wi-Fi Binder and Ethernet restored after Wi-Fi process death")
                                : "aemu-net: Ethernet restored; Wi-Fi Binder recovery unconfirmed");
                    } catch (Exception | Error failure) {
                        state = UNKNOWN;
                        System.out.println("aemu-net: Ethernet recovery unconfirmed: " + failure);
                        failure.printStackTrace(System.out);
                    }
                }
                return;
            }
            if (System.nanoTime() >= deadline) break;
            try { Thread.sleep(100); }
            catch (InterruptedException interrupted) {
                synchronized (this) { state = UNKNOWN; }
                Thread.currentThread().interrupt();
                System.out.println("aemu-net: Ethernet recovery interrupted");
                return;
            }
        } while (true);
        synchronized (this) { state = UNKNOWN; }
        System.out.println("aemu-net: interface still occupied; Ethernet recovery unconfirmed");
    }
}
