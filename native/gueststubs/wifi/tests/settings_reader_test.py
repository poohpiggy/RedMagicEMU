"""Exercise satellite-mode reads using a fake Android provider, without a device."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FAKES = {
    "android/os/IBinder.java": "package android.os; public interface IBinder {}",
    "android/os/Binder.java": "package android.os; public class Binder implements IBinder {}",
    "android/os/Bundle.java": """package android.os; import java.util.*;
        public class Bundle { private final Map<String,Object> values=new HashMap<>();
          public void putString(String k,String v) { values.put(k,v); }
          public void putInt(String k,int v) { values.put(k,v); }
          public Object get(String k) { return values.get(k); }
          public String getString(String k) { Object v=values.get(k); return v instanceof String?(String)v:null; }
          public boolean containsKey(String k) { return values.containsKey(k); } }""",
    "android/content/AttributionSource.java": "package android.content; public class AttributionSource {}",
    "android/content/Context.java": """package android.content; public class Context {
        public final AttributionSource source=new AttributionSource();
        public AttributionSource getAttributionSource() { return source; } }""",
    "android/content/IContentProvider.java": """package android.content; import android.os.Bundle;
        public interface IContentProvider { Bundle call(AttributionSource a,String authority,String method,String arg,Bundle extras); }""",
    "android/app/ContentProviderHolder.java": """package android.app; import android.content.IContentProvider;
        public class ContentProviderHolder { public IContentProvider provider; }""",
    "android/app/IActivityManager.java": """package android.app; import android.os.IBinder;
        public interface IActivityManager {
          ContentProviderHolder getContentProviderExternal(String name,int user,IBinder token,String tag);
          void removeContentProviderExternalAsUser(String name,IBinder token,int user); }""",
    "android/app/ActivityManager.java": """package android.app;
        public class ActivityManager { public static IActivityManager service;
          public static IActivityManager getService() { return service; } }""",
    "app/aemu/stub/SettingsReaderTest.java": """package app.aemu.stub;
        import android.app.*; import android.content.*; import android.os.*; import java.lang.reflect.*;
        public class SettingsReaderTest {
          static class Provider implements IContentProvider {
            Bundle result; boolean fail; Context context; String keyExpected="satellite_mode_enabled";
            public Bundle call(AttributionSource source,String authority,String method,String key,Bundle extras) {
              if (source != context.source || !"settings".equals(authority) || !"GET_global".equals(method)
                  || !keyExpected.equals(key)) throw new AssertionError("wrong request");
              if (fail) throw new SecurityException("provider denied"); return result;
            }
          }
          static class Manager implements IActivityManager {
            Provider provider; int acquired, released; boolean failAcquire, failRelease, nullHolder, nullProvider; IBinder token;
            public ContentProviderHolder getContentProviderExternal(String name,int user,IBinder t,String tag) {
              if (!"settings".equals(name) || user!=0 || t==null) throw new AssertionError("bad acquisition");
              if (failAcquire) throw new SecurityException("acquire denied");
              acquired++; token=t; if (nullHolder) return null; ContentProviderHolder h=new ContentProviderHolder();
              h.provider=nullProvider?null:provider; return h;
            }
            public void removeContentProviderExternalAsUser(String name,IBinder t,int user) {
              if (!"settings".equals(name) || t!=token || user!=0) throw new AssertionError("bad release");
              released++; if (failRelease) throw new SecurityException("release denied");
            }
          }
          static Method reader() {
            try { return Class.forName("app.aemu.stub.WifiSettingsReader").getDeclaredMethod("satelliteMode",Context.class); }
            catch (ReflectiveOperationException e) { throw new AssertionError("external satellite mode reader not implemented",e); }
          }
          static int read(Context context) throws Exception {
            try { Method m=reader(); m.setAccessible(true); return (Integer)m.invoke(null,context); }
            catch (InvocationTargetException e) { Throwable t=e.getCause(); if (t instanceof Exception) throw (Exception)t; throw e; }
          }
          static void expectFailure(Context c) throws Exception {
            try { read(c); throw new AssertionError("failure became an allowed value"); }
            catch (SecurityException | IllegalStateException expected) { }
          }
          public static void main(String[] args) throws Exception {
            Context c=new Context(); Manager m=new Manager(); m.provider=new Provider(); m.provider.context=c; ActivityManager.service=m;
            m.provider.result=new Bundle(); m.provider.result.putString("value","0");
            if(read(c)!=0 || m.released!=1) throw new AssertionError("off or release");
            m.provider.result.putString("value","1"); if(read(c)!=1 || m.released!=2) throw new AssertionError("on");
            m.provider.result.putString("value",null); if(read(c)!=0 || m.released!=3) throw new AssertionError("explicit unset");
            m.provider.result=new Bundle(); expectFailure(c); if(m.released!=4) throw new AssertionError("missing key not released");
            m.provider.result.putInt("value",1); expectFailure(c); if(m.released!=5) throw new AssertionError("wrong type not released");
            m.provider.result.putString("value","bad"); expectFailure(c); if(m.released!=6) throw new AssertionError("invalid value not released");
            m.provider.fail=true; expectFailure(c); if(m.released!=7) throw new AssertionError("denied provider not released");
            m.provider.fail=false; m.nullProvider=true; expectFailure(c); if(m.released!=8) throw new AssertionError("null provider not released");
            m.nullProvider=false; m.nullHolder=true; expectFailure(c); if(m.released!=8) throw new AssertionError("null holder released");
            m.nullHolder=false; m.failAcquire=true; expectFailure(c); if(m.released!=8) throw new AssertionError("failed acquire released");
            m.failAcquire=false; m.provider.result.putString("value","0"); m.failRelease=true; expectFailure(c);
            if(m.released!=9) throw new AssertionError("release not attempted");
            m.failRelease=false; m.provider.keyExpected="airplane_mode_on";
            Method airplane=WifiSettingsReader.class.getDeclaredMethod("airplaneMode",Context.class);
            m.provider.result.putString("value","1");
            if ((Integer)airplane.invoke(null,c)!=1 || m.released!=10) throw new AssertionError("airplane read or release");
            m.provider.result.putString("value","0");
            if ((Integer)airplane.invoke(null,c)!=0 || m.released!=11) throw new AssertionError("airplane off or release");
            System.out.println("WifiSettingsReaderTest passed");
          }
        }""",
}


def main():
    with tempfile.TemporaryDirectory(prefix="rme-settings-test-") as temporary:
        work = Path(temporary)
        sources = []
        for name, source in FAKES.items():
            path = work / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8")
            sources.append(path)
        reader = ROOT / "src/app/aemu/stub/WifiSettingsReader.java"
        if reader.exists():
            sources.append(reader)
        subprocess.run(["javac", "--release", "8", "-Xlint:-options", "-d", str(work), *map(str, sources)], check=True)
        subprocess.run(["java", "-cp", str(work), "app.aemu.stub.SettingsReaderTest"], check=True)


if __name__ == "__main__":
    main()
