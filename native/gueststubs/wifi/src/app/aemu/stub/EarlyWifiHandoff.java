package app.aemu.stub;
import android.content.Context;import android.net.*;import android.os.*;
import java.net.InetAddress;import java.util.Arrays;import java.util.concurrent.atomic.AtomicReference;
/** Isolated second phase. Never republishes the Wi-Fi Binder or restores the platform Wi-Fi service. */
final class EarlyWifiHandoff {
 private static final int STOP_EARLY=IBinder.FIRST_CALL_TRANSACTION+3;
 private static final String CONTROL="app.redmagicemu.INetworkControl";
 private static final class Status {final int state;final Network network;Status(int s,Network n){state=s;network=n;}}
 private static IBinder lookup(String name)throws Exception{return (IBinder)Class.forName("android.os.ServiceManager").getMethod("checkService",String.class).invoke(null,name);}
 private static Status status(IBinder control)throws Exception{
  Parcel data=Parcel.obtain(),reply=Parcel.obtain();try{data.writeInterfaceToken(CONTROL);
   if(!control.transact(IBinder.FIRST_CALL_TRANSACTION,data,reply,0))throw new IllegalStateException("control status unavailable");
   reply.readException();return new Status(reply.readInt(),(Network)reply.readParcelable(Network.class.getClassLoader()));
  }finally{data.recycle();reply.recycle();}
 }
 private static int command(IBinder control,int code,IBinder session)throws Exception{
  Parcel data=Parcel.obtain(),reply=Parcel.obtain();try{data.writeInterfaceToken(CONTROL);data.writeStrongBinder(session);
   if(!control.transact(code,data,reply,0))throw new IllegalStateException("control command unavailable");reply.readException();return reply.readInt();
  }finally{data.recycle();reply.recycle();}
 }
 private static boolean waitForNetwork(Context context,Network network,boolean wanted)throws Exception {
  ConnectivityManager manager=context.getSystemService(ConnectivityManager.class);if(manager==null)throw new IllegalStateException("connectivity unavailable");
  long end=SystemClock.elapsedRealtime()+15000;
  do{Network[] all=manager.getAllNetworks();if(all==null)throw new IllegalStateException("network list unavailable");boolean found=false;for(Network n:all)if(network.equals(n)){found=true;break;}if(found==wanted)return true;Thread.sleep(100);}while(SystemClock.elapsedRealtime()<end);
  return false;
 }
 static void run(final Context context,final Looper looper,final WifiConfig cfg,final WifiService service,final IBinder session)throws Exception {
  java.lang.reflect.Method property=Class.forName("android.os.SystemProperties").getMethod("get",String.class);
  for(int i=0;i<600 && !"1".equals(property.invoke(null,"sys.boot_completed"));i++)Thread.sleep(500);
  if(!"1".equals(property.invoke(null,"sys.boot_completed")))throw new IllegalStateException("boot incomplete; Ethernet retained");
  service.queueInitialBroadcast();
  LinkProperties link=VirtualWifiLinkProperties.create("eth0",InetAddress.getByName(cfg.ip),cfg.prefix,InetAddress.getByName(cfg.gateway),Arrays.asList(InetAddress.getByName(cfg.dns1),InetAddress.getByName(cfg.dns2)));
  final AtomicReference<Object> info=new AtomicReference<Object>();
  final WifiGuestNetwork guest=new WifiGuestNetwork(context,looper,link,new VirtualWifiState(cfg.ssid,cfg.psk),service.networkTransitionLock(),
   new WifiGuestNetwork.WifiInfoSource(){public Object get(){return info.get();}},
   new WifiGuestNetwork.FailureListener(){public void onFailure(Exception failure){service.onNetworkFailure(failure);}public void onNetworkUnwanted(){service.onNetworkUnwanted();}});
  WifiStub.attachEarlyNetwork(guest);
  final WifiService.Listener listener=new WifiService.Listener(){public synchronized boolean onConnectionChanged(boolean connected,Object wifiInfo){
   info.set(wifiInfo);try{if(connected){guest.setEnabled(true);return guest.connect(cfg.ssid,cfg.psk)==VirtualWifiState.Result.SUCCESS;}guest.disconnect();return true;}
   catch(Exception failure){if(guest.networkStatus()==WifiNetworkLifecycle.Status.UNKNOWN)service.onNetworkFailure(failure);Refl.log("early network transition failed: "+failure.getClass().getName());return false;}finally{info.set(null);}
  }};
  service.setListener(listener);
  final IBinder control=WifiControlWaiter.await(new WifiControlWaiter.Finder(){
   public IBinder find()throws Exception{return lookup("redmagicemu.network.control");}
   public boolean isReady(IBinder candidate)throws Exception{Status s=status(candidate);return WifiControlReadiness.isReady(s.state,s.network,context.getSystemService(ConnectivityManager.class));}
  },WifiControlWaiter.SYSTEM_CLOCK,30000);
  try{new WifiEthernetHandoff(new WifiEthernetHandoff.Backend(){
   public Object oldNetwork()throws Exception{Status s=status(control);if(!WifiControlReadiness.isReady(s.state,s.network,context.getSystemService(ConnectivityManager.class)))throw new IllegalStateException("Ethernet not ready");return s.network;}
   public void stopEthernet()throws Exception{if(command(control,STOP_EARLY,session)!=1)throw new IllegalStateException("early stop not confirmed");}
   public boolean waitGone(Object old)throws Exception{return waitForNetwork(context,(Network)old,false)&&WifiGuestNetwork.waitForAvailableInterface(context,"eth0",15000);}
   public void startWifi()throws Exception{
    boolean success=service.finishPreparation(expected->{try{
     if(expected){Network n=guest.network();return n!=null && waitForNetwork(context,n,true);}
     return guest.networkStatus()==WifiNetworkLifecycle.Status.DISCONNECTED && WifiGuestNetwork.waitForAvailableInterface(context,"eth0",15000);
    }catch(Exception failure){throw new IllegalStateException("network confirmation unavailable",failure);}});
    if(!success)throw new IllegalStateException("prepared Wi-Fi network not confirmed");

   }
   public boolean cleanupWifi()throws Exception{
    if(guest.networkStatus()==WifiNetworkLifecycle.Status.UNKNOWN)return false;
    Network previous=guest.network();guest.disconnect();
    if(previous!=null && !waitForNetwork(context,previous,false))return false;
    return guest.networkStatus()==WifiNetworkLifecycle.Status.DISCONNECTED && WifiGuestNetwork.waitForAvailableInterface(context,"eth0",15000);
   }
   public void restoreEthernet()throws Exception{if(command(control,IBinder.FIRST_CALL_TRANSACTION+2,session)!=0)throw new IllegalStateException("Ethernet restore not confirmed");}
   public boolean waitEthernetRestored()throws Exception{Status s=status(control);return s.state==0 && s.network!=null && waitForNetwork(context,s.network,true);}
  }).run();}catch(WifiEthernetHandoff.Uncertain failure){
   int recovery=EarlyHandoffRecovery.recover(new EarlyHandoffRecovery.Backend(){
    public boolean ethernetActive()throws Exception{Status s=status(control);return s.state==0 && s.network!=null && waitForNetwork(context,s.network,true);}
    public boolean ethernetPresent()throws Exception{
     Status s=status(control);if(s.network==null)throw new IllegalStateException("Ethernet identity unknown");
     ConnectivityManager cm=context.getSystemService(ConnectivityManager.class);if(cm==null)throw new IllegalStateException("connectivity unavailable");
     Network[] all=cm.getAllNetworks();if(all==null)throw new IllegalStateException("network list unavailable");
     for(Network n:all)if(s.network.equals(n)){
      NetworkCapabilities caps=cm.getNetworkCapabilities(n);LinkProperties lp=cm.getLinkProperties(n);
      if(caps==null || lp==null || lp.getInterfaceName()==null)throw new IllegalStateException("Ethernet network details unknown");
      if(!caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) || !"eth0".equals(lp.getInterfaceName()))throw new IllegalStateException("Ethernet network identity changed");
      return true;
     }
     return false;
    }
    public boolean wifiDisconnected(){return guest.networkStatus()==WifiNetworkLifecycle.Status.DISCONNECTED;}
    public boolean ifaceFree()throws Exception{return WifiGuestNetwork.waitForAvailableInterface(context,"eth0",15000);}
    public boolean restoreAndConfirm()throws Exception{if(command(control,IBinder.FIRST_CALL_TRANSACTION+2,session)!=0)return false;Status s=status(control);return s.state==0 && s.network!=null && waitForNetwork(context,s.network,true);}
   });
   if(recovery==EarlyHandoffRecovery.RETAINED)Refl.log("early handoff failed; Ethernet confirmed active");
   else if(recovery==EarlyHandoffRecovery.RESTORED)Refl.log("early handoff failed; Ethernet confirmed restored");
   else if(recovery==EarlyHandoffRecovery.OBSERVED_PRESENT)Refl.log("early handoff failed; old Ethernet currently visible, stop outcome unknown");
   else Refl.log("early handoff failed; Ethernet outcome unknown");
   throw failure;
  }
 }
}
