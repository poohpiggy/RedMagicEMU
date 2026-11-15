"""Exercise the actual client's negative-result handling without Android or a device."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent
STUBS = {
    "android/content/AttributionSource.java": """package android.content;
public class AttributionSource { public static class Builder {
 public Builder(int uid) {} public Builder setPackageName(String n) { return this; }
 public Builder setNext(AttributionSource n) { return this; }
 public AttributionSource build() { return new AttributionSource(); } } }""",
    "android/net/wifi/WifiConfiguration.java": """package android.net.wifi;
public class WifiConfiguration { public int networkId; public String SSID, preSharedKey;
 public java.util.BitSet allowedKeyManagement = new java.util.BitSet();
 public static class KeyMgmt { public static final int NONE=0; } }""",
    "android/net/wifi/IWifiManager.java": """package android.net.wifi;
import android.os.Bundle; public interface IWifiManager {
 Object getPrivilegedConfiguredNetworks(String p, String t, Bundle b);
 class Stub { public static int TRANSACTION_getPrivilegedConfiguredNetworks=1; } }""",
    "android/os/BadParcelableException.java": "package android.os; public class BadParcelableException extends RuntimeException { public BadParcelableException(String s){super(s);} }",
    "android/os/Parcelable.java": "package android.os; public interface Parcelable { interface Creator<T> { T createFromParcel(Parcel p); } }",
    "android/os/Bundle.java": "package android.os; public class Bundle { public void putParcelable(String k, Object v) {} }",
    "android/os/IBinder.java": "package android.os; public interface IBinder { int FLAG_ONEWAY=1; boolean transact(int c, Parcel d, Parcel r, int f); boolean isBinderAlive(); }",
    "android/os/Binder.java": "package android.os; public class Binder implements IBinder { public boolean transact(int c, Parcel d, Parcel r, int f){return false;} public boolean isBinderAlive(){return true;} }",
    "android/os/Parcel.java": """package android.os; public class Parcel {
 public static Parcel obtain(){return new Parcel();} public void writeInterfaceToken(String s){}
 public void writeString(String s){} public void writeTypedObject(Object o,int f){}
 public void writeInt(int i){} public void writeStrongBinder(IBinder b){}
 public void readException(){} public int readInt(){return 0;} public void recycle(){} }""",
    "android/os/Process.java": "package android.os; public class Process { public static int myUid(){return 1000;} public static int myPid(){return 1;} }",
    "android/os/SystemClock.java": "package android.os; public class SystemClock { public static long elapsedRealtime(){return 0;} }",
    "NegativeOutcomeTest.java": """import java.lang.reflect.*;
import android.os.*;
public class NegativeOutcomeTest {
 static class Endpoint implements IBinder {
   final RuntimeException error; Endpoint(RuntimeException e){error=e;}
   public boolean isBinderAlive(){return true;}
   public boolean transact(int code, Parcel data, Parcel reply, int flags){if(error!=null) throw error; return true;}
 }
 static void run(RuntimeException error, boolean shouldPass, Class<?> expected, String reason) throws Exception {
   Class<?> c=Class.forName("app.aemu.stub.WifiPrivilegedConfigClient"); Method denied=null;
   for(Method m:c.getDeclaredMethods()) if(m.getName().equals("denied")) denied=m;
   denied.setAccessible(true);
   Method query=android.net.wifi.IWifiManager.class.getMethod("getPrivilegedConfiguredNetworks", String.class, String.class, Bundle.class);
   Object[] args=denied.getParameterCount()==6
      ? new Object[]{new Endpoint(error),query,"package",null,false,"case"}
      : new Object[]{new Endpoint(error),query,"package",null,false,"case",expected,reason};
   boolean passed=true;
   try { denied.invoke(null,args); } catch(InvocationTargetException ex) { passed=false; }
   if(passed!=shouldPass) throw new AssertionError("wrong outcome for " + error + ": " + passed);
 }
 public static void main(String[] args) throws Exception {
   run(new SecurityException("invalid privileged configuration attribution"),true,SecurityException.class,"invalid privileged configuration attribution");
   run(new IllegalArgumentException("broken parcel"),false,SecurityException.class,"invalid privileged configuration attribution");
   run(new SecurityException("AttributionSource should be unparceled during a binder transaction"),false,SecurityException.class,"invalid privileged configuration attribution");
   run(null,false,SecurityException.class,"invalid privileged configuration attribution");
   run(new BadParcelableException("Parcel data not fully consumed, unread size: 4"),true,BadParcelableException.class,"Parcel data not fully consumed");
   run(new BadParcelableException("other parcel error"),false,BadParcelableException.class,"Parcel data not fully consumed");
   System.out.println("negative outcome checks passed");
 } }""",
}

with tempfile.TemporaryDirectory(prefix="rme-wifi-negative-") as tmp:
    work = pathlib.Path(tmp)
    sources = []
    for name, contents in STUBS.items():
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")
        sources.append(str(path))
    subprocess.run(["javac", "-d", str(work), *sources,
                    str(ROOT / "WifiPrivilegedConfigClient.java")], check=True)
    subprocess.run(["java", "-cp", str(work), "NegativeOutcomeTest"], check=True)
