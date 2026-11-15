package app.aemu.stub;
import android.os.*;import java.util.*;
public final class EarlyWifiProofBinderTest {
 static byte[] call(IBinder b,byte[] challenge)throws Exception{Parcel d=Parcel.obtain(),r=Parcel.obtain();d.writeByteArray(challenge);if(!b.transact(0x41454d56,d,r,0))throw new AssertionError("unsupported");r.readException();return r.createByteArray();}
 public static void main(String[] args)throws Exception {
  byte[] key=new byte[32],other=new byte[32],challenge=new byte[32],next=new byte[32];Arrays.fill(key,(byte)1);Arrays.fill(other,(byte)2);Arrays.fill(challenge,(byte)3);Arrays.fill(next,(byte)4);
  EarlyWifiProofBinder session=new EarlyWifiProofBinder(key),wifi=new EarlyWifiProofBinder(key),imposter=new EarlyWifiProofBinder(other);
  byte[] a=call(session,challenge);if(!Arrays.equals(a,call(wifi,challenge)))throw new AssertionError("same session mismatch");
  if(Arrays.equals(a,call(imposter,challenge)))throw new AssertionError("imposter accepted");if(Arrays.equals(a,call(wifi,next)))throw new AssertionError("replay accepted");
  Binder.callingUid=10000;try{call(wifi,challenge);throw new AssertionError("unprivileged caller accepted");}catch(SecurityException expected){}finally{Binder.callingUid=0;}
  try{call(wifi,new byte[2]);throw new AssertionError("malformed nonce accepted");}catch(IllegalArgumentException expected){}
  System.out.println("PASS session proof Binder");
 }
}
