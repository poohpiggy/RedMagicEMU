package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.RadioResponseType;
import android.hardware.radio.sim.IRadioSim;
import android.hardware.radio.sim.IRadioSimResponse;
import android.hardware.radio.sim.IRadioSimIndication;
import android.os.IBinder;
import android.os.RemoteException;
import java.util.NoSuchElementException;

/** Unregistered slot1 contract; never accesses host hardware. */
public final class RadioSimService extends IRadioSim.Stub {
  private volatile RadioRequestCounter requestCounter;
  void setRequestCounterForExperiment(RadioRequestCounter counter) { requestCounter = counter; }
  @Override public boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags) throws RemoteException {
    RadioRequestCounter counter = requestCounter;
    if (counter != null) counter.record(code);
    return super.onTransact(code, data, reply, flags);
  }
  private final Object lock = new Object();
  private Callbacks callbacks;
  private final SyntheticSingleSlot syntheticSlot;
  public RadioSimService(String slot) { this(slot, null); }
  RadioSimService(String slot, SyntheticSingleSlot slotModel) {
    if (!"slot1".equals(slot)) throw new IllegalArgumentException("only slot1 is supported");
    syntheticSlot = slotModel;
  }
  private final class Callbacks implements IBinder.DeathRecipient {
    final IRadioSimResponse response;
    final IBinder responseBinder, indicationBinder;
    boolean responseLinked, indicationLinked, dead;
    Callbacks(IRadioSimResponse r, IRadioSimIndication i) { response=r; responseBinder=r.asBinder(); indicationBinder=i.asBinder(); }
    @Override public void binderDied() { synchronized(lock) { dead=true; if (callbacks==this) { callbacks=null; unlink(this); } } }
  }
  private static void unlinkOne(IBinder b, IBinder.DeathRecipient r) {
    try { b.unlinkToDeath(r, 0); } catch (NoSuchElementException expectedAfterDeath) { }
  }
  private static void unlink(Callbacks c) {
    if (c.responseLinked) { c.responseLinked=false; unlinkOne(c.responseBinder,c); }
    if (c.indicationLinked) { c.indicationLinked=false; unlinkOne(c.indicationBinder,c); }
  }
  @Override public void setResponseFunctions(IRadioSimResponse response, IRadioSimIndication indication) throws RemoteException {
    if (response==null || indication==null || response.asBinder()==null || indication.asBinder()==null) throw new RemoteException("both callbacks required");
    Callbacks next=new Callbacks(response,indication);
    synchronized(lock) {
      try {
        next.responseBinder.linkToDeath(next,0); next.responseLinked=true;
        next.indicationBinder.linkToDeath(next,0); next.indicationLinked=true;
        if (next.dead) throw new RemoteException("callback already died");
      } catch (RemoteException | RuntimeException error) { unlink(next); throw error; }
      Callbacks old=callbacks; callbacks=next; if (old!=null) unlink(old);
    }
  }
  private IRadioSimResponse response() throws RemoteException {
    synchronized(lock) {
      if (callbacks==null || callbacks.dead) throw new RemoteException("callbacks unavailable");
      return callbacks.response;
    }
  }
  private static RadioResponseInfo result(int serial, int error) {
    RadioResponseInfo info=new RadioResponseInfo();
    info.serial=serial; info.error=error; info.type=RadioResponseType.SOLICITED; return info;
  }
  @Override public int getInterfaceVersion() { return IRadioSim.VERSION; }
  @Override public String getInterfaceHash() { return IRadioSim.HASH; }
  @Override public void areUiccApplicationsEnabled(int serial) throws RemoteException { response().areUiccApplicationsEnabledResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), false); }
  @Override public void changeIccPin2ForApp(int serial, java.lang.String oldPin2, java.lang.String newPin2, java.lang.String aid) throws RemoteException { response().changeIccPin2ForAppResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void changeIccPinForApp(int serial, java.lang.String oldPin, java.lang.String newPin, java.lang.String aid) throws RemoteException { response().changeIccPinForAppResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void enableUiccApplications(int serial, boolean enable) throws RemoteException { response().enableUiccApplicationsResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void getAllowedCarriers(int serial) throws RemoteException { response().getAllowedCarriersResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.sim.CarrierRestrictions(), 0); }
  @Override public void getCdmaSubscription(int serial) throws RemoteException { response().getCdmaSubscriptionResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), "", "", "", "", ""); }
  @Override public void getCdmaSubscriptionSource(int serial) throws RemoteException { response().getCdmaSubscriptionSourceResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void getFacilityLockForApp(int serial, java.lang.String facility, java.lang.String password, int serviceClass, java.lang.String appId) throws RemoteException { response().getFacilityLockForAppResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void getIccCardStatus(int serial) throws RemoteException {
    android.hardware.radio.sim.CardStatus card=new android.hardware.radio.sim.CardStatus();
    if (syntheticSlot == null) {
      response().getIccCardStatusResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED),card); return;
    }
    if (!syntheticSlot.isConnected()) {
      response().getIccCardStatusResponse(result(serial,RadioError.RADIO_NOT_AVAILABLE),card); return;
    }
    card.cardState=android.hardware.radio.sim.CardStatus.STATE_PRESENT;
    card.iccid=syntheticSlot.simProfile().iccid(); card.atr=syntheticSlot.simProfile().atr(); card.eid="";
    card.gsmUmtsSubscriptionAppIndex=0; card.cdmaSubscriptionAppIndex=-1; card.imsSubscriptionAppIndex=-1;
    android.hardware.radio.sim.AppStatus app=new android.hardware.radio.sim.AppStatus();
    app.appType=android.hardware.radio.sim.AppStatus.APP_TYPE_USIM;
    app.appState=android.hardware.radio.sim.AppStatus.APP_STATE_READY;
    app.pin1=android.hardware.radio.sim.PinState.DISABLED;
    app.aidPtr=syntheticSlot.simProfile().aid(); app.appLabelPtr="Synthetic USIM";
    card.applications=new android.hardware.radio.sim.AppStatus[]{app};
    card.slotMap=new android.hardware.radio.config.SlotPortMapping();
    card.slotMap.physicalSlotId=0; card.slotMap.portId=0;
    response().getIccCardStatusResponse(result(serial,RadioError.NONE),card);
  }
  @Override public void getImsiForApp(int serial, java.lang.String aid) throws RemoteException {
    int error=RadioError.REQUEST_NOT_SUPPORTED;
    String imsi="";
    if (syntheticSlot!=null && !syntheticSlot.isConnected()) error=RadioError.RADIO_NOT_AVAILABLE;
    else if (syntheticSlot!=null && syntheticSlot.simProfile().aid().equals(aid)) {
      error=RadioError.NONE; imsi=syntheticSlot.simProfile().imsi();
    }
    response().getImsiForAppResponse(result(serial,error),imsi);
  }
  @Override public void getSimPhonebookCapacity(int serial) throws RemoteException { response().getSimPhonebookCapacityResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.sim.PhonebookCapacity()); }
  @Override public void getSimPhonebookRecords(int serial) throws RemoteException { response().getSimPhonebookRecordsResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void iccCloseLogicalChannel(int serial, int channelId) throws RemoteException { response().iccCloseLogicalChannelResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void iccIoForApp(int serial, android.hardware.radio.sim.IccIo iccIo) throws RemoteException {
    android.hardware.radio.sim.IccIoResult ioResult=new android.hardware.radio.sim.IccIoResult();
    ioResult.simResponse="";
    int error=RadioError.REQUEST_NOT_SUPPORTED;
    if (syntheticSlot!=null && !syntheticSlot.isConnected()) {
      error=RadioError.RADIO_NOT_AVAILABLE;
    } else if (syntheticSlot!=null && iccIo!=null && iccIo.p1==0 && iccIo.p2==0
        && (iccIo.data==null || iccIo.data.isEmpty())
        && (iccIo.pin2==null || iccIo.pin2.isEmpty())) {
      SyntheticSimProfile profile=syntheticSlot.simProfile();
      boolean application=profile.aid().equals(iccIo.aid);
      byte[] content=null;
      if (iccIo.fileId==0x2fe2 && "3F00".equals(iccIo.path)
          && (application || iccIo.aid==null || iccIo.aid.isEmpty())) content=profile.efIccid();
      else if (iccIo.fileId==0x6fad && "3F007FFF".equals(iccIo.path) && application)
        content=profile.efAdministrativeData();
      else if (iccIo.fileId==0x6f46 && "3F007FFF".equals(iccIo.path) && application)
        content=profile.efServiceProviderName();
      else if (iccIo.fileId==0x6f38 && "3F007FFF".equals(iccIo.path) && application)
        content=profile.efUsimServiceTable();
      byte[] resultBytes=null;
      if (content!=null && iccIo.command==0xc0 && iccIo.p3==15) {
        resultBytes=new byte[15]; resultBytes[2]=(byte)(content.length >>> 8);
        resultBytes[3]=(byte)content.length; resultBytes[6]=4; resultBytes[13]=0;
      } else if (content!=null && iccIo.command==0xb0
          && iccIo.p3>0 && iccIo.p3<=content.length) {
        // IccFileHandler can request a bounded prefix without GET_RESPONSE.
        resultBytes=java.util.Arrays.copyOf(content,iccIo.p3);
      }
      if (resultBytes!=null) {
        StringBuilder hex=new StringBuilder(resultBytes.length*2);
        final char[] digits="0123456789ABCDEF".toCharArray();
        for (byte b:resultBytes) { hex.append(digits[(b >>> 4)&15]); hex.append(digits[b&15]); }
        ioResult.sw1=0x90; ioResult.sw2=0; ioResult.simResponse=hex.toString(); error=RadioError.NONE;
      }
    }
    response().iccIoForAppResponse(result(serial,error),ioResult);
  }
  @Override public void iccOpenLogicalChannel(int serial, java.lang.String aid, int p2) throws RemoteException { response().iccOpenLogicalChannelResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0, new byte[0]); }
  @Override public void iccTransmitApduBasicChannel(int serial, android.hardware.radio.sim.SimApdu message) throws RemoteException { response().iccTransmitApduBasicChannelResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.sim.IccIoResult()); }
  @Override public void iccTransmitApduLogicalChannel(int serial, android.hardware.radio.sim.SimApdu message) throws RemoteException { response().iccTransmitApduLogicalChannelResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.sim.IccIoResult()); }
  @Override public void reportStkServiceIsRunning(int serial) throws RemoteException { response().reportStkServiceIsRunningResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void requestIccSimAuthentication(int serial, int authContext, java.lang.String authData, java.lang.String aid) throws RemoteException { response().requestIccSimAuthenticationResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.sim.IccIoResult()); }
  @Override public void responseAcknowledgement() { }
  @Override public void sendEnvelope(int serial, java.lang.String contents) throws RemoteException { response().sendEnvelopeResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), ""); }
  @Override public void sendEnvelopeWithStatus(int serial, java.lang.String contents) throws RemoteException { response().sendEnvelopeWithStatusResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.sim.IccIoResult()); }
  @Override public void sendTerminalResponseToSim(int serial, java.lang.String contents) throws RemoteException { response().sendTerminalResponseToSimResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setAllowedCarriers(int serial, android.hardware.radio.sim.CarrierRestrictions carriers, int multiSimPolicy) throws RemoteException { response().setAllowedCarriersResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setCarrierInfoForImsiEncryption(int serial, android.hardware.radio.sim.ImsiEncryptionInfo imsiEncryptionInfo) throws RemoteException { response().setCarrierInfoForImsiEncryptionResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setCdmaSubscriptionSource(int serial, int cdmaSub) throws RemoteException { response().setCdmaSubscriptionSourceResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setFacilityLockForApp(int serial, java.lang.String facility, boolean lockState, java.lang.String password, int serviceClass, java.lang.String appId) throws RemoteException { response().setFacilityLockForAppResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void setSimCardPower(int serial, int powerUp) throws RemoteException { response().setSimCardPowerResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setUiccSubscription(int serial, android.hardware.radio.sim.SelectUiccSub uiccSub) throws RemoteException { response().setUiccSubscriptionResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void supplyIccPin2ForApp(int serial, java.lang.String pin2, java.lang.String aid) throws RemoteException { response().supplyIccPin2ForAppResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void supplyIccPinForApp(int serial, java.lang.String pin, java.lang.String aid) throws RemoteException { response().supplyIccPinForAppResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void supplyIccPuk2ForApp(int serial, java.lang.String puk2, java.lang.String pin2, java.lang.String aid) throws RemoteException { response().supplyIccPuk2ForAppResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void supplyIccPukForApp(int serial, java.lang.String puk, java.lang.String pin, java.lang.String aid) throws RemoteException { response().supplyIccPukForAppResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void supplySimDepersonalization(int serial, int persoType, java.lang.String controlKey) throws RemoteException { response().supplySimDepersonalizationResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0, 0); }
  @Override public void updateSimPhonebookRecords(int serial, android.hardware.radio.sim.PhonebookRecordInfo recordInfo) throws RemoteException { response().updateSimPhonebookRecordsResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void iccCloseLogicalChannelWithSessionInfo(int serial, android.hardware.radio.sim.SessionInfo sessionInfo) throws RemoteException { response().iccCloseLogicalChannelWithSessionInfoResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
}
