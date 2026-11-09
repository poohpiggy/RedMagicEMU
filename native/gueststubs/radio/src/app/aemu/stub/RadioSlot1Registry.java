package app.aemu.stub;

import android.os.IBinder;

/** Explicit experiment only. The caller must own the Guest lifecycle and terminate this process on failure. */
final class RadioSlot1Registry {
    interface Backend {
        boolean isDeclared(String name) throws Exception;
        boolean isOccupied(String name) throws Exception;
        void add(String name, IBinder binder) throws Exception;
        boolean isPublished(String name, IBinder binder) throws Exception;
    }

    private static final String[] NAMES = {
        "android.hardware.radio.config.IRadioConfig/default",
        "android.hardware.radio.modem.IRadioModem/slot1",
        "android.hardware.radio.sim.IRadioSim/slot1",
        "android.hardware.radio.network.IRadioNetwork/slot1",
        "android.hardware.radio.data.IRadioData/slot1",
        "android.hardware.radio.messaging.IRadioMessaging/slot1",
        "android.hardware.radio.voice.IRadioVoice/slot1"
    };
    private final Backend backend;
    private final IBinder[] binders;
    private boolean started, exitRequired;

    RadioSlot1Registry(Backend backend, IBinder[] binders) {
        if (backend == null || binders == null || binders.length != NAMES.length)
            throw new IllegalArgumentException("seven service binders required");
        this.binders = binders.clone();
        for (int i=0;i<this.binders.length;i++) {
            if (this.binders[i] == null) throw new IllegalArgumentException("null service binder");
            for (int j=0;j<i;j++) if (this.binders[i] == this.binders[j])
                throw new IllegalArgumentException("duplicate service binder");
        }
        this.backend = backend;
    }

    /** Success means the names are observable; it does not mean framework initialization. */
    synchronized void start() throws Exception {
        if (started) throw new IllegalStateException("registry already started");
        started = true;
        // Even a timed-out publication can complete later: only process exit retracts it.
        exitRequired = true;
        for (String name:NAMES) {
            if (!backend.isDeclared(name)) throw new IllegalStateException("service not declared: " + name);
            if (backend.isOccupied(name)) throw new IllegalStateException("service name already occupied: " + name);
        }
        for (int i=0;i<NAMES.length;i++) backend.add(NAMES[i],binders[i]);
        for (int i=0;i<NAMES.length;i++)
            if (!backend.isPublished(NAMES[i],binders[i])) throw new IllegalStateException("service not observed: " + NAMES[i]);
    }

    synchronized boolean mustExitProcess() { return exitRequired; }
}
