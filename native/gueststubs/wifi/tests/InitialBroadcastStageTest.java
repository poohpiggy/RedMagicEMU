package app.aemu.stub;
import android.content.*;
public final class InitialBroadcastStageTest {
 static class FailingContext extends Context {int count,fail;FailingContext(int fail){this.fail=fail;}@Override public void sendBroadcast(Intent i){if(++count==fail)throw new IllegalStateException("injected");super.sendBroadcast(i);}}
 public static void main(String[] args)throws Exception{
  String[] expected={"wifi_state","supplicant","network_state"};
  for(int i=1;i<=3;i++){
   try{WifiStateBroadcasts.publishInitial(new FailingContext(i),true,false);throw new AssertionError("failure swallowed");}
   catch(IllegalStateException e){String stage;try{java.lang.reflect.Field f=e.getClass().getDeclaredField("stage");f.setAccessible(true);stage=(String)f.get(e);}catch(ReflectiveOperationException ex){throw new AssertionError("failed broadcast stage not identified");}
    if(!expected[i-1].equals(stage))throw new AssertionError("wrong failure phase "+stage);
   }
  }
  System.out.println("PASS initial broadcast failure phase");
 }
}
