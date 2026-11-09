package app.aemu.stub;

import java.io.IOException;

/** Local contract check only: a claimed system UID must not authorize output. */
public final class VibratorHalOfflineTest {
    private static final class Sink implements VibrationController.Sink {
        int calls;
        @Override public void play(int durationMs) throws IOException { calls++; }
        @Override public void cancel() throws IOException { calls++; }
    }

    private static void check(boolean value) {
        if (!value) throw new AssertionError();
    }

    private static void expect(Class<? extends Throwable> type, Runnable operation) {
        try { operation.run(); }
        catch (Throwable failure) {
            if (type.isInstance(failure)) return;
            throw failure;
        }
        throw new AssertionError("expected " + type.getName());
    }

    public static void main(String[] args) {
        Sink sink = new Sink();
        VibrationController controller = new VibrationController(sink);
        VibratorHal.Manager manager = new VibratorHal.Manager(controller);
        check(android.os.Binder.getCallingUid() == 1000);
        check(manager.getCapabilities() == 0);
        check(manager.getVibratorIds().length == 0);
        expect(IllegalArgumentException.class, () -> manager.getVibrator(0));
        expect(IllegalArgumentException.class, () -> manager.getVibrator(1));
        VibratorHal.Device device = new VibratorHal.Device(controller);
        check(device.getCapabilities() == 0);
        check(device.getSupportedEffects().length == 0);
        check(device.getSupportedPrimitives().length == 0);
        check(device.getSupportedBraking().length == 0);
        expect(UnsupportedOperationException.class, () -> device.setAmplitude(0.5f));
        expect(UnsupportedOperationException.class, () -> device.compose(null, null));
        expect(UnsupportedOperationException.class, () -> manager.prepareSynced(new int[] {0}));
        expect(SecurityException.class, () -> {
            try { manager.onTransact(0, null, null, 0); }
            catch (android.os.RemoteException e) { throw new AssertionError(e); }
        });
        expect(SecurityException.class, () -> {
            try { manager.onTransact(1, null, null, 0); }
            catch (android.os.RemoteException e) { throw new AssertionError(e); }
        });
        expect(SecurityException.class, () -> {
            try { device.onTransact(0, null, null, 0); }
            catch (android.os.RemoteException e) { throw new AssertionError(e); }
        });
        expect(SecurityException.class, () -> {
            try { device.onTransact(1, null, null, 0); }
            catch (android.os.RemoteException e) { throw new AssertionError(e); }
        });
        expect(SecurityException.class, () -> device.on(500, null));
        expect(SecurityException.class, device::off);
        check(sink.calls == 0);
        System.out.println("VIBRATOR_HAL_OFFLINE_CONTRACT_OK");
    }
}
