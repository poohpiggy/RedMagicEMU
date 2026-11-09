package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.RadioResponseType;
import android.hardware.radio.config.IRadioConfig;
import android.hardware.radio.config.IRadioConfigIndication;
import android.hardware.radio.config.IRadioConfigResponse;
import android.hardware.radio.config.PhoneCapability;
import android.hardware.radio.config.SimSlotStatus;
import android.hardware.radio.config.SimTypeInfo;
import android.hardware.radio.config.SlotPortMapping;
import android.os.IBinder;
import android.os.RemoteException;
import java.util.NoSuchElementException;

/** Unregistered single-slot service; a simulated card exists only with an explicit test model. */
public final class RadioConfigService extends IRadioConfig.Stub {
    private volatile RadioRequestCounter requestCounter;
    void setRequestCounterForExperiment(RadioRequestCounter counter) { requestCounter = counter; }
    @Override public boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags) throws RemoteException {
        RadioRequestCounter counter = requestCounter;
        if (counter != null) counter.record(code);
        return super.onTransact(code, data, reply, flags);
    }
    private final Object lock = new Object();
    private final SyntheticSingleSlot syntheticSlot;
    private Callbacks callbacks;

    public RadioConfigService(String slot) {
        this(slot, null);
    }

    RadioConfigService(String slot, SyntheticSingleSlot syntheticSlot) {
        if (!"slot1".equals(slot)) throw new IllegalArgumentException("only slot1 is supported");
        this.syntheticSlot = syntheticSlot;
    }

    private final class Callbacks implements IBinder.DeathRecipient {
        final IRadioConfigResponse response;
        final IBinder responseBinder, indicationBinder;
        boolean responseLinked, indicationLinked, dead;
        Callbacks(IRadioConfigResponse r, IRadioConfigIndication i) {
            response = r; responseBinder = r.asBinder(); indicationBinder = i.asBinder();
        }
        @Override public void binderDied() {
            synchronized (lock) {
                dead = true;
                if (callbacks == this) { callbacks = null; unlink(this); }
            }
        }
    }
    private static void unlinkOne(IBinder binder, IBinder.DeathRecipient recipient) {
        try { binder.unlinkToDeath(recipient, 0); }
        catch (NoSuchElementException expectedAfterDeath) { /* already detached */ }
    }
    private static void unlink(Callbacks c) {
        if (c.responseLinked) { c.responseLinked = false; unlinkOne(c.responseBinder, c); }
        if (c.indicationLinked) { c.indicationLinked = false; unlinkOne(c.indicationBinder, c); }
    }
    @Override public void setResponseFunctions(IRadioConfigResponse response,
            IRadioConfigIndication indication) throws RemoteException {
        if (response == null || indication == null || response.asBinder() == null || indication.asBinder() == null)
            throw new RemoteException("both radio config callbacks are required");
        Callbacks next = new Callbacks(response, indication);
        synchronized (lock) {
            try {
                next.responseBinder.linkToDeath(next, 0); next.responseLinked = true;
                next.indicationBinder.linkToDeath(next, 0); next.indicationLinked = true;
                if (next.dead) throw new RemoteException("radio config callback already died");
            } catch (RemoteException | RuntimeException error) {
                unlink(next); throw error;
            }
            Callbacks old = callbacks;
            callbacks = next;
            if (old != null) unlink(old);
        }
    }
    private IRadioConfigResponse response() throws RemoteException {
        synchronized (lock) {
            if (callbacks == null || callbacks.dead) throw new RemoteException("radio config is not connected");
            return callbacks.response;
        }
    }
    private static RadioResponseInfo unsupported(int serial) {
        return result(serial, RadioError.REQUEST_NOT_SUPPORTED);
    }
    private static RadioResponseInfo result(int serial, int error) {
        RadioResponseInfo info = new RadioResponseInfo();
        info.type = RadioResponseType.SOLICITED;
        info.serial = serial;
        info.error = error;
        return info;
    }
    private int modelError() {
        if (syntheticSlot == null) return RadioError.REQUEST_NOT_SUPPORTED;
        return syntheticSlot.isConnected() ? RadioError.NONE : RadioError.RADIO_NOT_AVAILABLE;
    }
    @Override public int getInterfaceVersion() { return IRadioConfig.VERSION; }
    @Override public String getInterfaceHash() { return IRadioConfig.HASH; }
    @Override public void getHalDeviceCapabilities(int s) throws RemoteException { response().getHalDeviceCapabilitiesResponse(unsupported(s), false); }
    @Override public void getNumOfLiveModems(int s) throws RemoteException {
        int error = modelError();
        response().getNumOfLiveModemsResponse(result(s, error),
                error == RadioError.NONE ? syntheticSlot.liveModems() : (byte)0);
    }
    @Override public void getPhoneCapability(int s) throws RemoteException {
        int error = modelError();
        response().getPhoneCapabilityResponse(result(s, error),
                error == RadioError.NONE ? syntheticSlot.phoneCapability() : new PhoneCapability());
    }
    @Override public void getSimSlotsStatus(int s) throws RemoteException {
        int error = modelError();
        response().getSimSlotsStatusResponse(result(s, error),
                error == RadioError.NONE ? syntheticSlot.slotStatus() : new SimSlotStatus[0]);
    }
    @Override public void setNumOfLiveModems(int s, byte n) throws RemoteException { response().setNumOfLiveModemsResponse(unsupported(s)); }
    @Override public void setPreferredDataModem(int s, byte id) throws RemoteException { response().setPreferredDataModemResponse(unsupported(s)); }
    @Override public void setSimSlotsMapping(int s, SlotPortMapping[] map) throws RemoteException { response().setSimSlotsMappingResponse(unsupported(s)); }
    @Override public void getSimultaneousCallingSupport(int s) throws RemoteException { response().getSimultaneousCallingSupportResponse(unsupported(s), new int[0]); }
    @Override public void getSimTypeInfo(int s) throws RemoteException { response().getSimTypeInfoResponse(unsupported(s), new SimTypeInfo[0]); }
    @Override public void setSimType(int s, int[] types) throws RemoteException { response().setSimTypeResponse(unsupported(s)); }
}
