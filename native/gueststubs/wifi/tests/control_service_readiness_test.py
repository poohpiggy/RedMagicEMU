"""Verify a handoff waits for the actual Ethernet network, not just its Binder."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FAKES = {
    "android/net/Network.java": "package android.net; public final class Network {}",
    "android/net/NetworkCapabilities.java": """package android.net;
        public final class NetworkCapabilities {
          public static final int TRANSPORT_ETHERNET=3; public boolean ethernet;
          public boolean hasTransport(int t) { return t==TRANSPORT_ETHERNET && ethernet; }
        }""",
    "android/net/LinkProperties.java": """package android.net;
        public final class LinkProperties { public String name; public String getInterfaceName() { return name; } }""",
    "android/net/ConnectivityManager.java": """package android.net;
        public final class ConnectivityManager {
          public Network[] networks; public NetworkCapabilities caps; public LinkProperties link;
          public Network[] getAllNetworks() { return networks; }
          public NetworkCapabilities getNetworkCapabilities(Network n) { return caps; }
          public LinkProperties getLinkProperties(Network n) { return link; }
        }""",
    "app/aemu/stub/ControlReadinessTest.java": """package app.aemu.stub;
        import android.net.*; import java.lang.reflect.*;
        public final class ControlReadinessTest {
          static Method ready;
          static boolean call(int state, Network network, ConnectivityManager manager) throws Exception {
            try { return (Boolean) ready.invoke(null,state,network,manager); }
            catch (InvocationTargetException e) {
              Throwable t=e.getCause(); if (t instanceof Exception) throw (Exception)t;
              if (t instanceof Error) throw (Error)t; throw e;
            }
          }
          static void reject(int state, Network network, ConnectivityManager manager) throws Exception {
            try { call(state,network,manager); throw new AssertionError("invalid state accepted"); }
            catch (IllegalStateException expected) { }
          }
          public static void main(String[] args) throws Exception {
            try { ready=Class.forName("app.aemu.stub.WifiControlReadiness").getDeclaredMethod(
                "isReady",int.class,Network.class,ConnectivityManager.class); ready.setAccessible(true); }
            catch (ReflectiveOperationException missing) { throw new AssertionError("control network readiness missing",missing); }
            Network target=new Network(), other=new Network(); ConnectivityManager cm=new ConnectivityManager();
            if (call(3,null,cm)) throw new AssertionError("unarmed control accepted");
            reject(1,target,cm); reject(2,target,cm); reject(4,target,cm); reject(0,null,cm);
            reject(0,target,null); reject(0,target,cm);
            cm.networks=new Network[]{other}; if (call(0,target,cm)) throw new AssertionError("invisible network accepted");
            cm.networks=new Network[]{target}; if (call(0,target,cm)) throw new AssertionError("missing attributes accepted");
            cm.caps=new NetworkCapabilities(); if (call(0,target,cm)) throw new AssertionError("missing link accepted");
            cm.link=new LinkProperties(); if (call(0,target,cm)) throw new AssertionError("missing interface accepted");
            cm.link.name="eth0"; reject(0,target,cm);
            cm.caps.ethernet=true; cm.link.name="other"; reject(0,target,cm);
            cm.link.name="eth0"; if (!call(0,target,cm)) throw new AssertionError("visible Ethernet rejected");
            System.out.println("WifiControlReadinessTest passed");
          }
        }""",
}


def main():
    with tempfile.TemporaryDirectory(prefix="rme-control-readiness-") as temporary:
        work = Path(temporary)
        sources = []
        for name, source in FAKES.items():
            path = work / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8")
            sources.append(path)
        helper = ROOT / "src/app/aemu/stub/WifiControlReadiness.java"
        if helper.exists():
            sources.append(helper)
        subprocess.run(["javac", "--release", "8", "-Xlint:-options", "-d", str(work), *map(str, sources)], check=True)
        subprocess.run(["java", "-cp", str(work), "app.aemu.stub.ControlReadinessTest"], check=True)


if __name__ == "__main__":
    main()
