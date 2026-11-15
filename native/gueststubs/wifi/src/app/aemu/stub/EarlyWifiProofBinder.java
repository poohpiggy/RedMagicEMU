package app.aemu.stub;
import android.os.Binder;import android.os.Parcel;import android.os.RemoteException;
import javax.crypto.Mac;import javax.crypto.spec.SecretKeySpec;
/** Per-process proof session for the isolated early Wi-Fi handoff experiment. */
final class EarlyWifiProofBinder extends Binder {
 static final int PROOF=0x41454d56;
 private final byte[] key;
 EarlyWifiProofBinder(byte[] key){if(key==null||key.length!=32)throw new IllegalArgumentException("invalid key");this.key=key.clone();}
 boolean answer(Parcel data,Parcel reply)throws RemoteException {
  if(Binder.getCallingUid()!=0)throw new SecurityException("guest root required");
  if(data==null || reply==null)return false;byte[] challenge=data.createByteArray();data.enforceNoDataAvail();
  if(challenge==null || challenge.length!=32)throw new IllegalArgumentException("invalid challenge");
  try{Mac mac=Mac.getInstance("HmacSHA256");mac.init(new SecretKeySpec(key,"HmacSHA256"));byte[] answer=mac.doFinal(challenge);reply.writeNoException();reply.writeByteArray(answer);return true;}
  catch(java.security.GeneralSecurityException failure){throw new IllegalStateException("proof unavailable",failure);}
 }
 @Override protected boolean onTransact(int code,Parcel data,Parcel reply,int flags)throws RemoteException {
  if(code==PROOF)return answer(data,reply);return super.onTransact(code,data,reply,flags);
 }
}
