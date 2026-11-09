package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioIndicationType;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.RadioResponseType;
import android.hardware.radio.messaging.CdmaBroadcastSmsConfigInfo;
import android.hardware.radio.messaging.CdmaSmsAck;
import android.hardware.radio.messaging.CdmaSmsMessage;
import android.hardware.radio.messaging.CdmaSmsWriteArgs;
import android.hardware.radio.messaging.GsmBroadcastSmsConfigInfo;
import android.hardware.radio.messaging.GsmSmsMessage;
import android.hardware.radio.messaging.ImsSmsMessage;
import android.hardware.radio.messaging.IRadioMessaging;
import android.hardware.radio.messaging.IRadioMessagingIndication;
import android.hardware.radio.messaging.IRadioMessagingResponse;
import android.hardware.radio.messaging.SendSmsResult;
import android.hardware.radio.messaging.SmsWriteArgs;
import android.os.IBinder;
import android.os.RemoteException;

import java.util.NoSuchElementException;

/** Unregistered slot1 test service. It supports one incoming PDU awaiting acknowledgement. */
public final class RadioMessagingService extends IRadioMessaging.Stub {
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

    RadioMessagingService(String slot, SyntheticSingleSlot slotModel) {
        if (!"slot1".equals(slot)) throw new IllegalArgumentException("only slot1 is supported");
        if (slotModel == null) throw new IllegalArgumentException("single-slot state is required");
        syntheticSlot = slotModel;
    }

    /** Legacy unguarded path exclusively for isolated callback and Binder tests. */
    private RadioMessagingService(String slot) {
        if (!"slot1".equals(slot)) throw new IllegalArgumentException("only slot1 is supported");
        syntheticSlot = null;
    }

    static RadioMessagingService withoutStateForTests(String slot) {
        return new RadioMessagingService(slot);
    }

    boolean usesState(SyntheticSingleSlot state) { return state != null && syntheticSlot == state; }

    private final class Callbacks implements IBinder.DeathRecipient {
        final IRadioMessagingResponse response;
        final IRadioMessagingIndication indication;
        final IBinder responseBinder;
        final IBinder indicationBinder;
        boolean responseLinked, indicationLinked, dead, incomingPending;

        Callbacks(IRadioMessagingResponse r, IRadioMessagingIndication i) {
            response = r;
            indication = i;
            responseBinder = r.asBinder();
            indicationBinder = i.asBinder();
        }

