package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioIndicationType;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.RadioResponseType;
import android.hardware.radio.modem.IRadioModem;
import android.hardware.radio.modem.IRadioModemResponse;
import android.hardware.radio.modem.IRadioModemIndication;
import android.hardware.radio.modem.RadioState;
import android.os.IBinder;
import android.os.RemoteException;
import java.util.NoSuchElementException;

/** Unregistered slot1 contract; never accesses host hardware. */
public final class RadioModemService extends IRadioModem.Stub {
  private volatile RadioRequestCounter requestCounter;
  void setRequestCounterForExperiment(RadioRequestCounter counter) { requestCounter = counter; }
  @Override public boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags) throws RemoteException {
    RadioRequestCounter counter = requestCounter;
    if (counter != null) counter.record(code);
    return super.onTransact(code, data, reply, flags);
  }
  private final Object lock = new Object();
  private final Object powerDispatchLock = new Object();
  private final SyntheticSingleSlot syntheticSlot;
  private final SyntheticSingleSlot.Listener stateListener;
  private Callbacks callbacks;
  private volatile RuntimeException powerNotificationFailure;
  RuntimeException getPowerNotificationFailure() { return powerNotificationFailure; }
  public RadioModemService(String slot) { this(slot, null); }
  RadioModemService(String slot, SyntheticSingleSlot model) {
    if (!"slot1".equals(slot)) throw new IllegalArgumentException("only slot1 is supported");
    syntheticSlot=model;
    stateListener=syntheticSlot==null ? null : this::notifyUnavailable;
    if (stateListener!=null) syntheticSlot.addListener(stateListener);
  }
  /** Removes the model listener; an already captured notification may still run. */
  void detachStateListener() {
    if (stateListener!=null) syntheticSlot.removeListener(stateListener);
  }
  private void notifyUnavailable() throws RemoteException {
    synchronized (powerDispatchLock) {
      Callbacks target;
      synchronized (lock) { target=callbacks; if (target==null || target.dead) return; }
      target.indication.radioStateChanged(RadioIndicationType.UNSOLICITED,RadioState.UNAVAILABLE);
    }
  }
  private final class Callbacks implements IBinder.DeathRecipient {
    final IRadioModemResponse response;
    final IRadioModemIndication indication;
    final IBinder responseBinder, indicationBinder;
    boolean responseLinked, indicationLinked, dead;
    Callbacks(IRadioModemResponse r, IRadioModemIndication i) { response=r; indication=i; responseBinder=r.asBinder(); indicationBinder=i.asBinder(); }
    @Override public void binderDied() { synchronized(lock) { dead=true; if (callbacks==this) { callbacks=null; unlink(this); } } }
  }
  private static void unlinkOne(IBinder b, IBinder.DeathRecipient r) {
    try { b.unlinkToDeath(r, 0); } catch (NoSuchElementException expectedAfterDeath) { }
  }
  private static void unlink(Callbacks c) {
    if (c.responseLinked) { c.responseLinked=false; unlinkOne(c.responseBinder,c); }
    if (c.indicationLinked) { c.indicationLinked=false; unlinkOne(c.indicationBinder,c); }
  }
  @Override public void setResponseFunctions(IRadioModemResponse response, IRadioModemIndication indication) throws RemoteException {
    if (response==null || indication==null || response.asBinder()==null || indication.asBinder()==null) throw new RemoteException("both callbacks required");
    synchronized (powerDispatchLock) {
      Callbacks next=new Callbacks(response,indication);
      synchronized(lock) {
        try {
          next.responseBinder.linkToDeath(next,0); next.responseLinked=true;
          next.indicationBinder.linkToDeath(next,0); next.indicationLinked=true;
          if (next.dead) throw new RemoteException("callback already died");
        } catch (RemoteException | RuntimeException error) { unlink(next); throw error; }
        Callbacks old=callbacks; callbacks=next; if (old!=null) unlink(old);
      }
      if (syntheticSlot!=null) {
        try {
          next.indication.rilConnected(RadioIndicationType.UNSOLICITED);
          synchronized(lock) {
            if (callbacks!=next || next.dead) throw new RemoteException("callback died during modem initialization");
          }
          // rilConnected can reenter power or disconnect; do not replay a pre-callback snapshot.
          int currentState=syntheticSlot.radioState();
          next.indication.radioStateChanged(RadioIndicationType.UNSOLICITED, currentState);
        } catch (RemoteException | RuntimeException error) {
          synchronized(lock) { if (callbacks==next) { callbacks=null; unlink(next); } }
          throw error;
        }
      }
    }
  }
  private IRadioModemResponse response() throws RemoteException {
    synchronized(lock) {
      if (callbacks==null || callbacks.dead) throw new RemoteException("callbacks unavailable");
      return callbacks.response;
    }
  }
  private static RadioResponseInfo result(int serial, int error) {
    RadioResponseInfo info=new RadioResponseInfo();
    info.serial=serial; info.error=error; info.type=RadioResponseType.SOLICITED; return info;
  }
  @Override public int getInterfaceVersion() { return IRadioModem.VERSION; }
  @Override public String getInterfaceHash() { return IRadioModem.HASH; }
  @Override public void enableModem(int serial, boolean on) throws RemoteException { response().enableModemResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void getBasebandVersion(int serial) throws RemoteException { response().getBasebandVersionResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), ""); }
  @Override public void getDeviceIdentity(int serial) throws RemoteException { response().getDeviceIdentityResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), "", "", "", ""); }
  @Override public void getHardwareConfig(int serial) throws RemoteException { response().getHardwareConfigResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.modem.HardwareConfig[0]); }
  @Override public void getModemActivityInfo(int serial) throws RemoteException { response().getModemActivityInfoResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.modem.ActivityStatsInfo()); }
  @Override public void getModemStackStatus(int serial) throws RemoteException {
    int error=syntheticSlot==null ? RadioError.REQUEST_NOT_SUPPORTED
        : (syntheticSlot.isConnected() ? RadioError.NONE : RadioError.RADIO_NOT_AVAILABLE);
    response().getModemStackStatusResponse(result(serial,error), error==RadioError.NONE);
  }
  @Override public void getRadioCapability(int serial) throws RemoteException { response().getRadioCapabilityResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.modem.RadioCapability()); }
  @Override public void nvReadItem(int serial, int itemId) throws RemoteException { response().nvReadItemResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), ""); }
  @Override public void nvResetConfig(int serial, int resetType) throws RemoteException { response().nvResetConfigResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void nvWriteCdmaPrl(int serial, byte[] prl) throws RemoteException { response().nvWriteCdmaPrlResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void nvWriteItem(int serial, android.hardware.radio.modem.NvWriteItem item) throws RemoteException { response().nvWriteItemResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void requestShutdown(int serial) throws RemoteException { response().requestShutdownResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void responseAcknowledgement() { }
  @Override public void sendDeviceState(int serial, int deviceStateType, boolean state) throws RemoteException { response().sendDeviceStateResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setRadioCapability(int serial, android.hardware.radio.modem.RadioCapability rc) throws RemoteException { response().setRadioCapabilityResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.modem.RadioCapability()); }
  @Override public void setRadioPower(int serial, boolean powerOn, boolean forEmergencyCall, boolean preferredForEmergencyCall) throws RemoteException {
    synchronized (powerDispatchLock) {
      Callbacks target;
      boolean changed=false;
      int error=RadioError.REQUEST_NOT_SUPPORTED;
      synchronized (lock) {
        if (callbacks==null || callbacks.dead) throw new RemoteException("callbacks unavailable");
        target=callbacks;
        if (syntheticSlot!=null) {
          try { changed=syntheticSlot.setRadioPower(powerOn); error=RadioError.NONE; }
          catch (SyntheticSingleSlot.PowerNotificationException failure) {
            changed=true; error=RadioError.GENERIC_FAILURE; powerNotificationFailure=failure;
          }
          catch (IllegalStateException disconnected) { error=RadioError.RADIO_NOT_AVAILABLE; }
          if (error==RadioError.NONE && syntheticSlot.radioState()==RadioState.UNAVAILABLE)
            error=RadioError.RADIO_NOT_AVAILABLE;
        }
      }
      target.response.setRadioPowerResponse(result(serial,error));
      int requestedState=powerOn ? RadioState.ON : RadioState.OFF;
      if (changed && syntheticSlot.radioState()==requestedState) {
        // A response or state listener can synchronously rebind or retire this peer.
        synchronized (lock) { if (callbacks!=target || target.dead) return; }
        target.indication.radioStateChanged(RadioIndicationType.UNSOLICITED, requestedState);
      }
    }
  }
  @Override public void getImei(int serial) throws RemoteException { response().getImeiResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.modem.ImeiInfo()); }
}
