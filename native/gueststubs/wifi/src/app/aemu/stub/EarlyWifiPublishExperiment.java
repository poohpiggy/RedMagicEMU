/* RedMagicEMU 2026-11-11: experimental two-stage Wi-Fi service startup. */
package app.aemu.stub;
import android.content.Context;
import android.os.IBinder;
import android.os.Looper;
import android.os.Handler;
import android.os.Parcel;
import android.os.SystemClock;
import java.lang.reflect.Method;
import java.util.concurrent.Callable;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/** Publishes the prepared service early, then hands Ethernet over to Wi-Fi after boot. */
public final class EarlyWifiPublishExperiment {
 private static void log(String s){System.out.println("aemu-wifi-early: "+s);System.out.flush();}
 private static <T>T bounded(Callable<T> call)throws Exception {
  FutureTask<T> future=new FutureTask<T>(call);Thread t=new Thread(future,"wifi-early-bounded");t.setDaemon(true);t.start();
  return future.get(3,TimeUnit.SECONDS);
 }
 static final class Registry {
  private final Object manager;private final Method check,add;
  Registry()throws Exception{
   Class<?> sm=Class.forName("android.os.ServiceManager"),iface=Class.forName("android.os.IServiceManager");
   Method get=sm.getDeclaredMethod("getIServiceManager");get.setAccessible(true);manager=get.invoke(null);
   if(manager==null)throw new IllegalStateException("IServiceManager unavailable");
   check=iface.getMethod("checkService",String.class);add=sm.getMethod("addService",String.class,IBinder.class);
  }
  IBinder current()throws Exception{return bounded(()->(IBinder)check.invoke(manager,"wifi"));}
  void publish(IBinder service)throws Exception{bounded(()->{add.invoke(null,"wifi",service);return null;});}
 }
 private static boolean isVirtual(IBinder binder)throws Exception{
  if(binder==null)return false;
  return bounded(()->{Parcel data=Parcel.obtain(),reply=Parcel.obtain();try{
   return binder.transact(WifiService.PROBE,data,reply,0) && reply.readInt()==WifiService.PROBE_ANSWER;
  }finally{data.recycle();reply.recycle();}});
 }
 private static IBinder awaitCandidate(Registry registry)throws Exception {
  long end=SystemClock.elapsedRealtime()+45000;
  do{IBinder b=registry.current();if(b!=null){
   String descriptor=bounded(()->b.getInterfaceDescriptor());
   if(!WifiService.DESCRIPTOR.equals(descriptor))throw new IllegalStateException("unexpected Wi-Fi descriptor");
   return b;
  }Thread.sleep(100);}while(SystemClock.elapsedRealtime()<end);
  throw new IllegalStateException("Wi-Fi candidate timeout");
 }
 public static void main(String[] args)throws Exception {
  WifiStub.requireServiceReady();
  Thread.setDefaultUncaughtExceptionHandler((thread,failure)->{log("uncaught_"+thread.getName()+"_"+failure.getClass().getName());failure.printStackTrace(System.out);System.out.flush();});
  Runtime.getRuntime().addShutdownHook(new Thread(()->log("process_shutdown_best_effort"),"wifi-early-shutdown"));
  log("experimental_two_phase_start_ethernet_remains_until_authenticated_handoff");
  Looper.prepare();Context context=WifiSystemContext.obtain();WifiConfig cfg=WifiConfig.load();
  if(cfg==null)throw new IllegalStateException("Wi-Fi configuration unavailable");
  WifiPersistentState state=new WifiPersistentState(new java.io.File("/data/system/redmagic-wifi-state"),cfg.ssid,cfg.bssid);
  final WifiService service=new WifiService(cfg,Looper.myLooper(),context,state,true);
  service.startAirplaneModeObserver();
  byte[] sessionKey=new byte[32];new java.security.SecureRandom().nextBytes(sessionKey);
  final EarlyWifiProofBinder session=new EarlyWifiProofBinder(sessionKey);
  java.util.Arrays.fill(sessionKey,(byte)0);service.setEarlyProof(session);
  log("preparing_enabled_"+service.isEnabled()+"_connected_"+service.isConnected());
  final Looper serviceLooper=Looper.myLooper();
  final AtomicBoolean attempted=new AtomicBoolean();
  Thread startup=new Thread(()->{
   try{
    final Registry registry=new Registry();
    final IBinder original=awaitCandidate(registry);log("wifi_candidate_found_descriptor_matched_origin_not_proven");
    boolean accepted=EarlyWifiPublication.publish(new EarlyWifiPublication.Backend(){
     public boolean preparing(){return true;}public boolean connected(){return service.isConnected();}
     public Object original(){return original;}public boolean alive(Object binder){return ((IBinder)binder).isBinderAlive();}
     public Object current()throws Exception{return registry.current();}public boolean isOurService(Object binder)throws Exception{return isVirtual((IBinder)binder);}
     public void publish()throws Exception{attempted.set(true);registry.publish(service);}
     public boolean confirm()throws Exception{return isVirtual(registry.current());}public void log(String message){EarlyWifiPublishExperiment.log(message);}
    });
    if(!accepted && !attempted.get())throw new IllegalStateException("early Wi-Fi publication did not run; Ethernet retained");
    if(!accepted){log("publication_unknown_keep_binder_alive_ethernet_retained");return;}
    log("initial_broadcast_deferred_until_boot");
    try{
     log("waiting_for_boot_then_network_handoff");
     EarlyWifiHandoff.run(context,serviceLooper,cfg,service,session);
     log("handoff_wifi_network_confirmed");
    }catch(WifiEthernetHandoff.FailedAndRestored failure){log("handoff_failed_ethernet_restored");return;}
    catch(Exception failure){log("handoff_failed_or_unknown_"+failure.getClass().getName());failure.printStackTrace(System.out);System.out.flush();return;}
    try{for(;;){Thread.sleep(1000);if(!isVirtual(registry.current())){log("wifi_current_owner_unconfirmed");return;}}}
    catch(Exception ex){log("wifi_owner_monitor_unknown_"+ex.getClass().getName());}
   }catch(Exception failure){
    log("startup_failed_"+failure.getClass().getName()+"_ethernet_retained");failure.printStackTrace(System.out);System.out.flush();
    if(!attempted.get())System.exit(2);else log("publication_unknown_keep_binder_alive_ethernet_retained");
   }
  },"wifi-early-publish");startup.setDaemon(true);
  if(!new Handler(serviceLooper).post(startup::start))throw new IllegalStateException("service looper unavailable");
  Looper.loop();
  log("service_looper_returned_unexpectedly");
 }
}
