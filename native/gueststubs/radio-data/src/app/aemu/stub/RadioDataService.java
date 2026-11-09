package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.RadioResponseType;
import android.hardware.radio.data.*;
import android.hardware.radio.modem.RadioState;
import android.os.IBinder;
import android.os.RemoteException;
import java.util.NoSuchElementException;

/** Unregistered single-slot prototype. It never creates or uses a host cellular bearer. */
public final class RadioDataService extends IRadioData.Stub {
  private volatile RadioRequestCounter requestCounter;
  void setRequestCounterForExperiment(RadioRequestCounter counter) { requestCounter = counter; }
  @Override public boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags) throws RemoteException {
    RadioRequestCounter counter = requestCounter;
    if (counter != null) counter.record(code);
    return super.onTransact(code, data, reply, flags);
  }
    private final Object lock = new Object();
    private final SyntheticSingleSlot model;
    private Callbacks callbacks;

    public RadioDataService(String slot) { this(slot, null); }
    RadioDataService(String slot, SyntheticSingleSlot state) {
        if (!"slot1".equals(slot)) throw new IllegalArgumentException("only slot1 is supported");
        model = state;
    }
    private final class Callbacks implements IBinder.DeathRecipient {
        final IRadioDataResponse response;
        final IBinder responseBinder, indicationBinder;
        boolean responseLinked, indicationLinked, dead;
        Callbacks(IRadioDataResponse r, IRadioDataIndication i) {
            response=r; responseBinder=r.asBinder(); indicationBinder=i.asBinder();
        }
        @Override public void binderDied() {
            synchronized (lock) { dead=true; if (callbacks==this) { callbacks=null; unlink(this); } }
        }
    }
    private static void unlinkOne(IBinder b, IBinder.DeathRecipient recipient) {
        try { b.unlinkToDeath(recipient,0); } catch (NoSuchElementException expectedAfterDeath) { }
    }
    private static void unlink(Callbacks c) {
        if(c.responseLinked) { c.responseLinked=false; unlinkOne(c.responseBinder,c); }
        if(c.indicationLinked) { c.indicationLinked=false; unlinkOne(c.indicationBinder,c); }
    }
    @Override public void setResponseFunctions(IRadioDataResponse response, IRadioDataIndication indication) throws RemoteException {
        if(response==null || indication==null || response.asBinder()==null || indication.asBinder()==null)
            throw new RemoteException("both callbacks required");
        Callbacks next=new Callbacks(response,indication);
        synchronized(lock) {
            try {
                next.responseBinder.linkToDeath(next,0); next.responseLinked=true;
                next.indicationBinder.linkToDeath(next,0); next.indicationLinked=true;
                if(next.dead) throw new RemoteException("callback already died");
            } catch(RemoteException | RuntimeException error) { unlink(next); throw error; }
            Callbacks old=callbacks; callbacks=next; if(old!=null) unlink(old);
        }
    }
    private IRadioDataResponse response() throws RemoteException {
        synchronized(lock) {
            if(callbacks==null || callbacks.dead) throw new RemoteException("callbacks unavailable");
            return callbacks.response;
        }
    }
    private static RadioResponseInfo result(int serial,int error) {
        RadioResponseInfo info=new RadioResponseInfo(); info.serial=serial; info.error=error;
        info.type=RadioResponseType.SOLICITED; return info;
    }
    private int availability() {
        if(model==null) return RadioError.REQUEST_NOT_SUPPORTED;
        return model.radioState()==RadioState.ON ? RadioError.NONE : RadioError.RADIO_NOT_AVAILABLE;
    }
    private static SetupDataCallResult emptyCall() {
        SetupDataCallResult result=new SetupDataCallResult();
        result.ifname=""; result.addresses=new LinkAddress[0]; result.dnses=new String[0];
        result.gateways=new String[0]; result.pcscf=new String[0]; result.defaultQos=Qos.noinit(false);
        result.qosSessions=new QosSession[0]; result.trafficDescriptors=new TrafficDescriptor[0];
        return result;
    }
    @Override public int getInterfaceVersion() { return IRadioData.VERSION; }
    @Override public String getInterfaceHash() { return IRadioData.HASH; }
    @Override public void responseAcknowledgement() { }
    @Override public void allocatePduSessionId(int serial) throws RemoteException { response().allocatePduSessionIdResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED),0); }
    @Override public void cancelHandover(int serial,int callId) throws RemoteException { response().cancelHandoverResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
    @Override public void deactivateDataCall(int serial,int cid,int reason) throws RemoteException { response().deactivateDataCallResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
    @Override public void getDataCallList(int serial) throws RemoteException { response().getDataCallListResponse(result(serial,availability()),new SetupDataCallResult[0]); }
    @Override public void getSlicingConfig(int serial) throws RemoteException {
        SlicingConfig value=new SlicingConfig(); value.urspRules=new UrspRule[0]; value.sliceInfo=new SliceInfo[0];
        response().getSlicingConfigResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED),value);
    }
    @Override public void releasePduSessionId(int serial,int id) throws RemoteException { response().releasePduSessionIdResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
    @Override public void setDataAllowed(int serial,boolean allow) throws RemoteException { response().setDataAllowedResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
    @Override public void setDataProfile(int serial,DataProfileInfo[] profiles) throws RemoteException { response().setDataProfileResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
    @Override public void setDataThrottling(int serial,byte action,long duration) throws RemoteException { response().setDataThrottlingResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
    @Override public void setInitialAttachApn(int serial,DataProfileInfo profile) throws RemoteException { response().setInitialAttachApnResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
    @Override public void setupDataCall(int serial,int accessNetwork,DataProfileInfo profile,boolean roamingAllowed,int reason,LinkAddress[] addresses,String[] dnses,int pduSessionId,SliceInfo sliceInfo,boolean matchAllRuleAllowed) throws RemoteException {
        response().setupDataCallResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED),emptyCall());
    }
    @Override public void startHandover(int serial,int callId) throws RemoteException { response().startHandoverResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
    @Override public void startKeepalive(int serial,KeepaliveRequest keepalive) throws RemoteException { response().startKeepaliveResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED),new KeepaliveStatus()); }
    @Override public void stopKeepalive(int serial,int sessionHandle) throws RemoteException { response().stopKeepaliveResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
}
