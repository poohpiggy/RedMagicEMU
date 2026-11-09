package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.modem.IRadioModem;
import android.hardware.radio.modem.IRadioModemResponse;
import android.hardware.radio.modem.IRadioModemIndication;
import android.hardware.radio.modem.RadioState;
import android.hardware.radio.sim.IRadioSim;
import android.hardware.radio.sim.IRadioSimResponse;
import android.hardware.radio.sim.IRadioSimIndication;
import android.hardware.radio.sim.CardStatus;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.*;
import java.util.*;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** Only test callbacks: never registers a service or reads a real SIM. */
public final class RadioModemSimContractTest {
  private static void check(boolean b, String message) { if (!b) throw new AssertionError(message); }
  private static final class Peer {
    final List<IBinder.DeathRecipient> deaths = new ArrayList<>();
    final IBinder binder = (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class}, (p,m,a) -> {
      switch (m.getName()) {
        case "linkToDeath": deaths.add((IBinder.DeathRecipient)a[0]); return null;
        case "unlinkToDeath": return deaths.remove(a[0]);
        case "isBinderAlive": return true;
        case "hashCode": return System.identityHashCode(p);
        case "equals": return p == a[0];
        case "toString": return "test-peer";
        default: throw new UnsupportedOperationException(m.getName());
      }
    });
  }
  private static final class Calls implements InvocationHandler {
    final Peer peer = new Peer(); String name; Object[] args;
    final List<String> received = new ArrayList<>();
    final List<Integer> radioStates = new ArrayList<>();
    Runnable onRilConnected;
    Runnable onPowerResponse;
    @Override public Object invoke(Object p, Method m, Object[] a) {
      if (m.getName().equals("asBinder")) return peer.binder;
      if (m.getName().equals("toString")) return "test-callback";
      name=m.getName(); args=a; received.add(name);
      if (name.equals("radioStateChanged")) radioStates.add((Integer)a[1]);
      if (name.equals("rilConnected") && onRilConnected!=null) onRilConnected.run();
      if (name.equals("setRadioPowerResponse") && onPowerResponse!=null) onPowerResponse.run();
      return null;
    }
    Object proxy(Class<?> type) { return Proxy.newProxyInstance(type.getClassLoader(), new Class<?>[]{type}, this); }
    void die() { for (IBinder.DeathRecipient d : new ArrayList<>(peer.deaths)) d.binderDied(); }
    void expect(String n, int serial, int error) {
      check(n.equals(name), "callback " + name + " expected " + n);
      RadioResponseInfo i=(RadioResponseInfo) args[0];
      check(i.serial==serial && i.error==error, "incorrect serial/error for " + n);
    }
  }
  private static Object argument(Class<?> t, int serial) {
    if (t == int.class) return serial;
    if (t == byte.class) return (byte) 0;
    if (t == boolean.class) return false;
    if (t.isArray()) return Array.newInstance(t.getComponentType(),0);
    return null;
  }
  private static int unsupported(Object service, Class<?> iface, Calls response, Set<String> implemented) throws Exception {
    int serial=100;
    for (Method m : iface.getDeclaredMethods()) {
      String n=m.getName();
      if (n.equals("setResponseFunctions") || n.equals("responseAcknowledgement") || n.equals("getInterfaceVersion") || n.equals("getInterfaceHash") || implemented.contains(n)) continue;
      Class<?>[] types=m.getParameterTypes(); check(types.length>0 && types[0]==int.class,"bad request "+n);
      Object[] a=new Object[types.length]; for (int i=0;i<a.length;i++) a[i]=argument(types[i],i==0?serial:0);
      response.name=null;
      try { m.invoke(service,a); } catch (InvocationTargetException e) { throw new AssertionError(n,e.getCause()); }
      response.expect(n+"Response",serial++,RadioError.REQUEST_NOT_SUPPORTED);
    }
    return serial-100;
  }
  public static void main(String[] args) throws Exception {
    try { new RadioModemService("slot2"); throw new AssertionError("slot2 accepted"); } catch (IllegalArgumentException expected) {}
    try { new RadioSimService("slot2"); throw new AssertionError("slot2 accepted"); } catch (IllegalArgumentException expected) {}
    SyntheticSingleSlot model=SyntheticSingleSlot.forTests();
    RadioModemService modem=new RadioModemService("slot1", model);
    RadioSimService sim=new RadioSimService("slot1", model);
    check(modem.getInterfaceHash().equals(IRadioModem.HASH) && modem.getInterfaceVersion()==IRadioModem.VERSION,"modem ABI");
    check(sim.getInterfaceHash().equals(IRadioSim.HASH) && sim.getInterfaceVersion()==IRadioSim.VERSION,"SIM ABI");
    Calls mr=new Calls(),mi=new Calls(),sr=new Calls(),si=new Calls();
    modem.setResponseFunctions((IRadioModemResponse)mr.proxy(IRadioModemResponse.class),(IRadioModemIndication)mi.proxy(IRadioModemIndication.class));
    check(mi.received.equals(Arrays.asList("rilConnected", "radioStateChanged")),"initial modem notifications missing or out of order");
    check(((Integer)mi.args[1])==RadioState.OFF,"initial radio state should be OFF");
    sim.setResponseFunctions((IRadioSimResponse)sr.proxy(IRadioSimResponse.class),(IRadioSimIndication)si.proxy(IRadioSimIndication.class));
    int mc=unsupported(modem,IRadioModem.class,mr,new HashSet<>(Arrays.asList("getModemStackStatus", "setRadioPower")));
    int sc=unsupported(sim,IRadioSim.class,sr,Collections.singleton("getIccCardStatus"));
    sim.getIccCardStatus(501); sr.expect("getIccCardStatusResponse",501,RadioError.NONE);
    CardStatus card=(CardStatus)sr.args[1];
    check(card.cardState==CardStatus.STATE_PRESENT,"synthetic card missing");
    check("89000000000000000004".equals(card.iccid) && "3B00".equals(card.atr),"synthetic identity differs from config");
    check(card.slotMap.physicalSlotId==0 && card.slotMap.portId==0,"incorrect single-slot mapping");
    check(card.applications.length==1 && card.gsmUmtsSubscriptionAppIndex==0 &&
          card.applications[0].appState==android.hardware.radio.sim.AppStatus.APP_STATE_READY,
          "synthetic SIM application not ready");
    modem.getModemStackStatus(502); mr.expect("getModemStackStatusResponse",502,RadioError.NONE);
    check(Boolean.TRUE.equals(mr.args[1]),"simulated stack must match the single live modem");
    modem.setRadioPower(503,true,false,false); mr.expect("setRadioPowerResponse",503,RadioError.NONE);
    check("radioStateChanged".equals(mi.name) && ((Integer)mi.args[1])==RadioState.ON,"simulated radio did not turn on");
    check(model.radioState()==RadioState.ON,"modem did not update shared state");
    check(model.isNetworkRegistered() && model.isSimReady() && model.canDeliverVoiceOrSms(),"power on did not enable synthetic delivery model");
    modem.setRadioPower(504,false,false,false); mr.expect("setRadioPowerResponse",504,RadioError.NONE);
    check("radioStateChanged".equals(mi.name) && ((Integer)mi.args[1])==RadioState.OFF,"simulated radio did not turn off");
    SyntheticSingleSlot failedPowerModel=SyntheticSingleSlot.forTests();
    RadioModemService failedPowerModem=new RadioModemService("slot1",failedPowerModel);
    Calls failedPowerResponse=new Calls(),failedPowerIndication=new Calls();
    failedPowerModem.setResponseFunctions((IRadioModemResponse)failedPowerResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)failedPowerIndication.proxy(IRadioModemIndication.class));
    failedPowerModem.setRadioPower(601,true,false,false);
    final int[] notified={0};
    failedPowerModel.addDeliveryListener(() -> { throw new IllegalStateException("test listener failure"); });
    failedPowerModel.addDeliveryListener(() -> notified[0]++);
    failedPowerIndication.received.clear();
    failedPowerModem.setRadioPower(602,false,false,false);
    failedPowerResponse.expect("setRadioPowerResponse",602,RadioError.GENERIC_FAILURE);
    check(failedPowerModem.getPowerNotificationFailure()!=null && failedPowerModem.getPowerNotificationFailure().getCause() instanceof IllegalStateException,"listener failure detail was lost");
    check(notified[0]==1,"listener failure blocked another listener");
    check(failedPowerModel.radioState()==RadioState.OFF,"failed notification rolled back power off");
    check(failedPowerIndication.received.equals(Collections.singletonList("radioStateChanged")) && ((Integer)failedPowerIndication.args[1])==RadioState.OFF,"failed notification did not report actual radio state");
    SyntheticSingleSlot racingModel=SyntheticSingleSlot.forTests();
    RadioModemService racingModem=new RadioModemService("slot1",racingModel);
    Calls racingResponse=new Calls(),racingIndication=new Calls();
    racingModem.setResponseFunctions((IRadioModemResponse)racingResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)racingIndication.proxy(IRadioModemIndication.class));
    racingModem.setRadioPower(603,true,false,false);
    racingModel.addDeliveryListener(() -> { try { racingModel.disconnect(); } catch (RemoteException e) { throw new RuntimeException(e); } });
    racingIndication.received.clear();
    racingModem.setRadioPower(604,false,false,false);
    check(racingIndication.received.equals(Collections.singletonList("radioStateChanged")) && ((Integer)racingIndication.args[1])==RadioState.UNAVAILABLE,"stale OFF followed UNAVAILABLE during listener");
    racingResponse.expect("setRadioPowerResponse",604,RadioError.RADIO_NOT_AVAILABLE);
    SyntheticSingleSlot responseRaceModel=SyntheticSingleSlot.forTests();
    RadioModemService responseRaceModem=new RadioModemService("slot1",responseRaceModel);
    Calls responseRaceResponse=new Calls(),responseRaceIndication=new Calls();
    responseRaceModem.setResponseFunctions((IRadioModemResponse)responseRaceResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)responseRaceIndication.proxy(IRadioModemIndication.class));
    responseRaceModem.setRadioPower(605,true,false,false);
    responseRaceResponse.onPowerResponse=() -> { try { responseRaceModel.disconnect(); } catch (RemoteException e) { throw new RuntimeException(e); } };
    responseRaceIndication.received.clear();
    responseRaceModem.setRadioPower(606,false,false,false);
    check(responseRaceIndication.received.equals(Collections.singletonList("radioStateChanged")) && ((Integer)responseRaceIndication.args[1])==RadioState.UNAVAILABLE,"stale OFF followed UNAVAILABLE during response");
    SyntheticSingleSlot rebindModel=SyntheticSingleSlot.forTests();
    RadioModemService rebindModem=new RadioModemService("slot1",rebindModel);
    Calls oldPowerResponse=new Calls(),oldPowerIndication=new Calls();
    Calls newPowerResponse=new Calls(),newPowerIndication=new Calls();
    rebindModem.setResponseFunctions((IRadioModemResponse)oldPowerResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)oldPowerIndication.proxy(IRadioModemIndication.class));
    IBinder.DeathRecipient oldPowerDeath=oldPowerResponse.peer.deaths.get(0);
    oldPowerIndication.received.clear(); oldPowerIndication.radioStates.clear();
    oldPowerResponse.onPowerResponse=() -> {
      try { rebindModem.setResponseFunctions((IRadioModemResponse)newPowerResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)newPowerIndication.proxy(IRadioModemIndication.class)); }
      catch (RemoteException error) { throw new RuntimeException(error); }
    };
    rebindModem.setRadioPower(702,true,false,false);
    oldPowerResponse.expect("setRadioPowerResponse",702,RadioError.NONE);
    check(oldPowerIndication.radioStates.isEmpty(),"retired callback received power indication after response rebind");
    check(newPowerIndication.received.equals(Arrays.asList("rilConnected","radioStateChanged")) &&
        newPowerIndication.radioStates.equals(Collections.singletonList(RadioState.ON)),"new callback did not receive current ON state");
    oldPowerDeath.binderDied();
    rebindModem.getModemStackStatus(703); newPowerResponse.expect("getModemStackStatusResponse",703,RadioError.NONE);
    SyntheticSingleSlot dyingPowerModel=SyntheticSingleSlot.forTests();
    RadioModemService dyingPowerModem=new RadioModemService("slot1",dyingPowerModel);
    Calls dyingPowerResponse=new Calls(),dyingPowerIndication=new Calls();
    dyingPowerModem.setResponseFunctions((IRadioModemResponse)dyingPowerResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)dyingPowerIndication.proxy(IRadioModemIndication.class));
    dyingPowerIndication.received.clear(); dyingPowerIndication.radioStates.clear();
    dyingPowerResponse.onPowerResponse=dyingPowerResponse::die;
    dyingPowerModem.setRadioPower(704,true,false,false);
    dyingPowerResponse.expect("setRadioPowerResponse",704,RadioError.NONE);
    check(dyingPowerIndication.radioStates.isEmpty(),"dead callback received power indication after response");
    try { dyingPowerModem.getModemStackStatus(705); throw new AssertionError("dead power callback still bound"); }
    catch (RemoteException expected) { }
    SyntheticSingleSlot dyingIndModel=SyntheticSingleSlot.forTests();
    RadioModemService dyingIndModem=new RadioModemService("slot1",dyingIndModel);
    Calls dyingIndResponse=new Calls(),dyingPowerIndicationPeer=new Calls();
    dyingIndModem.setResponseFunctions((IRadioModemResponse)dyingIndResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)dyingPowerIndicationPeer.proxy(IRadioModemIndication.class));
    dyingPowerIndicationPeer.radioStates.clear(); dyingIndResponse.onPowerResponse=dyingPowerIndicationPeer::die;
    dyingIndModem.setRadioPower(706,true,false,false);
    dyingIndResponse.expect("setRadioPowerResponse",706,RadioError.NONE);
    check(dyingPowerIndicationPeer.radioStates.isEmpty(),"dead indication received power state after response");
    SyntheticSingleSlot listenerRebindModel=SyntheticSingleSlot.forTests();
    RadioModemService listenerRebindModem=new RadioModemService("slot1",listenerRebindModel);
    Calls listenerOldResponse=new Calls(),listenerOldIndication=new Calls();
    Calls listenerNewResponse=new Calls(),listenerNewIndication=new Calls();
    listenerRebindModem.setResponseFunctions((IRadioModemResponse)listenerOldResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)listenerOldIndication.proxy(IRadioModemIndication.class));
    listenerRebindModem.setRadioPower(707,true,false,false);
    listenerOldIndication.radioStates.clear();
    listenerRebindModel.addDeliveryListener(() -> {
      try { listenerRebindModem.setResponseFunctions((IRadioModemResponse)listenerNewResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)listenerNewIndication.proxy(IRadioModemIndication.class)); }
      catch (RemoteException error) { throw new RuntimeException(error); }
    });
    listenerRebindModem.setRadioPower(708,false,false,false);
    listenerOldResponse.expect("setRadioPowerResponse",708,RadioError.NONE);
    check(listenerOldIndication.radioStates.isEmpty(),"retired callback received power indication after listener rebind");
    check(listenerNewIndication.radioStates.equals(Collections.singletonList(RadioState.OFF)),"listener rebind did not replay OFF to new callback");
    SyntheticSingleSlot errorModel=SyntheticSingleSlot.forTests();
    RadioModemService errorModem=new RadioModemService("slot1",errorModel);
    Calls errorResponse=new Calls(),errorIndication=new Calls();
    errorModem.setResponseFunctions((IRadioModemResponse)errorResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)errorIndication.proxy(IRadioModemIndication.class));
    errorIndication.radioStates.clear();
    errorResponse.onPowerResponse=() -> { throw new IllegalStateException("test response failure"); };
    try { errorModem.setRadioPower(709,true,false,false); throw new AssertionError("power response error swallowed"); }
    catch (IllegalStateException expected) { check("test response failure".equals(expected.getMessage()),"wrong response error"); }
    check(errorIndication.radioStates.isEmpty() && errorModel.radioState()==RadioState.ON,"response failure changed model or sent another indication");
    SyntheticSingleSlot failedRebindModel=SyntheticSingleSlot.forTests();
    RadioModemService failedRebindModem=new RadioModemService("slot1",failedRebindModel);
    Calls failedOldResponse=new Calls(),failedOldIndication=new Calls();
    Calls failedNewResponse=new Calls(),failedNewIndication=new Calls();
    AtomicReference<Throwable> failedRebindError=new AtomicReference<>();
    failedRebindModem.setResponseFunctions((IRadioModemResponse)failedOldResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)failedOldIndication.proxy(IRadioModemIndication.class));
    failedOldIndication.radioStates.clear();
    failedNewIndication.onRilConnected=() -> { throw new IllegalStateException("test initialization failure"); };
    failedOldResponse.onPowerResponse=() -> {
      try { failedRebindModem.setResponseFunctions((IRadioModemResponse)failedNewResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)failedNewIndication.proxy(IRadioModemIndication.class)); }
      catch (RemoteException | RuntimeException error) { failedRebindError.set(error); }
    };
    failedRebindModem.setRadioPower(710,true,false,false);
    check(failedRebindError.get() instanceof IllegalStateException && failedOldIndication.radioStates.isEmpty() && failedNewIndication.radioStates.isEmpty(),"failed rebind was hidden or notified retired peer");
    try { failedRebindModem.getModemStackStatus(711); throw new AssertionError("failed rebind left callbacks available"); }
    catch (RemoteException expected) { }
    RadioModemService defaultModem=new RadioModemService("slot1");
    RadioSimService defaultSim=new RadioSimService("slot1");
    Calls dm=new Calls(),di=new Calls(),ds=new Calls(),dsi=new Calls();
    defaultModem.setResponseFunctions((IRadioModemResponse)dm.proxy(IRadioModemResponse.class),(IRadioModemIndication)di.proxy(IRadioModemIndication.class));
    defaultSim.setResponseFunctions((IRadioSimResponse)ds.proxy(IRadioSimResponse.class),(IRadioSimIndication)dsi.proxy(IRadioSimIndication.class));
    defaultModem.getModemStackStatus(505); dm.expect("getModemStackStatusResponse",505,RadioError.REQUEST_NOT_SUPPORTED);
    defaultModem.setRadioPower(506,true,false,false); dm.expect("setRadioPowerResponse",506,RadioError.REQUEST_NOT_SUPPORTED);
    check(di.name==null,"default modem emitted a synthetic indication");
    defaultSim.getIccCardStatus(507); ds.expect("getIccCardStatusResponse",507,RadioError.REQUEST_NOT_SUPPORTED);
    modem.setRadioPower(511,true,false,false); mr.expect("setRadioPowerResponse",511,RadioError.NONE);
    IBinder.DeathRecipient oldDeath=mr.peer.deaths.get(0);
    Calls next=new Calls(),nextInd=new Calls();
    modem.setResponseFunctions((IRadioModemResponse)next.proxy(IRadioModemResponse.class),(IRadioModemIndication)nextInd.proxy(IRadioModemIndication.class));
    check(nextInd.received.equals(Arrays.asList("rilConnected", "radioStateChanged")),"rebind did not replay modem notifications");
    check(((Integer)nextInd.args[1])==RadioState.ON,"rebind did not replay current power state");
    oldDeath.binderDied(); modem.getModemStackStatus(508); next.expect("getModemStackStatusResponse",508,RadioError.NONE);
    next.die();
    try { modem.getModemStackStatus(509); throw new AssertionError("dead callback accepted"); } catch (RemoteException expected) {}
    check(nextInd.peer.deaths.isEmpty(),"other callback death recipient leaked");
    sr.die();
    try { sim.getIccCardStatus(510); throw new AssertionError("dead SIM callback accepted"); } catch (RemoteException expected) {}
    check(si.peer.deaths.isEmpty(),"SIM indication death recipient leaked");
    Calls beforeDisconnectResponse=new Calls(), beforeDisconnectIndication=new Calls();
    modem.setResponseFunctions((IRadioModemResponse)beforeDisconnectResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)beforeDisconnectIndication.proxy(IRadioModemIndication.class));
    beforeDisconnectIndication.received.clear();
    model.disconnect();
    check(beforeDisconnectIndication.received.equals(Collections.singletonList("radioStateChanged")),"disconnect did not notify the bound modem peer");
    check(((Integer)beforeDisconnectIndication.args[1])==RadioState.UNAVAILABLE,"disconnect notification was not UNAVAILABLE");
    model.disconnect();
    check(beforeDisconnectIndication.received.size()==1,"duplicate disconnect emitted another notification");
    Calls disconnectedSimResponse=new Calls(), disconnectedSimIndication=new Calls();
    sim.setResponseFunctions((IRadioSimResponse)disconnectedSimResponse.proxy(IRadioSimResponse.class),(IRadioSimIndication)disconnectedSimIndication.proxy(IRadioSimIndication.class));
    sim.getIccCardStatus(514); disconnectedSimResponse.expect("getIccCardStatusResponse",514,RadioError.RADIO_NOT_AVAILABLE);
    Calls disconnectedResponse=new Calls(),disconnectedIndication=new Calls();
    modem.setResponseFunctions((IRadioModemResponse)disconnectedResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)disconnectedIndication.proxy(IRadioModemIndication.class));
    check(((Integer)disconnectedIndication.args[1])==RadioState.UNAVAILABLE,"rebound modem did not replay disconnected state");
    modem.getModemStackStatus(512); disconnectedResponse.expect("getModemStackStatusResponse",512,RadioError.RADIO_NOT_AVAILABLE);
    modem.setRadioPower(513,true,false,false); disconnectedResponse.expect("setRadioPowerResponse",513,RadioError.RADIO_NOT_AVAILABLE);
    RadioModemService dyingModem=new RadioModemService("slot1",SyntheticSingleSlot.forTests());
    Calls dyingResponse=new Calls(), dyingIndication=new Calls();
    dyingIndication.onRilConnected=dyingIndication::die;
    try {
      dyingModem.setResponseFunctions((IRadioModemResponse)dyingResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)dyingIndication.proxy(IRadioModemIndication.class));
      throw new AssertionError("callback death during initialization accepted");
    } catch (RemoteException expected) {}
    check(dyingIndication.received.equals(Collections.singletonList("rilConnected")),"sent state after callback death");
    check(dyingResponse.peer.deaths.isEmpty(),"initialization failure leaked death recipient");
    SyntheticSingleSlot initDisconnectModel=SyntheticSingleSlot.forTests();
    RadioModemService initDisconnectModem=new RadioModemService("slot1",initDisconnectModel);
    Calls initDisconnectResponse=new Calls(),initDisconnectIndication=new Calls();
    initDisconnectIndication.onRilConnected=() -> {
      try { initDisconnectModel.disconnect(); } catch (RemoteException e) { throw new RuntimeException(e); }
    };
    initDisconnectModem.setResponseFunctions((IRadioModemResponse)initDisconnectResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)initDisconnectIndication.proxy(IRadioModemIndication.class));
    check(!initDisconnectIndication.radioStates.isEmpty() &&
        initDisconnectIndication.radioStates.stream().allMatch(state -> state==RadioState.UNAVAILABLE),
        "initialization sent stale state after reentrant disconnect: "+initDisconnectIndication.radioStates);
    SyntheticSingleSlot initPowerModel=SyntheticSingleSlot.forTests();
    RadioModemService initPowerModem=new RadioModemService("slot1",initPowerModel);
    Calls initPowerResponse=new Calls(),initPowerIndication=new Calls();
    initPowerIndication.onRilConnected=() -> {
      try { initPowerModem.setRadioPower(701,true,false,false); } catch (RemoteException e) { throw new RuntimeException(e); }
    };
    initPowerModem.setResponseFunctions((IRadioModemResponse)initPowerResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)initPowerIndication.proxy(IRadioModemIndication.class));
    initPowerResponse.expect("setRadioPowerResponse",701,RadioError.NONE);
    check(!initPowerIndication.radioStates.isEmpty() &&
        initPowerIndication.radioStates.stream().allMatch(state -> state==RadioState.ON),
        "initialization sent stale state after reentrant power-on: "+initPowerIndication.radioStates);
    SyntheticSingleSlot concurrentModel=SyntheticSingleSlot.forTests();
    RadioModemService concurrentModem=new RadioModemService("slot1",concurrentModel);
    Calls concurrentResponse=new Calls(),concurrentIndication=new Calls();
    CountDownLatch stateDisconnected=new CountDownLatch(1);
    AtomicReference<Throwable> concurrentFailure=new AtomicReference<>();
    concurrentModel.addDeliveryListener(stateDisconnected::countDown);
    Thread disconnectThread=new Thread(() -> {
      try { concurrentModel.disconnect(); } catch (Throwable error) { concurrentFailure.set(error); }
    });
    concurrentIndication.onRilConnected=() -> {
      disconnectThread.start();
      try { check(stateDisconnected.await(2,TimeUnit.SECONDS),"concurrent disconnect did not change state"); }
      catch (InterruptedException error) { Thread.currentThread().interrupt(); throw new AssertionError(error); }
    };
    try {
      concurrentModem.setResponseFunctions((IRadioModemResponse)concurrentResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)concurrentIndication.proxy(IRadioModemIndication.class));
    } finally { disconnectThread.join(3000); }
    check(!disconnectThread.isAlive() && concurrentFailure.get()==null,"concurrent disconnect did not finish");
    check(!concurrentIndication.radioStates.isEmpty() &&
        concurrentIndication.radioStates.stream().allMatch(state -> state==RadioState.UNAVAILABLE),
        "initialization sent stale state during concurrent disconnect: "+concurrentIndication.radioStates);
    SyntheticSingleSlot detachedModel=SyntheticSingleSlot.forTests();
    RadioModemService detachedModem=new RadioModemService("slot1",detachedModel);
    Calls detachedResponse=new Calls(), detachedIndication=new Calls();
    detachedModem.setResponseFunctions((IRadioModemResponse)detachedResponse.proxy(IRadioModemResponse.class),(IRadioModemIndication)detachedIndication.proxy(IRadioModemIndication.class));
    detachedIndication.received.clear(); detachedModem.detachStateListener(); detachedModel.disconnect();
    check(detachedIndication.received.isEmpty(),"detached listener still received a new disconnect");
    System.out.println("RADIO_MODEM_SIM_CONTRACT_OK modem="+mc+" sim="+sc);
  }
}
