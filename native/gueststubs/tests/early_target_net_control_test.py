from pathlib import Path
import importlib.util, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('base_control_test', ROOT/'tests/target_net_control_test.py')
base = importlib.util.module_from_spec(spec); spec.loader.exec_module(base)
sources = dict(base.SOURCES)
sources['android/os/IServiceManager.java'] = 'package android.os; public interface IServiceManager { IBinder checkService(String s); }'
sources['android/os/ServiceManager.java'] = '''package android.os; public class ServiceManager {
 public static IBinder current, wifi, realWifi;
 private static IServiceManager getIServiceManager() { return new IServiceManager(){public IBinder checkService(String s){return realWifi;}}; }
 public static IBinder checkService(String s){return s.equals("wifi")?wifi:current;}
 public static void addService(String s,IBinder b){if(s.equals("wifi"))wifi=b;else current=b;}
}'''
sources['app/aemu/stub/ControlTest.java'] = '''package app.aemu.stub;
import android.net.Network;import android.os.*;import java.util.*;import javax.crypto.Mac;import javax.crypto.spec.SecretKeySpec;
public final class ControlTest {
 static final int EARLY=IBinder.FIRST_CALL_TRANSACTION+3, PROOF=0x41454d56;
 static class Lease implements TargetNetControl.Lease {volatile int stops,restores;volatile boolean available=true,registered=true,failStop=false,failRegistration=false,unregisterBeforeThrow=false,failRestore=false;final Network n=new Network();
  public Network network(){return n;}public boolean networkRegistered(){if(failRegistration)throw new IllegalStateException("registration failed");return registered;}public void stop(){stops++;if(failStop){if(unregisterBeforeThrow)registered=false;throw new IllegalStateException("stop unknown");}}public boolean canRestore(){return available;}public void restore(){restores++;if(failRestore)throw new IllegalStateException("restore unknown");registered=true;}}
 static class Proof extends Binder {byte[] key;boolean invalid,slow,dieOnLink; Proof(byte v){key=new byte[32];Arrays.fill(key,v);}
  protected boolean onTransact(int code,Parcel data,Parcel reply,int flags)throws RemoteException {
   if(code!=PROOF)return false; if(slow)try{Thread.sleep(6000);}catch(InterruptedException e){};
   try { byte[] n=data.createByteArray(); Mac m=Mac.getInstance("HmacSHA256");m.init(new SecretKeySpec(key,"HmacSHA256"));byte[] a=m.doFinal(n);if(invalid)a[0]^=1;reply.writeNoException();reply.writeByteArray(a);return true; }catch(Exception e){throw new RuntimeException(e);}}
  public void linkToDeath(DeathRecipient r,int flags){super.linkToDeath(r,flags);if(dieOnLink){alive=false;r.binderDied();}}
  void kill(){alive=false;die();}
 }
 static Parcel request(IBinder session){Parcel d=new Parcel();d.token=TargetNetControl.DESCRIPTOR;d.owner=session;return d;}
 static void reject(TargetNetControl c,Parcel d,Lease lease)throws Exception {try{c.onTransact(EARLY,d,new Parcel(),0);throw new AssertionError("unverified STOP_EARLY accepted");}catch(IllegalStateException | SecurityException expected){} if(lease.stops!=0)throw new AssertionError("Ethernet stopped despite failed proof");}
 static void expect(boolean x,String s){if(!x)throw new AssertionError(s);}
 public static void main(String[] args)throws Exception {
  Binder.uid=0;Proof session=new Proof((byte)7),replacement=new Proof((byte)7);ServiceManager.realWifi=replacement;ServiceManager.wifi=new Proof((byte)9);
  Lease ok=new Lease();TargetNetControl c=new TargetNetControl(ok);Parcel data=request(session),reply=new Parcel();
  expect(c.onTransact(EARLY,data,reply,0),"STOP_EARLY not implemented");expect(reply.result==1 && ok.stops==1,"valid live early handoff failed");
  ok.available=false;session.kill();Thread.sleep(100);expect(ok.restores==0,"restored on occupied interface");ok.available=true;
  for(int n=0;n<100&&ok.restores==0;n++)Thread.sleep(10);expect(ok.restores==1,"death did not restore ethernet");
  c.onTransact(TargetNetControl.STATUS,data,reply,0);expect(reply.result==0,"confirmed Ethernet restoration not active");expect(ServiceManager.realWifi==replacement,"unexpected wifi registry mutation");
  Lease explicit=new Lease();TargetNetControl explicitControl=new TargetNetControl(explicit);Proof explicitSession=new Proof((byte)7);ServiceManager.realWifi=replacement;Parcel explicitData=request(explicitSession);
  explicitControl.onTransact(EARLY,explicitData,reply,0);explicitControl.onTransact(TargetNetControl.RESTORE,explicitData,reply,0);
  expect(explicit.restores==1 && reply.result==0,"explicit early Ethernet restore failed");expect(ServiceManager.realWifi==replacement,"explicit restore modified Wi-Fi");
  Lease failedStop=new Lease();failedStop.failStop=true;TargetNetControl failedStopControl=new TargetNetControl(failedStop);Proof failedStopSession=new Proof((byte)7);ServiceManager.realWifi=replacement;
  try{failedStopControl.onTransact(EARLY,request(failedStopSession),reply,0);throw new AssertionError("failed stop accepted");}catch(IllegalStateException expected){}
  failedStopControl.onTransact(TargetNetControl.STATUS,request(failedStopSession),reply,0);expect(reply.result==2 && failedStop.stops==1 && failedStop.restores==0,"failed stop is not unknown");
  Thread.sleep(250);expect(failedStop.restores==0,"old Ethernet present but duplicated after failed stop");
  failedStopSession.kill();Thread.sleep(200);expect(failedStop.restores==0,"session death duplicated old Ethernet");
  failedStop.available=false;failedStop.registered=false;Thread.sleep(200);expect(failedStop.restores==0,"restored while Wi-Fi or eth0 still busy");
  failedStop.available=true;for(int n=0;n<200&&failedStop.restores==0;n++)Thread.sleep(10);
  expect(failedStop.restores==1,"late unregister after failed stop was not recovered");
  failedStopControl.onTransact(TargetNetControl.STATUS,request(failedStopSession),reply,0);expect(reply.result==0,"confirmed late recovery not active");
  Lease aliveLate=new Lease();aliveLate.failStop=true;Proof aliveLateSession=new Proof((byte)7);TargetNetControl aliveLateControl=new TargetNetControl(aliveLate);ServiceManager.realWifi=replacement;
  try{aliveLateControl.onTransact(EARLY,request(aliveLateSession),reply,0);throw new AssertionError("failed stop accepted");}catch(IllegalStateException expected){}
  Thread.sleep(250);expect(aliveLate.restores==0 && aliveLateSession.isBinderAlive(),"live session or original Ethernet not preserved");
  aliveLate.registered=false;for(int n=0;n<200&&aliveLate.restores==0;n++)Thread.sleep(10);
  expect(aliveLate.restores==1 && aliveLateSession.isBinderAlive(),"late unregister was not monitored while session alive");
  Lease lostImmediately=new Lease();lostImmediately.failStop=true;lostImmediately.unregisterBeforeThrow=true;Proof lostSession=new Proof((byte)7);TargetNetControl lostControl=new TargetNetControl(lostImmediately);ServiceManager.realWifi=replacement;
  try{lostControl.onTransact(EARLY,request(lostSession),reply,0);throw new AssertionError("failed stop accepted");}catch(IllegalStateException expected){}
  for(int n=0;n<200&&lostImmediately.restores==0;n++)Thread.sleep(10);expect(lostImmediately.restores==1,"disappeared old Ethernet not restored");
  Lease badRecovery=new Lease();badRecovery.failStop=true;badRecovery.unregisterBeforeThrow=true;badRecovery.failRestore=true;Proof badRecoverySession=new Proof((byte)7);TargetNetControl badRecoveryControl=new TargetNetControl(badRecovery);ServiceManager.realWifi=replacement;
  try{badRecoveryControl.onTransact(EARLY,request(badRecoverySession),reply,0);throw new AssertionError("failed stop accepted");}catch(IllegalStateException expected){}
  for(int n=0;n<200&&badRecovery.restores==0;n++)Thread.sleep(10);expect(badRecovery.restores==1,"uncertain restore not attempted");
  Thread.sleep(100);badRecoverySession.kill();Thread.sleep(300);expect(badRecovery.restores==1,"unknown restore retried after session death");
  Lease unknownStatus=new Lease();unknownStatus.failStop=true;unknownStatus.failRegistration=true;Proof unknownSession=new Proof((byte)7);TargetNetControl unknownControl=new TargetNetControl(unknownStatus);ServiceManager.realWifi=replacement;
  try{unknownControl.onTransact(EARLY,request(unknownSession),reply,0);throw new AssertionError("failed stop accepted");}catch(IllegalStateException expected){}
  unknownSession.kill();Thread.sleep(200);expect(unknownStatus.restores==0,"unknown old network state triggered restore");
  Lease badRegistration=new Lease();badRegistration.failRegistration=true;TargetNetControl badRegistrationControl=new TargetNetControl(badRegistration);Proof badRegistrationSession=new Proof((byte)7);ServiceManager.realWifi=replacement;
  badRegistrationControl.onTransact(EARLY,request(badRegistrationSession),reply,0);try{badRegistrationControl.onTransact(TargetNetControl.RESTORE,request(badRegistrationSession),reply,0);throw new AssertionError("unverified registration accepted");}catch(IllegalStateException expected){}
  badRegistrationControl.onTransact(TargetNetControl.STATUS,request(badRegistrationSession),reply,0);expect(reply.result==2 && badRegistration.restores==1,"unverified restore not unknown");
  Lease wrong=new Lease();TargetNetControl a=new TargetNetControl(wrong);ServiceManager.realWifi=new Proof((byte)8);reject(a,request(new Proof((byte)7)),wrong);
  Lease missing=new Lease();ServiceManager.realWifi=null;reject(new TargetNetControl(missing),request(new Proof((byte)7)),missing);
  Lease dead=new Lease();Proof d=new Proof((byte)7);d.alive=false;ServiceManager.realWifi=replacement;reject(new TargetNetControl(dead),request(d),dead);
  Lease race=new Lease();Proof dying=new Proof((byte)7);dying.dieOnLink=true;reject(new TargetNetControl(race),request(dying),race);
  Lease permission=new Lease();Binder.uid=12345;reject(new TargetNetControl(permission),request(new Proof((byte)7)),permission);Binder.uid=0;
  Lease timeout=new Lease();Proof hung=new Proof((byte)7);hung.slow=true;ServiceManager.realWifi=hung;long begin=System.nanoTime();reject(new TargetNetControl(timeout),request(new Proof((byte)7)),timeout);
  expect((System.nanoTime()-begin)/1000000<4500,"blocking proof did not time out");
  System.out.println("EARLY_NETCONTROL_PROOF_AND_RECOVERY_OK");
 }
}'''
with tempfile.TemporaryDirectory(prefix='rme-early-nettest-') as tmp:
 work=Path(tmp)
 for name,content in sources.items():
  path=work/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(content,encoding='utf-8')
 args=[str(work/name) for name in sources]+[str(ROOT/'src/app/aemu/stub/TargetNetControl.java')]
 subprocess.run(['javac','--release','8','-d',str(work),*args],check=True)
 subprocess.run(['java','-cp',str(work),'app.aemu.stub.ControlTest'],check=True,timeout=15)
