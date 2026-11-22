package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioIndicationType;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.RadioResponseType;
import android.hardware.radio.data.*;
import android.hardware.radio.modem.RadioState;
import android.os.IBinder;
import android.os.RemoteException;
import java.io.IOException;
import java.util.NoSuchElementException;

/** Single-slot data service; LTE uses the existing host-backed guest interface. */
public final class RadioDataService extends IRadioData.Stub {
    private volatile RadioRequestCounter requestCounter;

    void setRequestCounterForExperiment(RadioRequestCounter counter) {
        requestCounter = counter;
    }

    @Override public boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags)
            throws RemoteException {
        RadioRequestCounter counter = requestCounter;
        if (counter != null) counter.record(code);
        return super.onTransact(code, data, reply, flags);
    }

    private final Object lock = new Object();
    private final SyntheticSingleSlot model;
    private final boolean lteData;
    private final RadioNetworkGate networkGate;
    private final Runnable radioStateListener;
    private Callbacks callbacks;
    private SetupDataCallResult activeCall;
    private boolean dataAllowed = true;
    private boolean closed;
    private int nextCid = 1;

    public RadioDataService(String slot) {
        this(slot, null, false);
    }

    RadioDataService(String slot, SyntheticSingleSlot state) {
        this(slot, state, false);
    }

    RadioDataService(String slot, SyntheticSingleSlot state, boolean lteData) {
        if (!"slot1".equals(slot)) throw new IllegalArgumentException("only slot1 is supported");
        model = state;
        this.lteData = lteData;
        networkGate = RadioNetworkGate.defaultGate();
        if (lteData && model != null) {
            radioStateListener = this::onRadioStateChanged;
            model.addRadioStateListener(radioStateListener);
        } else {
            radioStateListener = null;
        }
    }

    private final class Callbacks implements IBinder.DeathRecipient {
        final IRadioDataResponse response;
        final IRadioDataIndication indication;
        final IBinder responseBinder, indicationBinder;
        boolean responseLinked, indicationLinked, dead;

        Callbacks(IRadioDataResponse response, IRadioDataIndication indication) {
            this.response = response;
            this.indication = indication;
            responseBinder = response.asBinder();
            indicationBinder = indication.asBinder();
        }

        @Override public void binderDied() {
            boolean hadCall;
            synchronized (lock) {
                dead = true;
                if (callbacks == this) {
                    callbacks = null;
                    unlink(this);
                }
                hadCall = activeCall != null;
                activeCall = null;
            }
            if (hadCall) closeNetworkGate();
        }
    }

    private static void unlinkOne(IBinder binder, IBinder.DeathRecipient recipient) {
        try {
            binder.unlinkToDeath(recipient, 0);
        } catch (NoSuchElementException expectedAfterDeath) {
        }
    }

    private static void unlink(Callbacks callback) {
        if (callback.responseLinked) {
            callback.responseLinked = false;
            unlinkOne(callback.responseBinder, callback);
        }
        if (callback.indicationLinked) {
            callback.indicationLinked = false;
            unlinkOne(callback.indicationBinder, callback);
        }
    }

    @Override public void setResponseFunctions(IRadioDataResponse response, IRadioDataIndication indication)
            throws RemoteException {
        if (response == null || indication == null || response.asBinder() == null || indication.asBinder() == null)
            throw new RemoteException("both callbacks required");
        Callbacks next = new Callbacks(response, indication);
        synchronized (lock) {
            try {
                next.responseBinder.linkToDeath(next, 0);
                next.responseLinked = true;
                next.indicationBinder.linkToDeath(next, 0);
                next.indicationLinked = true;
                if (next.dead) throw new RemoteException("callback already died");
            } catch (RemoteException | RuntimeException error) {
                unlink(next);
                throw error;
            }
            Callbacks old = callbacks;
            callbacks = next;
            if (old != null) unlink(old);
        }
    }

    private IRadioDataResponse response() throws RemoteException {
        synchronized (lock) {
            if (callbacks == null || callbacks.dead) throw new RemoteException("callbacks unavailable");
            return callbacks.response;
        }
    }

    private static RadioResponseInfo result(int serial, int error) {
        RadioResponseInfo info = new RadioResponseInfo();
        info.serial = serial;
        info.error = error;
        info.type = RadioResponseType.SOLICITED;
        return info;
    }

    private int availability() {
        if (model == null) return RadioError.REQUEST_NOT_SUPPORTED;
        return model.radioState() == RadioState.ON ? RadioError.NONE : RadioError.RADIO_NOT_AVAILABLE;
    }

    private static SetupDataCallResult emptyCall(long retryMillis) {
        SetupDataCallResult result = new SetupDataCallResult();
        result.cause = DataCallFailCause.ERROR_UNSPECIFIED;
        result.suggestedRetryTime = retryMillis;
        result.ifname = "";
        result.addresses = new LinkAddress[0];
        result.dnses = new String[0];
        result.gateways = new String[0];
        result.pcscf = new String[0];
        result.defaultQos = Qos.noinit(false);
        result.qosSessions = new QosSession[0];
        result.trafficDescriptors = new TrafficDescriptor[0];
        return result;
    }

    private static SetupDataCallResult createCall(int cid, int pduSessionId) {
        SetupDataCallResult result = new SetupDataCallResult();
        result.cause = DataCallFailCause.NONE;
        result.suggestedRetryTime = -1;
        result.cid = cid;
        result.active = SetupDataCallResult.DATA_CONNECTION_STATUS_ACTIVE;
        result.type = PdpProtocolType.IP;
        result.ifname = "eth0";
        LinkAddress address = new LinkAddress();
        address.address = "10.0.2.15/24";
        address.addressProperties = LinkAddress.ADDRESS_PROPERTY_NONE;
        result.addresses = new LinkAddress[]{address};
        result.dnses = new String[]{"8.8.8.8", "1.1.1.1"};
        result.gateways = new String[]{"10.0.2.2"};
        result.pcscf = new String[0];
        result.mtuV4 = 1500;
        result.mtuV6 = 0;
        result.defaultQos = Qos.noinit(false);
        result.qosSessions = new QosSession[0];
        result.handoverFailureMode = SetupDataCallResult.HANDOVER_FAILURE_MODE_LEGACY;
        result.pduSessionId = pduSessionId;
        result.trafficDescriptors = new TrafficDescriptor[0];
        return result;
    }

    private boolean closeActiveCall() {
        boolean changed;
        synchronized (lock) {
            changed = activeCall != null;
            activeCall = null;
        }
        if (changed) closeNetworkGate();
        return changed;
    }

    private void closeNetworkGate() {
        if (!lteData) return;
        try {
            networkGate.setConnected(false);
        } catch (IOException failure) {
            System.err.println("RADIO_DATA_GATE_CLOSE_FAILED " + failure.getClass().getSimpleName());
        }
    }

    private void notifyDataCallList() {
        final IRadioDataIndication callback;
        final SetupDataCallResult[] calls;
        synchronized (lock) {
            if (closed || callbacks == null || callbacks.dead) return;
            callback = callbacks.indication;
            calls = activeCall == null ? new SetupDataCallResult[0] : new SetupDataCallResult[]{activeCall};
        }
        try {
            callback.dataCallListChanged(RadioIndicationType.UNSOLICITED, calls);
        } catch (RemoteException | RuntimeException failure) {
            System.err.println("RADIO_DATA_INDICATION_FAILED " + failure.getClass().getSimpleName());
        }
    }

    private void onRadioStateChanged() {
        if (closeActiveCall()) notifyDataCallList();
    }

    void close() {
        if (radioStateListener != null && model != null) model.removeRadioStateListener(radioStateListener);
        Callbacks old;
        synchronized (lock) {
            closed = true;
            activeCall = null;
            old = callbacks;
            callbacks = null;
        }
        if (old != null) unlink(old);
        closeNetworkGate();
    }

    @Override public int getInterfaceVersion() { return IRadioData.VERSION; }
    @Override public String getInterfaceHash() { return IRadioData.HASH; }
    @Override public void responseAcknowledgement() { }

    @Override public void allocatePduSessionId(int serial) throws RemoteException {
        response().allocatePduSessionIdResponse(
                result(serial, lteData ? RadioError.NONE : RadioError.REQUEST_NOT_SUPPORTED), 1);
    }

    @Override public void cancelHandover(int serial, int callId) throws RemoteException {
        response().cancelHandoverResponse(result(serial, RadioError.REQUEST_NOT_SUPPORTED));
    }

    @Override public void deactivateDataCall(int serial, int cid, int reason) throws RemoteException {
        if (!lteData) {
            response().deactivateDataCallResponse(result(serial, RadioError.REQUEST_NOT_SUPPORTED));
            return;
        }
        boolean removed;
        synchronized (lock) {
            removed = activeCall != null && activeCall.cid == cid;
            if (removed) activeCall = null;
        }
        if (removed) closeNetworkGate();
        response().deactivateDataCallResponse(result(serial, RadioError.NONE));
        if (removed) notifyDataCallList();
    }

    @Override public void getDataCallList(int serial) throws RemoteException {
        int error = availability();
        SetupDataCallResult[] calls;
        synchronized (lock) {
            calls = error == RadioError.NONE && activeCall != null
                    ? new SetupDataCallResult[]{activeCall}
                    : new SetupDataCallResult[0];
        }
        response().getDataCallListResponse(result(serial, error), calls);
    }

    @Override public void getSlicingConfig(int serial) throws RemoteException {
        SlicingConfig value = new SlicingConfig();
        value.urspRules = new UrspRule[0];
        value.sliceInfo = new SliceInfo[0];
        response().getSlicingConfigResponse(result(serial, RadioError.REQUEST_NOT_SUPPORTED), value);
    }

    @Override public void releasePduSessionId(int serial, int id) throws RemoteException {
        response().releasePduSessionIdResponse(
                result(serial, lteData ? RadioError.NONE : RadioError.REQUEST_NOT_SUPPORTED));
    }

    @Override public void setDataAllowed(int serial, boolean allow) throws RemoteException {
        if (!lteData) {
            response().setDataAllowedResponse(result(serial, RadioError.REQUEST_NOT_SUPPORTED));
            return;
        }
        boolean removed;
        synchronized (lock) {
            dataAllowed = allow;
            removed = !allow && activeCall != null;
            if (!allow) activeCall = null;
        }
        if (!allow) closeNetworkGate();
        response().setDataAllowedResponse(result(serial, RadioError.NONE));
        if (removed) notifyDataCallList();
    }

    @Override public void setDataProfile(int serial, DataProfileInfo[] profiles) throws RemoteException {
        response().setDataProfileResponse(
                result(serial, lteData ? RadioError.NONE : RadioError.REQUEST_NOT_SUPPORTED));
    }

    @Override public void setDataThrottling(int serial, byte action, long duration) throws RemoteException {
        response().setDataThrottlingResponse(
                result(serial, lteData ? RadioError.NONE : RadioError.REQUEST_NOT_SUPPORTED));
    }

    @Override public void setInitialAttachApn(int serial, DataProfileInfo profile) throws RemoteException {
        response().setInitialAttachApnResponse(
                result(serial, lteData ? RadioError.NONE : RadioError.REQUEST_NOT_SUPPORTED));
    }

    @Override public void setupDataCall(int serial, int accessNetwork, DataProfileInfo profile, boolean roamingAllowed,
            int reason, LinkAddress[] addresses, String[] dnses, int pduSessionId, SliceInfo sliceInfo,
            boolean matchAllRuleAllowed) throws RemoteException {
        if (!lteData) {
            // This boot mode cannot create a cellular bearer. Suppress modem-directed retries.
            response().setupDataCallResponse(result(serial, RadioError.REQUEST_NOT_SUPPORTED), emptyCall(Long.MAX_VALUE));
            return;
        }
        int error = availability();
        SetupDataCallResult call = null;
        synchronized (lock) {
            if (error == RadioError.NONE && !dataAllowed) error = RadioError.INVALID_STATE;
            if (error == RadioError.NONE && activeCall != null) call = activeCall;
            if (error == RadioError.NONE && activeCall == null) {
                int cid = nextCid++;
                if (nextCid <= 0) nextCid = 1;
                call = createCall(cid, pduSessionId);
                try {
                    networkGate.setConnected(true);
                    activeCall = call;
                } catch (IOException failure) {
                    error = RadioError.INTERNAL_ERR;
                    call = null;
                }
            }
        }
        if (error != RadioError.NONE || call == null) {
            // LTE can recover after data or radio is re-enabled. Let Android choose the backoff.
            response().setupDataCallResponse(result(serial, error), emptyCall(-1));
            return;
        }
        try {
            response().setupDataCallResponse(result(serial, RadioError.NONE), call);
        } catch (RemoteException | RuntimeException failure) {
            closeActiveCall();
            throw failure;
        }
    }

    @Override public void startHandover(int serial, int callId) throws RemoteException {
        response().startHandoverResponse(result(serial, RadioError.REQUEST_NOT_SUPPORTED));
    }

    @Override public void startKeepalive(int serial, KeepaliveRequest keepalive) throws RemoteException {
        response().startKeepaliveResponse(
                result(serial, RadioError.REQUEST_NOT_SUPPORTED), new KeepaliveStatus());
    }

    @Override public void stopKeepalive(int serial, int sessionHandle) throws RemoteException {
        response().stopKeepaliveResponse(result(serial, RadioError.REQUEST_NOT_SUPPORTED));
    }
}
