"""Run the API 36 Ethernet-control Binder against a minimal fake framework."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = {
    "android/os/IBinder.java": """package android.os; public interface IBinder {
        int FIRST_CALL_TRANSACTION = 1; int INTERFACE_TRANSACTION = 1598968902;
        interface DeathRecipient { void binderDied(); }
        boolean isBinderAlive();
        void linkToDeath(DeathRecipient d, int f) throws RemoteException;
        boolean unlinkToDeath(DeathRecipient d, int f);
        boolean transact(int c, Parcel d, Parcel r, int f) throws RemoteException; }""",
    "android/os/RemoteException.java": "package android.os; public class RemoteException extends Exception {}",
    "android/os/Parcelable.java": "package android.os; public interface Parcelable {}",
    "android/os/Parcel.java": """package android.os; public class Parcel {
        public String token; public int result; public Object object; public IBinder owner, original, replacement; int strongIndex; byte[] bytes;
        public static Parcel obtain() { return new Parcel(); }
        public IBinder readStrongBinder() { int i=strongIndex++; return i==0?owner:i==1?original:replacement; }
        public void writeStrongBinder(IBinder b) { if (owner==null) owner=b; else if (original==null) original=b; else replacement=b; }
        public void writeInterfaceToken(String s) { token = s; } public void recycle() {} public void readException() {}
        public int readInt() { return result; } @SuppressWarnings("unchecked") public <T> T readParcelable(ClassLoader c) { return (T) object; }
        public void enforceInterface(String d) { strongIndex=0; if (!d.equals(token)) throw new SecurityException(); }
        public void enforceNoDataAvail() {} public void writeByteArray(byte[] b) {bytes=b==null?null:b.clone();} public byte[] createByteArray(){return bytes==null?null:bytes.clone();} public void writeString(String s) {} public void writeNoException() {}
        public void writeInt(int i) { result = i; } public void writeParcelable(Parcelable p, int f) { object = p; }
    }""",
    "android/os/Binder.java": """package android.os; public class Binder implements IBinder {
        public static int uid; public static int getCallingUid() { return uid; }
        public DeathRecipient death; public boolean alive=true; public boolean isBinderAlive() { return alive; }
        public void linkToDeath(DeathRecipient d, int f) { death=d; }
        public boolean unlinkToDeath(DeathRecipient d, int f) { if (death == d) death = null; return true; }
        public void die() { DeathRecipient d=death; if (d != null) d.binderDied(); }
        public boolean transact(int c, Parcel d, Parcel r, int f) throws RemoteException { return onTransact(c,d,r,f); }
        protected boolean onTransact(int c, Parcel d, Parcel r, int f) throws RemoteException { return false; }
    }""",
    "android/os/ServiceManager.java": """package android.os; public class ServiceManager {
        public static IBinder current; public static volatile IBinder wifi; public static IBinder checkService(String s) { return s.equals("wifi")?wifi:current; }
        public static void addService(String s, IBinder b) { if (s.equals("wifi")) wifi=b; else current = b; }
    }""",
    "android/net/Network.java": "package android.net; public class Network implements android.os.Parcelable {}",
    "android/os/Looper.java": "package android.os; public class Looper {}",
    "android/content/Context.java": "package android.content; public class Context {}",
    "android/net/LinkProperties.java": "package android.net; public class LinkProperties {}",
    "android/net/NetworkCapabilities.java": """package android.net; public class NetworkCapabilities {
        public void addCapability(int cap) {} }""",
    "android/net/NetworkProvider.java": "package android.net; public class NetworkProvider {}",
    "android/net/NetworkAgentConfig.java": """package android.net; public class NetworkAgentConfig {
        public static class Builder { public void setLegacyType(int type) {} public void setLegacyTypeName(String name) {}
        public NetworkAgentConfig build() { return new NetworkAgentConfig(); } } }""",
    "android/net/NetworkAgent.java": """package android.net;
        import android.content.Context; import android.os.Looper;
        public abstract class NetworkAgent {
          public static int unregisters; public static boolean failMark, failUnregister;
          public NetworkAgent(Context c, Looper l, String tag, NetworkCapabilities n, LinkProperties p,
              int s, NetworkAgentConfig cfg, NetworkProvider provider) {}
          public Network register() { return new Network(); }
          public void markConnected() { if (failMark) throw new IllegalStateException("connect"); }
          public void unregister() { unregisters++; if (failUnregister) throw new IllegalStateException("unregister"); }
          protected abstract void unwanted();
        }""",
    "app/aemu/stub/NetStub.java": """package app.aemu.stub;
        import java.lang.reflect.*;
        public class NetStub {
          static Object newObj(String cls, Class<?>[] types, Object... args) throws Exception {
            return Class.forName(cls).getDeclaredConstructor(types).newInstance(args);
          }
          static void call(Object o, String name, Class<?>[] types, Object... args) throws Exception {
            o.getClass().getMethod(name, types).invoke(o, args);
          }
        }""",
    "app/aemu/stub/ControlTest.java": """package app.aemu.stub;
        import android.net.Network; import android.os.*;
        public class ControlTest {
          static class Lease implements TargetNetControl.Lease {
            volatile int calls, restores; volatile boolean available = true; boolean registered = true, fail, failRestore, failRegistration; int busyRaces; IBinder wifiDuringRestore; final Network network = new Network();
            public Network network() { return network; }
            public boolean networkRegistered() { if (failRegistration) throw new IllegalStateException("registration query failed"); return registered; }
            public void stop() throws Exception { calls++; if (fail) throw new Exception("failure"); }
            public void restore() throws Exception { restores++; if (busyRaces-- > 0) throw new TargetNetControl.InterfaceBusyException(); if (failRestore) throw new Exception("restore failure"); if (wifiDuringRestore != null) ServiceManager.wifi = wifiDuringRestore; }
            public boolean canRestore() { return available; }
          }
          static class Registry implements TargetNetControl.WifiRegistry {
            IBinder current; int writes;
            public IBinder current() { return current; }
            public void publish(IBinder b) { current = b; writes++; }
          }
          public static void main(String[] args) throws Exception {
            Lease lease = new Lease(); TargetNetControl c = new TargetNetControl(lease);
            Parcel data = new Parcel(), reply = new Parcel(); data.token = TargetNetControl.DESCRIPTOR;
            Binder session = new Binder(); data.owner = session;
            Binder.uid = 10001;
            try { c.onTransact(TargetNetControl.STATUS, data, reply, 0); throw new AssertionError(); }
            catch (SecurityException expected) { }
            Binder.uid = 0;
            Registry registry = new Registry(); Binder oldWifi = new Binder(), replacement = new Binder(), other = new Binder();
            registry.current = other;
            if (TargetNetControl.restoreWifiBinder(registry, oldWifi, replacement) || registry.writes != 0)
              throw new AssertionError("unrelated service overwritten");
            registry.current = oldWifi;
            if (!TargetNetControl.restoreWifiBinder(registry, oldWifi, replacement) || registry.writes != 0)
              throw new AssertionError("original service needlessly replaced");
            registry.current = replacement;
            if (!TargetNetControl.restoreWifiBinder(registry, oldWifi, replacement) || registry.current != oldWifi || registry.writes != 1)
              throw new AssertionError("original service not restored");
            registry.current = null; oldWifi.alive = false;
            if (TargetNetControl.restoreWifiBinder(registry, oldWifi, replacement) || registry.writes != 1)
              throw new AssertionError("dead original service published");
            oldWifi.alive = true; data.original=oldWifi; data.replacement=replacement;
            ServiceManager.wifi = other;
            try { c.onTransact(TargetNetControl.STOP, data, reply, 0); throw new AssertionError("unrelated original accepted"); }
            catch (IllegalStateException expected) { }
            if (lease.calls != 0) throw new AssertionError("Ethernet stopped with unrelated Wi-Fi service");
            ServiceManager.wifi = oldWifi;
            TargetNetControl.publish(lease);
            if (ServiceManager.current == null) throw new AssertionError("control not published");
            try { TargetNetControl.publish(lease); throw new AssertionError("replaced control"); }
            catch (IllegalStateException expected) { }
            if (!c.onTransact(TargetNetControl.STATUS, data, reply, 0) || reply.result != 0 || reply.object != lease.network)
              throw new AssertionError("initial status or network");
            c.onTransact(TargetNetControl.STOP, data, reply, 0);
            if (reply.result != 1 || lease.calls != 1) throw new AssertionError("stop request");
            c.onTransact(TargetNetControl.STOP, data, reply, 0);
            if (lease.calls != 1) throw new AssertionError("duplicate stop");
            data.owner = new Binder();
            try { c.onTransact(TargetNetControl.RESTORE, data, reply, 0); throw new AssertionError(); }
            catch (SecurityException expected) { }
            data.owner = session;
            c.onTransact(TargetNetControl.RESTORE, data, reply, 0);
            if (lease.restores != 1 || reply.result != 0) throw new AssertionError("restore");
            c.onTransact(TargetNetControl.RESTORE, data, reply, 0);
            if (lease.restores != 1) throw new AssertionError("duplicate restore");
            Lease mismatch = new Lease(); TargetNetControl mismatchControl = new TargetNetControl(mismatch);
            data.owner = new Binder();
            mismatchControl.onTransact(TargetNetControl.STOP, data, reply, 0);
            ServiceManager.wifi = other;
            mismatchControl.onTransact(TargetNetControl.RESTORE, data, reply, 0);
            if (mismatch.restores != 1) throw new AssertionError("Ethernet not restored after Wi-Fi mismatch");
            if (reply.result != 2) throw new AssertionError("unconfirmed Wi-Fi reported active on explicit restore");
            mismatchControl.onTransact(TargetNetControl.STATUS, data, reply, 0);
            if (reply.result != 2 || ServiceManager.wifi != other) throw new AssertionError("Wi-Fi mismatch not retained");
            ServiceManager.wifi = oldWifi;
            Lease invisible = new Lease(); invisible.registered = false;
            TargetNetControl invisibleControl = new TargetNetControl(invisible);
            data.owner = new Binder();
            invisibleControl.onTransact(TargetNetControl.STOP, data, reply, 0);
            try { invisibleControl.onTransact(TargetNetControl.RESTORE, data, reply, 0); }
            catch (IllegalStateException expected) { }
            invisibleControl.onTransact(TargetNetControl.STATUS, data, reply, 0);
            if (reply.result != 2) throw new AssertionError("invisible restored network reported active");
            Lease transientRace = new Lease(); transientRace.busyRaces = 1;
            TargetNetControl cRace = new TargetNetControl(transientRace); Binder raceOwner = new Binder(); data.owner = raceOwner;
            cRace.onTransact(TargetNetControl.STOP, data, reply, 0);
            cRace.onTransact(TargetNetControl.RESTORE, data, reply, 0);
            if (transientRace.restores != 2 || reply.result != 0) throw new AssertionError("transient race not retried");
            Lease busy = new Lease(); busy.available = false;
            try { TargetNetControl.restoreWhenAvailable(busy, 0); throw new AssertionError("busy interface accepted"); }
            catch (IllegalStateException expected) { }
            if (busy.restores != 0) throw new AssertionError("restore called while busy");
            Lease badRestore = new Lease(); TargetNetControl cRestore = new TargetNetControl(badRestore);
            Binder restoreOwner = new Binder(); data.owner = restoreOwner;
            cRestore.onTransact(TargetNetControl.STOP, data, reply, 0); badRestore.failRestore = true;
            try { cRestore.onTransact(TargetNetControl.RESTORE, data, reply, 0); throw new AssertionError(); }
            catch (IllegalStateException expected) { }
            cRestore.onTransact(TargetNetControl.STATUS, data, reply, 0);
            if (reply.result != 2 || badRestore.restores != 1) throw new AssertionError("failed restore not unknown");
            data.owner = session;
            Lease broken = new Lease(); broken.fail = true; TargetNetControl c2 = new TargetNetControl(broken);
            try { c2.onTransact(TargetNetControl.STOP, data, reply, 0); throw new AssertionError(); }
            catch (IllegalStateException expected) { }
            c2.onTransact(TargetNetControl.STATUS, data, reply, 0);
            if (reply.result != 2 || reply.object != broken.network) throw new AssertionError("unknown state");
            try { c2.onTransact(TargetNetControl.STOP, data, reply, 0); throw new AssertionError(); }
            catch (IllegalStateException expected) { }
            if (broken.calls != 1) throw new AssertionError("retried unknown stop");
            Lease lost = new Lease(); TargetNetControl c3 = new TargetNetControl(lost);
            Binder owner = new Binder(); data.owner = owner;
            c3.onTransact(TargetNetControl.STOP, data, reply, 0);
            ServiceManager.wifi = replacement;
            owner.die();
            for (int n=0; n<100 && lost.restores==0; n++) Thread.sleep(10);
            if (lost.restores != 1) throw new AssertionError("dead client did not restore Ethernet");
            if (ServiceManager.wifi != oldWifi) throw new AssertionError("dead client did not restore Wi-Fi Binder");
            c3.onTransact(TargetNetControl.STATUS, data, reply, 0);
            if (reply.result != 0) throw new AssertionError("dead client recovery not active");
            Lease wifiFirst = new Lease(); wifiFirst.available = false;
            TargetNetControl wifiFirstControl = new TargetNetControl(wifiFirst);
            Binder wifiFirstOwner = new Binder(); data.owner = wifiFirstOwner;
            wifiFirstControl.onTransact(TargetNetControl.STOP, data, reply, 0);
            ServiceManager.wifi = replacement; wifiFirstOwner.die();
            for (int n=0; n<100 && ServiceManager.wifi != oldWifi; n++) Thread.sleep(10);
            if (ServiceManager.wifi != oldWifi) throw new AssertionError("busy interface delayed Wi-Fi Binder restoration");
            if (wifiFirst.restores != 0) throw new AssertionError("Ethernet restored while interface busy");
            wifiFirst.available = true;
            for (int n=0; n<100 && wifiFirst.restores==0; n++) Thread.sleep(10);
            if (wifiFirst.restores != 1) throw new AssertionError("Ethernet not restored after interface release");
            busy = new Lease(); busy.available = false; TargetNetControl c4 = new TargetNetControl(busy);
            Binder owner2 = new Binder(); data.owner = owner2;
            c4.onTransact(TargetNetControl.STOP, data, reply, 0); ServiceManager.wifi = other; owner2.die(); Thread.sleep(50);
            if (busy.restores != 0) throw new AssertionError("restored while interface busy");
            if (ServiceManager.wifi != other) throw new AssertionError("unrelated Wi-Fi service overwritten");
            busy.available = true;
            for (int n=0; n<100 && busy.restores==0; n++) Thread.sleep(10);
            if (busy.restores != 1) throw new AssertionError("failed to restore after interface free");
            if (ServiceManager.wifi != other) throw new AssertionError("unrelated Wi-Fi service overwritten on recovery");
            c4.onTransact(TargetNetControl.STATUS, data, reply, 0);
            if (reply.result != 2) throw new AssertionError("unconfirmed Wi-Fi recovery reported success");
            ServiceManager.wifi = oldWifi;
            Lease timeoutLease = new Lease(); timeoutLease.available = false;
            TargetNetControl timeoutControl = new TargetNetControl(timeoutLease);
            Binder timeoutOwner = new Binder(); data.owner = timeoutOwner;
            timeoutControl.onTransact(TargetNetControl.STOP, data, reply, 0);
            ServiceManager.wifi = replacement; timeoutOwner.alive = false;
            timeoutControl.recoverOwner(timeoutOwner, 0);
            timeoutControl.onTransact(TargetNetControl.STATUS, data, reply, 0);
            if (reply.result != 2 || timeoutLease.restores != 0 || ServiceManager.wifi != oldWifi)
              throw new AssertionError("interface timeout not marked unknown after restoring Wi-Fi Binder");
            Lease lateChange = new Lease(); lateChange.wifiDuringRestore = other;
            TargetNetControl lateChangeControl = new TargetNetControl(lateChange);
            Binder lateOwner = new Binder(); data.owner = lateOwner;
            lateChangeControl.onTransact(TargetNetControl.STOP, data, reply, 0);
            ServiceManager.wifi = replacement; lateOwner.alive = false;
            lateChangeControl.recoverOwner(lateOwner, 0);
            lateChangeControl.onTransact(TargetNetControl.STATUS, data, reply, 0);
            if (reply.result != 2 || ServiceManager.wifi != other)
              throw new AssertionError("late replacement of Wi-Fi Binder reported active");
            ServiceManager.wifi = oldWifi;
            Lease unknownRegistration = new Lease(); unknownRegistration.failRegistration = true;
            TargetNetControl unknownControl = new TargetNetControl(unknownRegistration);
            Binder unknownOwner = new Binder(); data.owner = unknownOwner;
            unknownControl.onTransact(TargetNetControl.STOP, data, reply, 0);
            ServiceManager.wifi = replacement; unknownOwner.alive = false;
            unknownControl.recoverOwner(unknownOwner, 0);
            unknownControl.onTransact(TargetNetControl.STATUS, data, reply, 0);
            if (reply.result != 2) throw new AssertionError("failed registration check reported active after death");
            final NetAgent2 agent = NetAgent2.start(new android.content.Context(), new android.os.Looper(),
                new android.net.NetworkCapabilities(), new android.net.LinkProperties());
            if (agent.network() == null) throw new AssertionError("agent did not retain Network");
            new TargetNetControl(new TargetNetControl.Lease() {
              public Network network() { return agent.network(); }
              public boolean networkRegistered() { return true; }
              public void stop() throws Exception { agent.stop(); }
              public void restore() { throw new UnsupportedOperationException(); }
              public boolean canRestore() { return true; }
            }).onTransact(TargetNetControl.STOP, data, reply, 0);
            if (android.net.NetworkAgent.unregisters != 1) throw new AssertionError("agent not unregistered");
            android.net.NetworkAgent.failMark = true;
            try { NetAgent2.start(new android.content.Context(), new android.os.Looper(),
                new android.net.NetworkCapabilities(), new android.net.LinkProperties()); throw new AssertionError(); }
            catch (IllegalStateException expected) { }
            if (android.net.NetworkAgent.unregisters != 2) throw new AssertionError("failed startup not cleaned");
            System.out.println("TargetNetControlTest passed");
          }
        }""",
}


def main():
    with tempfile.TemporaryDirectory(prefix="redmagic-netcontrol-") as tmp:
        work = Path(tmp)
        for name, content in SOURCES.items():
            path = work / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        sources = [str(work / name) for name in SOURCES]
        sources.extend(str(ROOT / "src/app/aemu/stub" / name) for name in ("TargetNetControl.java", "NetAgent2.java"))
        subprocess.run(["javac", "--release", "8", "-d", str(work)] + sources, check=True)
        subprocess.run(["java", "-cp", str(work), "app.aemu.stub.ControlTest"], check=True)


if __name__ == "__main__":
    main()