        @Override public void binderDied() {
            synchronized (lock) {
                dead = true;
                if (callbacks == this) {
                    callbacks = null;
                    unlink(this);
                }
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

    @Override public void setResponseFunctions(IRadioMessagingResponse response,
            IRadioMessagingIndication indication) throws RemoteException {
        if (response == null || indication == null || response.asBinder() == null || indication.asBinder() == null)
            throw new RemoteException("both radio messaging callbacks are required");
        Callbacks next = new Callbacks(response, indication);
        synchronized (lock) {
            try {
                next.responseBinder.linkToDeath(next, 0); next.responseLinked = true;
                next.indicationBinder.linkToDeath(next, 0); next.indicationLinked = true;
                if (next.dead) throw new RemoteException("radio messaging callback already died");
            } catch (RemoteException | RuntimeException error) {
                unlink(next);
                throw error;
            }
            Callbacks old = callbacks;
            callbacks = next;
            if (old != null) unlink(old);
        }
    }

    private IRadioMessagingResponse response() throws RemoteException {
        synchronized (lock) {
            if (callbacks == null || callbacks.dead) throw new RemoteException("radio messaging is not connected");
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

    /** Test-only injection. A remote exception may mean the indication was already delivered. */
    public void injectIncomingGsmSms(byte[] pdu) throws RemoteException {
        if (!tryInjectIncomingGsmSms(pdu)) throw new IllegalStateException("incoming SMS awaiting acknowledgement");
    }

    /** Returns false only before reserving or invoking a callback for this PDU. */
    boolean tryInjectIncomingGsmSms(byte[] pdu) throws RemoteException {
        if (pdu == null || pdu.length == 0 || pdu.length > 256)
            throw new IllegalArgumentException("invalid GSM PDU length");
        final long token = syntheticSlot == null ? 0 : syntheticSlot.deliveryToken();
        if (syntheticSlot != null && token == 0)
            throw new RemoteException("virtual radio is not ready for SMS");
        final Callbacks target;
        synchronized (lock) {
            target = callbacks;
            if (target == null || target.dead) throw new RemoteException("radio messaging is not connected");
            if (target.incomingPending) return false;
            // Reserve before the oneway callback; a local callback may acknowledge immediately.
            // Do not clear on transport failure: delivery is ambiguous until ack, death or rebind.
            target.incomingPending = true;
        }
        target.indication.newSms(RadioIndicationType.UNSOLICITED, pdu.clone());
        if (syntheticSlot != null && !syntheticSlot.isCurrentDelivery(token))
            throw new RemoteException("virtual radio changed; SMS event may be partially delivered");
        return true;
    }

    @Override public int getInterfaceVersion() { return IRadioMessaging.VERSION; }
    @Override public String getInterfaceHash() { return IRadioMessaging.HASH; }
    @Override public void responseAcknowledgement() { /* no outstanding indications */ }
    @Override public void acknowledgeIncomingGsmSmsWithPdu(int s, boolean ok, String pdu) throws RemoteException { response().acknowledgeIncomingGsmSmsWithPduResponse(unsupported(s)); }
    @Override public void acknowledgeLastIncomingCdmaSms(int s, CdmaSmsAck ack) throws RemoteException { response().acknowledgeLastIncomingCdmaSmsResponse(unsupported(s)); }
    @Override public void acknowledgeLastIncomingGsmSms(int s, boolean ok, int cause) throws RemoteException {
        final IRadioMessagingResponse target;
        final int error;
        synchronized (lock) {
            if (callbacks == null || callbacks.dead) throw new RemoteException("radio messaging is not connected");
            target = callbacks.response;
            error = callbacks.incomingPending ? RadioError.NONE : RadioError.INVALID_STATE;
            callbacks.incomingPending = false;
        }
        target.acknowledgeLastIncomingGsmSmsResponse(result(s, error));
    }
    @Override public void deleteSmsOnRuim(int s, int index) throws RemoteException { response().deleteSmsOnRuimResponse(unsupported(s)); }
    @Override public void deleteSmsOnSim(int s, int index) throws RemoteException { response().deleteSmsOnSimResponse(unsupported(s)); }
    @Override public void getCdmaBroadcastConfig(int s) throws RemoteException { response().getCdmaBroadcastConfigResponse(unsupported(s), new CdmaBroadcastSmsConfigInfo[0]); }
    @Override public void getGsmBroadcastConfig(int s) throws RemoteException { response().getGsmBroadcastConfigResponse(unsupported(s), new GsmBroadcastSmsConfigInfo[0]); }
    @Override public void getSmscAddress(int s) throws RemoteException { response().getSmscAddressResponse(unsupported(s), ""); }
    @Override public void reportSmsMemoryStatus(int s, boolean available) throws RemoteException { response().reportSmsMemoryStatusResponse(unsupported(s)); }
    @Override public void sendCdmaSms(int s, CdmaSmsMessage sms) throws RemoteException { response().sendCdmaSmsResponse(unsupported(s), new SendSmsResult()); }
    @Override public void sendCdmaSmsExpectMore(int s, CdmaSmsMessage sms) throws RemoteException { response().sendCdmaSmsExpectMoreResponse(unsupported(s), new SendSmsResult()); }
    @Override public void sendImsSms(int s, ImsSmsMessage sms) throws RemoteException { response().sendImsSmsResponse(unsupported(s), new SendSmsResult()); }
    @Override public void sendSms(int s, GsmSmsMessage sms) throws RemoteException { response().sendSmsResponse(unsupported(s), new SendSmsResult()); }
    @Override public void sendSmsExpectMore(int s, GsmSmsMessage sms) throws RemoteException { response().sendSmsExpectMoreResponse(unsupported(s), new SendSmsResult()); }
    @Override public void setCdmaBroadcastActivation(int s, boolean active) throws RemoteException { response().setCdmaBroadcastActivationResponse(unsupported(s)); }
    @Override public void setCdmaBroadcastConfig(int s, CdmaBroadcastSmsConfigInfo[] c) throws RemoteException { response().setCdmaBroadcastConfigResponse(unsupported(s)); }
    @Override public void setGsmBroadcastActivation(int s, boolean active) throws RemoteException { response().setGsmBroadcastActivationResponse(unsupported(s)); }
    @Override public void setGsmBroadcastConfig(int s, GsmBroadcastSmsConfigInfo[] c) throws RemoteException { response().setGsmBroadcastConfigResponse(unsupported(s)); }
    @Override public void setSmscAddress(int s, String address) throws RemoteException { response().setSmscAddressResponse(unsupported(s)); }
    @Override public void writeSmsToRuim(int s, CdmaSmsWriteArgs args) throws RemoteException { response().writeSmsToRuimResponse(unsupported(s), 0); }
    @Override public void writeSmsToSim(int s, SmsWriteArgs args) throws RemoteException { response().writeSmsToSimResponse(unsupported(s), 0); }
}
