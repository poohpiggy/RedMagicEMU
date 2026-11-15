package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioConst;
import android.hardware.radio.RadioIndicationType;
import android.hardware.radio.AccessNetwork;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.ReentrantLock;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.RadioResponseType;
import android.hardware.radio.RadioTechnology;
import android.hardware.radio.modem.RadioState;
import android.hardware.radio.network.*;
import android.os.IBinder;
import android.os.RemoteException;
import java.util.NoSuchElementException;

/** Synthetic network prototype; it never queries a real host network. */
public final class RadioNetworkService extends IRadioNetwork.Stub {
  private volatile RadioRequestCounter requestCounter;
  void setRequestCounterForExperiment(RadioRequestCounter counter) { requestCounter = counter; }
  @Override public boolean onTransact(int code, android.os.Parcel data, android.os.Parcel reply, int flags) throws RemoteException {
    RadioRequestCounter counter = requestCounter;
    if (counter != null) counter.record(code);
    return super.onTransact(code, data, reply, flags);
  }
  private final Object lock=new Object();
  private final SyntheticSingleSlot model;
  private final ReentrantLock dispatchLock=new ReentrantLock();
  private final Runnable stateListener;
  private final ExecutorService signalExecutor=Executors.newSingleThreadExecutor(r -> {
    Thread thread=new Thread(r,"radio-network-signal"); thread.setDaemon(true); return thread;
  });
  private volatile Throwable signalFailure;
  Throwable getSignalFailure() { return signalFailure; }
  private boolean closed, signalQueued, signalDirty;
  private int indicationFilter=IndicationFilter.ALL;
  private int allowedNetworkTypes=1<<13; // LTE only; this service does not emulate GSM/WCDMA/NR.
  private final Map<Integer,SignalThresholdInfo> criteria=new HashMap<>();
  private Callbacks callbacks;
  public RadioNetworkService(String slot) { this(slot, null); }
  RadioNetworkService(String slot, SyntheticSingleSlot state) {
    if (!"slot1".equals(slot)) throw new IllegalArgumentException("only slot1 is supported");
    model=state;
    stateListener=model==null ? null : this::queueSignal;
    if (stateListener!=null) model.addRadioStateListener(stateListener);
  }
  private boolean acquireDispatch() {
    try { return dispatchLock.tryLock(2, TimeUnit.SECONDS); }
    catch (InterruptedException interrupted) { Thread.currentThread().interrupt(); return false; }
  }
  void close() {
    if (!acquireDispatch()) throw new IllegalStateException("signal delivery did not stop");
    try {
      synchronized(lock) { closed=true; Callbacks old=callbacks; callbacks=null; if(old!=null) unlink(old); }
      if (stateListener!=null) model.removeRadioStateListener(stateListener);
      signalExecutor.shutdownNow();
    } finally { dispatchLock.unlock(); }
  }
  // Only one task may be queued or running; transitions during delivery collapse to the latest state.
  private void queueSignal() {
    synchronized(lock) {
      if (closed) return;
      signalDirty=true;
      if (signalQueued) return;
      signalQueued=true;
      try { signalExecutor.execute(this::drainSignal); }
      catch (RejectedExecutionException error) { signalQueued=false; if (!closed) throw error; }
    }
  }
  private void drainSignal() {
    for (;;) {
      synchronized(lock) { if (closed || !signalDirty) { signalQueued=false; return; } signalDirty=false; }
      try { notifySignal(); }
      catch (RemoteException | RuntimeException error) {
        signalFailure=error;
        System.err.println("RADIO_NETWORK_SIGNAL_FAILED " + error.getClass().getSimpleName());
      }
    }
  }
  private final class Callbacks implements IBinder.DeathRecipient {
    final IRadioNetworkResponse response;
    final IRadioNetworkIndication indication;
    final IBinder responseBinder, indicationBinder;
    boolean responseLinked, indicationLinked, dead;
    Callbacks(IRadioNetworkResponse r, IRadioNetworkIndication i) { response=r; indication=i; responseBinder=r.asBinder(); indicationBinder=i.asBinder(); }
    @Override public void binderDied() { synchronized(lock) { dead=true; if (callbacks==this) { callbacks=null; unlink(this); } } }
  }
  private static void unlinkOne(IBinder b, IBinder.DeathRecipient r) {
    try { b.unlinkToDeath(r, 0); } catch (NoSuchElementException expectedAfterDeath) { }
  }
  private static void unlink(Callbacks c) {
    if (c.responseLinked) { c.responseLinked=false; unlinkOne(c.responseBinder,c); }
    if (c.indicationLinked) { c.indicationLinked=false; unlinkOne(c.indicationBinder,c); }
  }
  @Override public void setResponseFunctions(IRadioNetworkResponse response, IRadioNetworkIndication indication) throws RemoteException {
    if (response==null || indication==null || response.asBinder()==null || indication.asBinder()==null) throw new RemoteException("both callbacks required");
    if (!acquireDispatch()) throw new RemoteException("signal delivery did not stop");
    try {
      Callbacks next=new Callbacks(response,indication);
      synchronized(lock) {
        if (closed) throw new RemoteException("service closed");
        try {
          next.responseBinder.linkToDeath(next,0); next.responseLinked=true;
          next.indicationBinder.linkToDeath(next,0); next.indicationLinked=true;
          if (next.dead) throw new RemoteException("callback already died");
        } catch (RemoteException | RuntimeException error) { unlink(next); throw error; }
        Callbacks old=callbacks; callbacks=next; if (old!=null) unlink(old);
      }
    } finally { dispatchLock.unlock(); }
    queueSignal();
  }
  private IRadioNetworkResponse response() throws RemoteException {
    synchronized(lock) {
      if (callbacks==null || callbacks.dead) throw new RemoteException("callbacks unavailable");
      return callbacks.response;
    }
  }
  private static RadioResponseInfo result(int serial, int error) {
    RadioResponseInfo info=new RadioResponseInfo(); info.serial=serial;
    info.error=error; info.type=RadioResponseType.SOLICITED; return info;
  }
  private int availability() {
    if (model==null) return RadioError.REQUEST_NOT_SUPPORTED;
    return model.radioState()==RadioState.ON ? RadioError.NONE : RadioError.RADIO_NOT_AVAILABLE;
  }
  // Fixed synthetic sample: RSRP/RSRQ are positive magnitudes; RSSNR is in 0.1 dB.
  private static final int LTE_RSSI=20, LTE_RSRP=95, LTE_RSRQ=10, LTE_RSSNR=100;
  private static SignalStrength signal(boolean available) {
    int u=RadioConst.VALUE_UNAVAILABLE;
    SignalStrength s=new SignalStrength();
    s.gsm=new GsmSignalStrength(); s.gsm.signalStrength=u; s.gsm.bitErrorRate=u; s.gsm.timingAdvance=u;
    s.cdma=new CdmaSignalStrength(); s.cdma.dbm=u; s.cdma.ecio=u;
    s.evdo=new EvdoSignalStrength(); s.evdo.dbm=u; s.evdo.ecio=u; s.evdo.signalNoiseRatio=u;
    s.lte=new LteSignalStrength(); s.lte.signalStrength=available?LTE_RSSI:u; s.lte.rsrp=available?LTE_RSRP:u;
    s.lte.rsrq=available?LTE_RSRQ:u; s.lte.rssnr=available?LTE_RSSNR:u;
    s.lte.cqi=u; s.lte.timingAdvance=u; s.lte.cqiTableIndex=u;
    s.tdscdma=new TdscdmaSignalStrength(); s.tdscdma.signalStrength=u; s.tdscdma.bitErrorRate=u; s.tdscdma.rscp=u;
    s.wcdma=new WcdmaSignalStrength(); s.wcdma.signalStrength=u; s.wcdma.bitErrorRate=u; s.wcdma.rscp=u; s.wcdma.ecno=u;
    s.nr=new NrSignalStrength(); s.nr.ssRsrp=u; s.nr.ssRsrq=u; s.nr.ssSinr=u; s.nr.csiRsrp=u;
    s.nr.csiRsrq=u; s.nr.csiSinr=u; s.nr.csiCqiTableIndex=u; s.nr.csiCqiReport=new byte[0]; s.nr.timingAdvance=u;
    return s;
  }
  private void notifySignal() throws RemoteException {
    dispatchLock.lock();
    try {
      Callbacks target;
      boolean available;
      synchronized(lock) {
        target=callbacks;
        if (model==null || closed || target==null || target.dead || (indicationFilter & IndicationFilter.SIGNAL_STRENGTH)==0) return;
        boolean enabled=criteria.isEmpty();
        for (SignalThresholdInfo info:criteria.values()) enabled |= info.isEnabled;
        available=model.radioState()==RadioState.ON;
        if (available && !enabled) return;
      }
      target.indication.currentSignalStrength(RadioIndicationType.UNSOLICITED,signal(available));
    } finally { dispatchLock.unlock(); }
  }
  private static int validateCriteria(SignalThresholdInfo[] infos) {
    if (infos==null) return RadioError.INVALID_ARGUMENTS;
    boolean[] seen=new boolean[6];
    for (SignalThresholdInfo info:infos) {
      if (info==null || info.thresholds==null || info.hysteresisMs<0 || info.hysteresisDb<0) return RadioError.INVALID_ARGUMENTS;
      if (info.ran<AccessNetwork.UNKNOWN || info.ran>AccessNetwork.NGRAN || info.signalMeasurement<1 || info.signalMeasurement>9) return RadioError.INVALID_ARGUMENTS;
      if (info.ran!=AccessNetwork.EUTRAN || info.signalMeasurement<3 || info.signalMeasurement>5) return RadioError.REQUEST_NOT_SUPPORTED;
      int type=info.signalMeasurement;
      if (seen[type]) return RadioError.INVALID_ARGUMENTS;
      seen[type]=true;
      int min=type==3?-140:type==4?-34:-20, max=type==3?-44:type==4?3:30;
      for(int i=0;i<info.thresholds.length;i++) {
        int value=info.thresholds[i];
        if(value<min || value>max || (i>0 && ((long)value-info.thresholds[i-1]<=info.hysteresisDb))) return RadioError.INVALID_ARGUMENTS;
      }
    }
    return RadioError.NONE;
  }
  private RegStateResult registration(int error) {
    RegStateResult state=new RegStateResult();
    boolean available=error==RadioError.NONE;
    state.regState=available ? RegState.REG_HOME : RegState.NOT_REG_MT_NOT_SEARCHING_OP;
    state.rat=available ? RadioTechnology.LTE : RadioTechnology.UNKNOWN;
    state.registeredPlmn=available ? model.simProfile().operatorNumeric() : "";
    state.cellIdentity=CellIdentity.noinit(false);
    state.accessTechnologySpecificInfo=AccessTechnologySpecificInfo.noinit(false);
    return state;
  }
  @Override public int getInterfaceVersion() { return IRadioNetwork.VERSION; }
  @Override public String getInterfaceHash() { return IRadioNetwork.HASH; }
  @Override public void getAllowedNetworkTypesBitmap(int serial) throws RemoteException {
    int error=model==null ? RadioError.REQUEST_NOT_SUPPORTED : RadioError.NONE;
    int value;
    synchronized(lock) { value=allowedNetworkTypes; }
    response().getAllowedNetworkTypesBitmapResponse(result(serial,error), error==RadioError.NONE ? value : 0);
  }
  @Override public void getAvailableBandModes(int serial) throws RemoteException { response().getAvailableBandModesResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new int[0]); }
  @Override public void getAvailableNetworks(int serial) throws RemoteException { response().getAvailableNetworksResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.network.OperatorInfo[0]); }
  @Override public void getBarringInfo(int serial) throws RemoteException { response().getBarringInfoResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.network.CellIdentity(), new android.hardware.radio.network.BarringInfo[0]); }
  @Override public void getCdmaRoamingPreference(int serial) throws RemoteException { response().getCdmaRoamingPreferenceResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void getCellInfoList(int serial) throws RemoteException { response().getCellInfoListResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.network.CellInfo[0]); }
  @Override public void getDataRegistrationState(int serial) throws RemoteException {
    int error=availability(); response().getDataRegistrationStateResponse(result(serial,error),registration(error));
  }
  @Override public void getImsRegistrationState(int serial) throws RemoteException { response().getImsRegistrationStateResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), false, 0); }
  @Override public void getNetworkSelectionMode(int serial) throws RemoteException { response().getNetworkSelectionModeResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), false); }
  @Override public void getOperator(int serial) throws RemoteException {
    int error=availability();
    String name=error==RadioError.NONE ? model.simProfile().operatorName() : "";
    String numeric=error==RadioError.NONE ? model.simProfile().operatorNumeric() : "";
    response().getOperatorResponse(result(serial,error),name,name,numeric);
  }
  @Override public void getSignalStrength(int serial) throws RemoteException { int error=availability(); response().getSignalStrengthResponse(result(serial,error),signal(error==RadioError.NONE)); }
  @Override public void getSystemSelectionChannels(int serial) throws RemoteException { response().getSystemSelectionChannelsResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.network.RadioAccessSpecifier[0]); }
  @Override public void getVoiceRadioTechnology(int serial) throws RemoteException { response().getVoiceRadioTechnologyResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void getVoiceRegistrationState(int serial) throws RemoteException {
    int error=availability(); response().getVoiceRegistrationStateResponse(result(serial,error),registration(error));
  }
  @Override public void isNrDualConnectivityEnabled(int serial) throws RemoteException { response().isNrDualConnectivityEnabledResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), false); }
  @Override public void responseAcknowledgement() { }
  @Override public void setAllowedNetworkTypesBitmap(int serial, int networkTypeBitmap) throws RemoteException {
    synchronized(lock) { if (model!=null) allowedNetworkTypes=networkTypeBitmap & (1<<13); }
    response().setAllowedNetworkTypesBitmapResponse(result(serial,
            model==null ? RadioError.REQUEST_NOT_SUPPORTED : RadioError.NONE));
  }
  @Override public void setBandMode(int serial, int mode) throws RemoteException { response().setBandModeResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setBarringPassword(int serial, java.lang.String facility, java.lang.String oldPassword, java.lang.String newPassword) throws RemoteException { response().setBarringPasswordResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setCdmaRoamingPreference(int serial, int type) throws RemoteException { response().setCdmaRoamingPreferenceResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setCellInfoListRate(int serial, int rate) throws RemoteException { response().setCellInfoListRateResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setIndicationFilter(int serial, int filter) throws RemoteException {
    int error=model==null ? RadioError.REQUEST_NOT_SUPPORTED : (model.isConnected() ? RadioError.NONE : RadioError.RADIO_NOT_AVAILABLE);
    if (error==RadioError.NONE && filter!=IndicationFilter.ALL && (filter & ~127)!=0) error=RadioError.INVALID_ARGUMENTS;
    boolean refresh=false;
    if (error==RadioError.NONE) synchronized(lock) {
      refresh=(indicationFilter & IndicationFilter.SIGNAL_STRENGTH)==0 && (filter & IndicationFilter.SIGNAL_STRENGTH)!=0;
      indicationFilter=filter;
    }
    response().setIndicationFilterResponse(result(serial,error));
    if (refresh) queueSignal();
  }
  @Override public void setLinkCapacityReportingCriteria(int serial, int hysteresisMs, int hysteresisDlKbps, int hysteresisUlKbps, int[] thresholdsDownlinkKbps, int[] thresholdsUplinkKbps, int accessNetwork) throws RemoteException { response().setLinkCapacityReportingCriteriaResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setLocationUpdates(int serial, boolean enable) throws RemoteException { response().setLocationUpdatesResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setNetworkSelectionModeAutomatic(int serial) throws RemoteException { response().setNetworkSelectionModeAutomaticResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setNetworkSelectionModeManual(int serial, java.lang.String operatorNumeric, int ran) throws RemoteException { response().setNetworkSelectionModeManualResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setNrDualConnectivityState(int serial, byte nrDualConnectivityState) throws RemoteException { response().setNrDualConnectivityStateResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setSignalStrengthReportingCriteria(int serial, SignalThresholdInfo[] infos) throws RemoteException {
    int error=model==null ? RadioError.REQUEST_NOT_SUPPORTED : (model.isConnected() ? RadioError.NONE : RadioError.RADIO_NOT_AVAILABLE);
    if (error==RadioError.NONE) error=validateCriteria(infos);
    boolean refresh=false;
    if (error==RadioError.NONE) synchronized(lock) {
      boolean wasEnabled=criteria.isEmpty();
      for (SignalThresholdInfo old:criteria.values()) wasEnabled |= old.isEnabled;
      for (SignalThresholdInfo info:infos) {
        SignalThresholdInfo copy=new SignalThresholdInfo(); copy.ran=info.ran; copy.signalMeasurement=info.signalMeasurement;
        copy.hysteresisMs=info.hysteresisMs; copy.hysteresisDb=info.hysteresisDb; copy.thresholds=info.thresholds.clone(); copy.isEnabled=info.isEnabled;
        criteria.put(copy.signalMeasurement,copy);
      }
      boolean enabled=criteria.isEmpty();
      for (SignalThresholdInfo current:criteria.values()) enabled |= current.isEnabled;
      refresh=!wasEnabled && enabled;
    }
    response().setSignalStrengthReportingCriteriaResponse(result(serial,error));
    if (refresh) queueSignal();
  }
  @Override public void setSuppServiceNotifications(int serial, boolean enable) throws RemoteException { response().setSuppServiceNotificationsResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setSystemSelectionChannels(int serial, boolean specifyChannels, android.hardware.radio.network.RadioAccessSpecifier[] specifiers) throws RemoteException { response().setSystemSelectionChannelsResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void startNetworkScan(int serial, android.hardware.radio.network.NetworkScanRequest request) throws RemoteException { response().startNetworkScanResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void stopNetworkScan(int serial) throws RemoteException { response().stopNetworkScanResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void supplyNetworkDepersonalization(int serial, java.lang.String netPin) throws RemoteException { response().supplyNetworkDepersonalizationResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void setUsageSetting(int serial, int usageSetting) throws RemoteException { response().setUsageSettingResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void getUsageSetting(int serial) throws RemoteException { response().getUsageSettingResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void setEmergencyMode(int serial, int emcModeType) throws RemoteException { response().setEmergencyModeResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.network.EmergencyRegResult()); }
  @Override public void triggerEmergencyNetworkScan(int serial, android.hardware.radio.network.EmergencyNetworkScanTrigger request) throws RemoteException { response().triggerEmergencyNetworkScanResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void cancelEmergencyNetworkScan(int serial, boolean resetScan) throws RemoteException { response().cancelEmergencyNetworkScanResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void exitEmergencyMode(int serial) throws RemoteException { response().exitEmergencyModeResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setNullCipherAndIntegrityEnabled(int serial, boolean enabled) throws RemoteException { response().setNullCipherAndIntegrityEnabledResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void isNullCipherAndIntegrityEnabled(int serial) throws RemoteException { response().isNullCipherAndIntegrityEnabledResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), false); }
  @Override public void isN1ModeEnabled(int serial) throws RemoteException { response().isN1ModeEnabledResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), false); }
  @Override public void setN1ModeEnabled(int serial, boolean enable) throws RemoteException { response().setN1ModeEnabledResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void isCellularIdentifierTransparencyEnabled(int serial) throws RemoteException { response().isCellularIdentifierTransparencyEnabledResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), false); }
  @Override public void setCellularIdentifierTransparencyEnabled(int serial, boolean enabled) throws RemoteException { response().setCellularIdentifierTransparencyEnabledResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setSecurityAlgorithmsUpdatedEnabled(int serial, boolean enable) throws RemoteException { response().setSecurityAlgorithmsUpdatedEnabledResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void isSecurityAlgorithmsUpdatedEnabled(int serial) throws RemoteException { response().isSecurityAlgorithmsUpdatedEnabledResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), false); }
  @Override public void setSatellitePlmn(int serial, java.lang.String[] carrierPlmnArray, java.lang.String[] allSatellitePlmnArray) throws RemoteException { response().setSatellitePlmnResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setSatelliteEnabledForCarrier(int serial, boolean satelliteEnabled) throws RemoteException { response().setSatelliteEnabledForCarrierResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void isSatelliteEnabledForCarrier(int serial) throws RemoteException { response().isSatelliteEnabledForCarrierResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), false); }
}
