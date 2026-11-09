package app.aemu.stub;

import android.hardware.radio.RadioError;
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
  private Callbacks callbacks;
  public RadioNetworkService(String slot) { this(slot, null); }
  RadioNetworkService(String slot, SyntheticSingleSlot state) {
    if (!"slot1".equals(slot)) throw new IllegalArgumentException("only slot1 is supported");
    model=state;
  }
  private final class Callbacks implements IBinder.DeathRecipient {
    final IRadioNetworkResponse response;
    final IBinder responseBinder, indicationBinder;
    boolean responseLinked, indicationLinked, dead;
    Callbacks(IRadioNetworkResponse r, IRadioNetworkIndication i) { response=r; responseBinder=r.asBinder(); indicationBinder=i.asBinder(); }
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
  @Override public void getAllowedNetworkTypesBitmap(int serial) throws RemoteException { response().getAllowedNetworkTypesBitmapResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
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
  @Override public void getSignalStrength(int serial) throws RemoteException { response().getSignalStrengthResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.network.SignalStrength()); }
  @Override public void getSystemSelectionChannels(int serial) throws RemoteException { response().getSystemSelectionChannelsResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), new android.hardware.radio.network.RadioAccessSpecifier[0]); }
  @Override public void getVoiceRadioTechnology(int serial) throws RemoteException { response().getVoiceRadioTechnologyResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), 0); }
  @Override public void getVoiceRegistrationState(int serial) throws RemoteException {
    int error=availability(); response().getVoiceRegistrationStateResponse(result(serial,error),registration(error));
  }
  @Override public void isNrDualConnectivityEnabled(int serial) throws RemoteException { response().isNrDualConnectivityEnabledResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED), false); }
  @Override public void responseAcknowledgement() { }
  @Override public void setAllowedNetworkTypesBitmap(int serial, int networkTypeBitmap) throws RemoteException { response().setAllowedNetworkTypesBitmapResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setBandMode(int serial, int mode) throws RemoteException { response().setBandModeResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setBarringPassword(int serial, java.lang.String facility, java.lang.String oldPassword, java.lang.String newPassword) throws RemoteException { response().setBarringPasswordResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setCdmaRoamingPreference(int serial, int type) throws RemoteException { response().setCdmaRoamingPreferenceResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setCellInfoListRate(int serial, int rate) throws RemoteException { response().setCellInfoListRateResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setIndicationFilter(int serial, int indicationFilter) throws RemoteException { response().setIndicationFilterResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setLinkCapacityReportingCriteria(int serial, int hysteresisMs, int hysteresisDlKbps, int hysteresisUlKbps, int[] thresholdsDownlinkKbps, int[] thresholdsUplinkKbps, int accessNetwork) throws RemoteException { response().setLinkCapacityReportingCriteriaResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setLocationUpdates(int serial, boolean enable) throws RemoteException { response().setLocationUpdatesResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setNetworkSelectionModeAutomatic(int serial) throws RemoteException { response().setNetworkSelectionModeAutomaticResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setNetworkSelectionModeManual(int serial, java.lang.String operatorNumeric, int ran) throws RemoteException { response().setNetworkSelectionModeManualResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setNrDualConnectivityState(int serial, byte nrDualConnectivityState) throws RemoteException { response().setNrDualConnectivityStateResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
  @Override public void setSignalStrengthReportingCriteria(int serial, android.hardware.radio.network.SignalThresholdInfo[] signalThresholdInfos) throws RemoteException { response().setSignalStrengthReportingCriteriaResponse(result(serial,RadioError.REQUEST_NOT_SUPPORTED)); }
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
