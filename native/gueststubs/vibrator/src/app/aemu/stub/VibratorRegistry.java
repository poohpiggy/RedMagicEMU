package app.aemu.stub;

import android.os.IBinder;
import java.lang.reflect.Method;

/** Unwired experiment. The caller must retire the process after a registration attempt. */
final class VibratorRegistry {
    private static final String NAME = "android.hardware.vibrator.IVibratorManager/default";
    enum Observation { OBSERVED_UNVERIFIED }
    interface Backend {
        boolean isDeclared(String name) throws Exception;
        boolean isOccupied(String name) throws Exception;
        void add(String name, IBinder binder) throws Exception;
        boolean isObservable(String name, IBinder binder) throws Exception;
    }

    private final Backend backend;
    private final IBinder binder;
    private boolean started, exitRequired;
    private Observation observation;

    VibratorRegistry(Backend backend, IBinder binder) {
        if (backend == null || binder == null) throw new IllegalArgumentException("backend and binder required");
        this.backend = backend;
        this.binder = binder;
    }

    /** Observation only: this does not establish a functioning HAL or trusted caller identity. */
    synchronized Observation start() throws Exception {
        if (started) throw new IllegalStateException("registry already started");
        started = true;
        // A timed-out publication may complete later; retirement must be process-wide.
        exitRequired = true;
        if (!backend.isDeclared(NAME)) throw new IllegalStateException("vibrator service not declared");
        if (backend.isOccupied(NAME)) throw new IllegalStateException("vibrator service name occupied");
        backend.add(NAME, binder);
        if (!backend.isObservable(NAME, binder)) throw new IllegalStateException("vibrator service not observed");
        // Descriptor equality cannot prove that this process owns the observed binder.
        observation = Observation.OBSERVED_UNVERIFIED;
        return observation;
    }

    synchronized boolean mustExitProcess() { return exitRequired; }
    synchronized Observation observation() { return observation; }

    /** No automatic startup: this entry is not suitable for the currently disabled HAL. */
    static VibratorRegistry registerWithServiceManager(IBinder binder) throws Exception {
        VibratorRegistry registry = new VibratorRegistry(new ServiceManagerBackend(), binder);
        registry.start();
        return registry;
    }

    private static final class ServiceManagerBackend implements Backend {
        private final Method declared, check, add;
        ServiceManagerBackend() throws Exception {
            Class<?> manager = Class.forName("android.os.ServiceManager");
            declared = manager.getMethod("isDeclared", String.class);
            check = manager.getMethod("checkService", String.class);
            add = manager.getMethod("addService", String.class, IBinder.class);
        }
        @Override public boolean isDeclared(String name) throws Exception { return (Boolean) declared.invoke(null, name); }
        @Override public boolean isOccupied(String name) throws Exception { return check.invoke(null, name) != null; }
        @Override public void add(String name, IBinder binder) throws Exception { add.invoke(null, name, binder); }
        @Override public boolean isObservable(String name, IBinder expected) throws Exception {
            IBinder actual = (IBinder) check.invoke(null, name);
            return actual != null && actual.isBinderAlive() &&
                    expected.getInterfaceDescriptor().equals(actual.getInterfaceDescriptor());
        }
    }
}
