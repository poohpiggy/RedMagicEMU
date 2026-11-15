from pathlib import Path
import subprocess,tempfile
base=Path(__file__).resolve().parent.parent/'src/app/aemu/stub'
source=(base/'WifiService.java').read_text(encoding='utf8')
sig='void queueInitialBroadcast() {'
if sig in source:
 start=source.index(sig);end=start+len(sig);depth=1
 while depth:
  if source[end]=='{':depth+=1
  elif source[end]=='}':depth-=1
  end+=1
 method=source[start:end]
else:
 method='void queueInitialBroadcast() {}'
fixture=r'''
package app.aemu.stub;
import java.util.*;
class Refl {static final List<String> logs=new ArrayList<>();static void log(String s){logs.add(s);}}
class WifiStateBroadcasts {
 static class InitialBroadcastFailure extends IllegalStateException {final String stage; InitialBroadcastFailure(String s,Throwable t){super(t);stage=s;}}
 static void publishInitial(Object context,boolean enabled,boolean connected){Test t=(Test)context;if(t.fail)throw new InitialBroadcastFailure("wifi_state",new IllegalStateException());t.events.add("initial:"+enabled+":"+connected);}
}
class FakeHandler {boolean accepting=true;final ArrayDeque<Runnable> work=new ArrayDeque<>();boolean post(Runnable r){if(!accepting)return false;work.add(r);return true;}void drain(){while(!work.isEmpty())work.remove().run();}}
public class Test {
 final Object transitionLock=new Object(),context=this;final FakeHandler scanHandler=new FakeHandler();final List<String> events=new ArrayList<>();
 boolean enabled=true,connected=false,networkUnknown=false,fail=false;
 INITIAL_METHOD
 void change(boolean e,boolean c){synchronized(transitionLock){synchronized(this){enabled=e;connected=c;}scanHandler.post(()->events.add("change:"+e+":"+c));}}
 static void check(boolean ok,String m){if(!ok)throw new AssertionError(m);}
 public static void main(String[] x){
  Test off=new Test();off.change(false,false);off.queueInitialBroadcast();off.scanHandler.drain();check(off.events.equals(Arrays.asList("change:false:false","initial:false:false")),"disabled state lost: "+off.events);
  Test down=new Test();down.connected=true;down.change(true,false);down.queueInitialBroadcast();down.scanHandler.drain();check(down.events.equals(Arrays.asList("change:true:false","initial:true:false")),"disconnected state lost: "+down.events);
  Test order=new Test();order.queueInitialBroadcast();order.change(false,false);order.scanHandler.drain();check(order.events.equals(Arrays.asList("initial:true:false","change:false:false")),"stale initial after later transition: "+order.events);
  Test unknown=new Test();unknown.networkUnknown=true;unknown.queueInitialBroadcast();unknown.scanHandler.drain();check(unknown.events.isEmpty(),"unknown state broadcast");
  Test rejected=new Test();rejected.scanHandler.accepting=false;rejected.queueInitialBroadcast();check(Refl.logs.contains("initial_broadcast_enqueue_rejected"),"queue rejection not reported");
  Test failed=new Test();failed.fail=true;failed.queueInitialBroadcast();failed.scanHandler.drain();check(Refl.logs.contains("initial_broadcast_failed_wifi_state_java.lang.IllegalStateException"),"failure stage not reported");
  System.out.println("PASS initial state snapshot and broadcast ordering");
 }
}
'''.replace('INITIAL_METHOD',method)
with tempfile.TemporaryDirectory(prefix='rme-initial-order-') as tmp:
 path=Path(tmp)/'Test.java';path.write_text(fixture,encoding='utf8')
 subprocess.run(['javac','-d',tmp,str(path)],check=True)
 subprocess.run(['java','-cp',tmp,'app.aemu.stub.Test'],check=True)
boot=(base/'EarlyWifiPublishExperiment.java').read_text(encoding='utf8')
handoff=(base/'EarlyWifiHandoff.java').read_text(encoding='utf8')
assert 'WifiStateBroadcasts.publishInitial(context,service.isEnabled(),false)' not in boot, 'preboot initial broadcast still sent'
assert handoff.index('sys.boot_completed') < handoff.index('service.queueInitialBroadcast()'), 'initial broadcast not deferred past boot'
assert 'WifiStateBroadcasts.publishInitial(context,service.isEnabled(),service.isConnected())' not in handoff, 'duplicate initial after handoff'
print('PASS boot readiness and no duplicate initial announcement')
