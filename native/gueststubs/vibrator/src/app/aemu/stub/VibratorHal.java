package app.aemu.stub;

import android.hardware.vibrator.CompositeEffect;
import android.hardware.vibrator.IVibrator;
import android.hardware.vibrator.IVibratorCallback;
import android.hardware.vibrator.IVibratorManager;
import android.hardware.vibrator.PrimitivePwle;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.ServiceSpecificException;
import java.io.IOException;

/** Unregistered Android 16 AIDL v2 bridge. Not wired into Guest startup. */
public final class VibratorHal {
    private VibratorHal() {}

    private static UnsupportedOperationException unsupported() {
        return new UnsupportedOperationException("unsupported by virtual vibrator");
    }
    private static ServiceSpecificException bridgeFailure(IOException failure) {
        return new ServiceSpecificException(1, "vibration bridge unavailable");
    }
    private static void checkCaller() { VibrationController.requireAuthorizedCaller(); }

    public static final class Manager extends IVibratorManager.Stub {
        public Manager(VibrationController controller) { java.util.Objects.requireNonNull(controller); }
        @Override public boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            checkCaller();
            return super.onTransact(code, data, reply, flags);
        }
        @Override public int getInterfaceVersion() { return IVibratorManager.VERSION; }
        @Override public String getInterfaceHash() { return IVibratorManager.HASH; }
        @Override public int getCapabilities() { return 0; }
        @Override public int[] getVibratorIds() { return new int[0]; }
        @Override public IVibrator getVibrator(int id) {
            throw new IllegalArgumentException("unknown vibrator");
        }
        @Override public void prepareSynced(int[] ids) { throw unsupported(); }
        @Override public void triggerSynced(IVibratorCallback callback) { throw unsupported(); }
        @Override public void cancelSynced() { throw unsupported(); }
    }

    public static final class Device extends IVibrator.Stub {
        private final VibrationController controller;
        Device(VibrationController controller) { this.controller = controller; }
        @Override public boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            checkCaller();
            return super.onTransact(code, data, reply, flags);
        }
        @Override public int getInterfaceVersion() { return IVibrator.VERSION; }
        @Override public String getInterfaceHash() { return IVibrator.HASH; }
        @Override public int getCapabilities() { return 0; }
        @Override public void off() {
            try { controller.off(0); }
            catch (IOException failure) { throw bridgeFailure(failure); }
        }
        @Override public void on(int timeoutMs, IVibratorCallback callback) {
            try { controller.on(0, timeoutMs); }
            catch (IOException failure) { throw bridgeFailure(failure); }
        }
        @Override public int perform(int effect, byte strength, IVibratorCallback callback) { throw unsupported(); }
        @Override public int[] getSupportedEffects() { return new int[0]; }
        @Override public void setAmplitude(float amplitude) { throw unsupported(); }
        @Override public void setExternalControl(boolean enabled) { throw unsupported(); }
        @Override public int getCompositionDelayMax() { throw unsupported(); }
        @Override public int getCompositionSizeMax() { throw unsupported(); }
        @Override public int[] getSupportedPrimitives() { return new int[0]; }
        @Override public int getPrimitiveDuration(int primitive) { throw unsupported(); }
        @Override public void compose(CompositeEffect[] composite, IVibratorCallback callback) { throw unsupported(); }
        @Override public int[] getSupportedAlwaysOnEffects() { return new int[0]; }
        @Override public void alwaysOnEnable(int id, int effect, byte strength) { throw unsupported(); }
        @Override public void alwaysOnDisable(int id) { throw unsupported(); }
        @Override public float getResonantFrequency() { throw unsupported(); }
        @Override public float getQFactor() { throw unsupported(); }
        @Override public float getFrequencyResolution() { throw unsupported(); }
        @Override public float getFrequencyMinimum() { throw unsupported(); }
        @Override public float[] getBandwidthAmplitudeMap() { throw unsupported(); }
        @Override public int getPwlePrimitiveDurationMax() { throw unsupported(); }
        @Override public int getPwleCompositionSizeMax() { throw unsupported(); }
        @Override public int[] getSupportedBraking() { return new int[0]; }
        @Override public void composePwle(PrimitivePwle[] composite, IVibratorCallback callback) { throw unsupported(); }
    }
}
