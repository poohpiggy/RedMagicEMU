"""Exercise the real VoldStub transactions with a small host-side Binder fixture."""
from pathlib import Path
import subprocess
import tempfile
import os
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = {
    "android/os/IBinder.java": "package android.os; public interface IBinder {}",
    "android/os/Binder.java": """package android.os; public class Binder implements IBinder {
        protected boolean onTransact(int c, Parcel d, Parcel r, int f) throws Exception { return false; }
        public String getInterfaceDescriptor() { return null; }
    }""",
    "android/os/Parcel.java": """package android.os; public class Parcel {
        public String value; public IBinder binder;
        public void enforceInterface(String descriptor) {}
        public IBinder readStrongBinder() { return binder; }
        public String readString() { return value; }
        public void writeNoException() {} public void writeInt(int i) {}
    }""",
    "android/os/IVold.java": """package android.os; public interface IVold {
        class Stub { public static final int TRANSACTION_setListener=1, TRANSACTION_reset=2,
            TRANSACTION_onUserStarted=3, TRANSACTION_mount=4; }
    }""",
    "android/os/IVoldListener.java": """package android.os; public interface IVoldListener {
        class Stub { public static Object asInterface(IBinder b) { return b; } }
    }""",
    "android/os/ServiceManager.java": """package android.os; public class ServiceManager {
        public static IBinder checkService(String name) { return new Binder(); }
        public static void addService(String name, IBinder binder) {}
    }""",
    "app/aemu/stub/VoldPrimaryTest.java": """package app.aemu.stub;
        import android.os.*; import java.util.*;
        public class VoldPrimaryTest {
          public static class Listener implements IBinder {
            final VoldStub vold; final List<String> events = Collections.synchronizedList(new ArrayList<String>());
            volatile Thread mounting;
            Listener(VoldStub v) { vold=v; }
            public void onVolumeCreated(String id, int type, String disk, String part) {
              events.add("created:"+id+":"+type);
              mounting=new Thread(() -> { Parcel p=new Parcel(); p.value=id; vold.onTransact(4,p,null,0); });
              mounting.start();
            }
            public void onVolumeStateChanged(String id, int state) { events.add("state:"+id+":"+state); }
            public void onVolumeInternalPathChanged(String id, String path) { events.add("internal:"+id+":"+path); }
            public void onVolumePathChanged(String id, String path) { events.add("path:"+id+":"+path); }
            void expect(String id) throws Exception {
              mounting.join(5000); if (mounting.isAlive()) throw new AssertionError("mount blocked");
              List<String> expected=Arrays.asList("created:"+id+":2", "state:"+id+":0", "state:"+id+":1",
                 "internal:"+id+":/data/media", "path:"+id+":/storage/emulated", "state:"+id+":2");
              if (!events.equals(expected)) throw new AssertionError("expected "+expected+" got "+events);
              events.clear();
            }
          }
          static void check(String[] args, String id, boolean userStart) throws Exception {
            VoldStub.main(args); VoldStub vold=new VoldStub(); Listener l=new Listener(vold);
            Parcel p=new Parcel(); p.binder=l; vold.onTransact(1,p,null,0);
            vold.onTransact(2,new Parcel(),null,0); l.expect(id);
            vold.onTransact(3,new Parcel(),null,0);
            if (userStart) l.expect(id); else if (!l.events.isEmpty()) throw new AssertionError("unexpected userstart event");
          }
          public static void main(String[] args) throws Exception {
            check(new String[]{"userstart36"}, "emulated;0", true);
            check(new String[]{"userstart"}, "emulated", true);
            check(new String[]{}, "emulated", false);
            System.out.println("vold primary volume and mount order: OK");
          }
        }""",
}

with tempfile.TemporaryDirectory(prefix="vold-primary-test-") as temp:
    root = Path(temp)
    sources = []
    for name, text in SOURCES.items():
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
        sources.append(str(target))
    sources.append(str(ROOT / "src/app/aemu/stub/VoldStub.java"))
    subprocess.run(["javac", "-Xlint:-options", "--release", "8", "-d", str(root), *sources], check=True)
    target_jar = root / "target.jar"
    framework_jar = root / "framework.jar"
    with zipfile.ZipFile(target_jar, "w") as target, zipfile.ZipFile(framework_jar, "w") as framework:
        for cls in root.rglob("*.class"):
            relative = cls.relative_to(root).as_posix()
            (target if relative == "app/aemu/stub/VoldStub.class" else framework).write(cls, relative)
    subprocess.run(["java", "-cp", os.pathsep.join([str(target_jar), str(framework_jar)]),
                    "app.aemu.stub.VoldPrimaryTest"], check=True)
