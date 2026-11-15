"""Exercise the adapter against a minimal fake framework; not a device test."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
FAKES = {
    "android/content/Context.java": """package android.content;
        import android.net.ConnectivityManager;
        public class Context {
          public static ConnectivityManager connectivity = new ConnectivityManager();
          public <T> T getSystemService(Class<T> name) { return name.cast(connectivity); }
        }""",
    "android/os/Looper.java": "package android.os; public class Looper {}",
    "android/os/SystemClock.java": "package android.os; public class SystemClock { public static long elapsedRealtime() { return System.nanoTime()/1000000L; } }",
    "android/os/Handler.java": """package android.os;
        public class Handler {
          public static boolean defer, reject; public static Runnable pending;
          public Handler(Looper l) {}
          public boolean post(Runnable r) { if (reject) return false; if (defer) pending = r; else r.run(); return true; }
          public static void runPending() { Runnable r = pending; pending = null; r.run(); }
        }""",
    "android/net/TransportInfo.java": "package android.net; public interface TransportInfo {}",
    "android/net/Network.java": "package android.net; public class Network {}",
    "android/net/ConnectivityManager.java": """package android.net;
        public class ConnectivityManager {
          public Network[] networks = new Network[0]; public LinkProperties existing; public int vanishAfter;
          public Network[] getAllNetworks() { if (vanishAfter > 0 && --vanishAfter == 0) networks = new Network[0]; return networks; }
          public LinkProperties getLinkProperties(Network network) { return existing; }
        }""",
    "android/net/NetworkProvider.java": "package android.net; public class NetworkProvider {}",
    "android/net/LinkProperties.java": """package android.net;
        import java.net.InetAddress; import java.util.*;
        public class LinkProperties {
          public static boolean rejectRoute;
          public String iface; public final List<LinkAddress> addresses = new ArrayList<LinkAddress>();
          public final List<RouteInfo> routes = new ArrayList<RouteInfo>();
          public final List<InetAddress> dns = new ArrayList<InetAddress>();
          public void setInterfaceName(String name) { iface = name; }
          public String getInterfaceName() { return iface; }
          public boolean addLinkAddress(LinkAddress addr) { return addresses.add(addr); }
          public boolean addRoute(RouteInfo route) { return !rejectRoute && routes.add(route); }
          public boolean addDnsServer(InetAddress addr) { return dns.add(addr); }
        }""",
    "android/net/LinkAddress.java": """package android.net;
        import java.net.InetAddress;
        public class LinkAddress {
          public InetAddress addr; public int prefix;
          public LinkAddress(InetAddress a, int p) { addr = a; prefix = p; }
        }""",
    "android/net/IpPrefix.java": """package android.net;
        import java.net.InetAddress;
        public class IpPrefix {
          public InetAddress addr; public int prefix;
          public IpPrefix(InetAddress a, int p) { addr = a; prefix = p; }
        }""",
    "android/net/RouteInfo.java": """package android.net;
        import java.net.InetAddress;
        public class RouteInfo {
          public IpPrefix prefix; public InetAddress gateway; public String iface; public int type;
          public RouteInfo(IpPrefix p, InetAddress g, String i, int t) {
            prefix = p; gateway = g; iface = i; type = t;
          }
        }""",
    "android/net/NetworkCapabilities.java": """package android.net;
        import java.util.*;
        public class NetworkCapabilities {
          public final Set<Integer> transports = new HashSet<Integer>();
          public final Set<Integer> caps = new HashSet<Integer>();
          public TransportInfo info;
          public void addTransportType(int i) { transports.add(i); }
          public void addCapability(int i) { caps.add(i); }
          public void setTransportInfo(TransportInfo i) { info = i; }
        }""",
    "android/net/NetworkAgentConfig.java": """package android.net;
        public class NetworkAgentConfig {
          public int type; public String name;
          public static class Builder {
            private final NetworkAgentConfig c = new NetworkAgentConfig();
            public void setLegacyType(int type) { c.type = type; }
            public void setLegacyTypeName(String name) { c.name = name; }
            public NetworkAgentConfig build() { return c; }
          }
        }""",
    "android/net/NetworkAgent.java": """package android.net;
        import android.content.Context; import android.os.Looper;
        public abstract class NetworkAgent {
          public static NetworkCapabilities caps; public static LinkProperties link; public static NetworkAgent last;
          public static NetworkAgentConfig config; public static int starts, stops;
          public static boolean failConnect, failStop, failRegister, unwantedDuringConnect;
          public NetworkAgent(Context c, Looper l, String tag, NetworkCapabilities nc,
              LinkProperties lp, int score, NetworkAgentConfig cfg, NetworkProvider provider) {
            caps = nc; link = lp; config = cfg; last = this;
          }
          protected abstract void unwanted();
          public Network register() { starts++; if (failRegister) throw new IllegalStateException("registration failed"); return new Network(); }
          public void markConnected() {
            if (failConnect) throw new IllegalStateException("failure");
            if (unwantedDuringConnect) unwanted();
          }
          public void unregister() { stops++; if (failStop) throw new IllegalStateException("cleanup failed"); }
        }""",
    "app/aemu/stub/AdapterTest.java": """package app.aemu.stub;
        import android.content.Context; import android.os.Looper; import android.os.Handler; import android.net.*;
        import java.net.InetAddress; import java.util.Arrays;
        public class AdapterTest {
          static void awaitBlocked(Thread thread) throws Exception {
            long deadline = System.nanoTime() + 2000000000L;
            while (thread.getState() != Thread.State.BLOCKED && System.nanoTime() < deadline) Thread.sleep(1);
            if (thread.getState() != Thread.State.BLOCKED) throw new AssertionError("callback did not reach service lock");
          }
          public static void main(String[] args) throws Exception {
            InetAddress ip = InetAddress.getByName("10.0.2.15");
            InetAddress gateway = InetAddress.getByName("10.0.2.2");
            InetAddress dns = InetAddress.getByName("1.1.1.1");
            LinkProperties lp = VirtualWifiLinkProperties.create("eth0", ip, 24, gateway, Arrays.asList(dns));
            if (!"eth0".equals(lp.iface) || lp.addresses.size() != 1 || !lp.addresses.get(0).addr.equals(ip)
                || lp.addresses.get(0).prefix != 24 || lp.dns.size() != 1 || !lp.dns.get(0).equals(dns)
                || lp.routes.size() != 2 || lp.routes.get(0).prefix == null
                || lp.routes.get(0).prefix.prefix != 24 || lp.routes.get(0).gateway != null
                || lp.routes.get(1).prefix != null || !gateway.equals(lp.routes.get(1).gateway)
                || !"eth0".equals(lp.routes.get(1).iface) || lp.routes.get(1).type != 1)
              throw new AssertionError("link addresses, routes or DNS");
            try { VirtualWifiLinkProperties.create("eth0", ip, 33, gateway, Arrays.asList(dns));
              throw new AssertionError("invalid prefix accepted");
            } catch (IllegalArgumentException expected) { }
            LinkProperties.rejectRoute = true;
            try { VirtualWifiLinkProperties.create("eth0", ip, 24, gateway, Arrays.asList(dns));
              throw new AssertionError("rejected route accepted");
            } catch (IllegalStateException expected) { }
            LinkProperties.rejectRoute = false;
            TransportInfo info = new TransportInfo() {};
            final int[] unwanted = {0};
            VirtualWifiNetworkAgent a = VirtualWifiNetworkAgent.start(new Context(), new Looper(), lp, info,
                new Runnable() { public void run() { unwanted[0]++; } });
            if (a.network() == null || NetworkAgent.link != lp || NetworkAgent.caps.info != info
                || !NetworkAgent.caps.transports.contains(1) || NetworkAgent.caps.transports.contains(3)
                || NetworkAgent.caps.caps.contains(16) || !NetworkAgent.caps.caps.contains(12)
                || NetworkAgent.config.type != 1 || !"WIFI".equals(NetworkAgent.config.name))
              throw new AssertionError("Wi-Fi network contract");
            a.onNetworkUnwanted();
            if (unwanted[0] != 1) throw new AssertionError("unwanted callback");
            a.stop();
            NetworkAgent.failConnect = true;
            try { VirtualWifiNetworkAgent.start(new Context(), new Looper(), lp, info, null);
              throw new AssertionError("failure not propagated");
            } catch (IllegalStateException expected) { }
            if (NetworkAgent.starts != 2 || NetworkAgent.stops != 2)
              throw new AssertionError("failed registration not cleaned up");
            NetworkAgent.failStop = true;
            try { VirtualWifiNetworkAgent.start(new Context(), new Looper(), lp, info, null);
              throw new AssertionError("uncertain agent not reported");
            } catch (WifiNetworkLifecycle.UncertainStartException expected) {
              if (expected.agent() == null || expected.getSuppressed().length != 1)
                throw new AssertionError("uncertain agent or cleanup error lost");
            }
            NetworkAgent.failRegister = true;
            try { VirtualWifiNetworkAgent.start(new Context(), new Looper(), lp, info, null);
              throw new AssertionError("uncertain registration not reported");
            } catch (WifiNetworkLifecycle.UncertainStartException expected) {
              if (expected.agent() == null || expected.getSuppressed().length != 1)
                throw new AssertionError("uncertain registration agent lost");
            }
            NetworkAgent.failRegister = false; NetworkAgent.failConnect = false; NetworkAgent.failStop = false;
            VirtualWifiState state = new VirtualWifiState("RedMagicEMU", "");
            final int[] notifications = {0};
            final int[] failures = {0};
            final Object transitions = new Object();
            WifiGuestNetwork guest = new WifiGuestNetwork(new Context(), new Looper(), lp, state, transitions,
              new WifiGuestNetwork.WifiInfoSource() { public Object get() { return info; } },
              new WifiGuestNetwork.FailureListener() {
                public void onFailure(Exception e) {
                  if (Thread.holdsLock(state)) throw new AssertionError("failure listener called under state lock");
                  synchronized (transitions) { failures[0]++; }
                }
                public void onNetworkUnwanted() {
                  if (Thread.holdsLock(state)) throw new AssertionError("listener called under state lock");
                  synchronized (transitions) { notifications[0]++; }
                }
              });
            guest.setEnabled(true);
            Context.connectivity.networks = new Network[] {new Network()};
            Context.connectivity.existing = lp;
            int before = NetworkAgent.starts;
            try { WifiGuestNetwork.requireAvailableInterface(new Context(), "eth0"); throw new AssertionError("interface conflict accepted"); }
            catch (IllegalStateException expected) { }
            if (state.isConnected() || NetworkAgent.starts != before) throw new AssertionError("conflicting agent started");
            Context.connectivity.existing = null;
            try { WifiGuestNetwork.requireAvailableInterface(new Context(), "eth0"); throw new AssertionError("unknown interface accepted"); }
            catch (IllegalStateException expected) { }
            if (state.isConnected() || NetworkAgent.starts != before) throw new AssertionError("unknown interface started");
            Context.connectivity.vanishAfter = 3;
            if (guest.connect("RedMagicEMU", "") != VirtualWifiState.Result.SUCCESS)
              throw new AssertionError("connection did not wait for transient unknown");
            guest.disconnect();
            Context.connectivity.networks = new Network[] {new Network()};
            Context.connectivity.existing = lp;
            if (WifiGuestNetwork.waitForAvailableInterface(new Context(), "eth0", 0))
              throw new AssertionError("busy interface accepted");
            Context.connectivity.networks = new Network[0];
            if (guest.connect("RedMagicEMU", "") != VirtualWifiState.Result.SUCCESS
                || guest.network() == null || NetworkAgent.starts != before + 2)
              throw new AssertionError("guest network not connected");
            Handler.defer = true;
            VirtualWifiNetworkAgent oldAgent = (VirtualWifiNetworkAgent) NetworkAgent.last;
            oldAgent.onNetworkUnwanted();
            guest.disconnect();
            guest.reconnect();
            Network replacement = guest.network();
            if (replacement == null) throw new AssertionError("replacement network not connected");
            Handler.runPending();
            if (guest.network() != replacement || !state.isConnected() || notifications[0] != 0)
              throw new AssertionError("old unwanted callback disconnected replacement network");
            Handler.reject = true;
            oldAgent.onNetworkUnwanted();
            if (failures[0] != 1 || guest.network() != replacement)
              throw new AssertionError("shared looper failure was not reported");
            Handler.reject = false;
            ((VirtualWifiNetworkAgent) NetworkAgent.last).onNetworkUnwanted();
            final Throwable[] queuedCallbackError = {null};
            Thread callback = new Thread(new Runnable() { public void run() {
              try { Handler.runPending(); } catch (Throwable failure) { queuedCallbackError[0] = failure; }
            } });
            callback.setDaemon(true);
            synchronized (transitions) {
              callback.start();
              awaitBlocked(callback);
              guest.disconnect();
              guest.reconnect();
              replacement = guest.network();
            }
            callback.join(2000);
            if (callback.isAlive() || queuedCallbackError[0] != null)
              throw new AssertionError("callback failed or deadlocked", queuedCallbackError[0]);
            if (guest.network() != replacement || !state.isConnected() || notifications[0] != 0)
              throw new AssertionError("stale service notification after concurrent reconnect");
            Handler.defer = false;
            ((VirtualWifiNetworkAgent) NetworkAgent.last).onNetworkUnwanted();
            if (guest.network() != null || state.isConnected() || notifications[0] != 1)
              throw new AssertionError("current unwanted callback did not disconnect and notify");
            VirtualWifiState startupState = new VirtualWifiState("RedMagicEMU", "");
            final int[] startupNotifications = {0};
            WifiGuestNetwork startup = new WifiGuestNetwork(new Context(), new Looper(), lp, startupState,
              new Object(), new WifiGuestNetwork.WifiInfoSource() { public Object get() { return info; } },
              new WifiGuestNetwork.FailureListener() {
                public void onFailure(Exception e) { throw new AssertionError(e); }
                public void onNetworkUnwanted() { startupNotifications[0]++; }
              });
            Handler.defer = true;
            NetworkAgent.unwantedDuringConnect = true;
            startup.setEnabled(true);
            if (startup.connect("RedMagicEMU", "") != VirtualWifiState.Result.SUCCESS
                || startup.network() == null || Handler.pending == null || startupNotifications[0] != 0)
              throw new AssertionError("registration callback must wait until registration completes");
            NetworkAgent.unwantedDuringConnect = false;
            Handler.runPending();
            if (startup.network() != null || startupState.isConnected() || startupNotifications[0] != 1)
              throw new AssertionError("queued registration callback was lost");
            Handler.defer = false;
            guest.reconnect();
            NetworkAgent.failStop = true;
            ((VirtualWifiNetworkAgent) NetworkAgent.last).onNetworkUnwanted();
            if (!state.isConnected() || guest.networkStatus() != WifiNetworkLifecycle.Status.UNKNOWN
                || notifications[0] != 1 || failures[0] != 2)
              throw new AssertionError("failed removal must report unknown without a disconnect notification");
            NetworkAgent.failStop = false;
            System.out.println("VirtualWifiNetworkAgent fake-framework test passed");
          }
        }""",
}


def main():
    with tempfile.TemporaryDirectory(prefix="redmagic-wifi-agent-") as directory:
        work = pathlib.Path(directory)
        for name, contents in FAKES.items():
            path = work / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")
        sources = [str(work / name) for name in FAKES]
        sources.extend(str(ROOT / "src/app/aemu/stub" / name) for name in
                       ("WifiNetworkLifecycle.java", "VirtualWifiNetworkAgent.java", "VirtualWifiLinkProperties.java",
                        "VirtualWifiState.java", "WifiConnectionCoordinator.java", "WifiNetworkGate.java",
                        "WifiGuestNetwork.java"))
        subprocess.run(["javac", "--release", "8", "-d", str(work)] + sources, check=True, timeout=30)
        gate = work / "network-gate"
        subprocess.run(["java", f"-Dapp.aemu.networkGatePath={gate}", "-cp", str(work),
                        "app.aemu.stub.AdapterTest"], check=True, timeout=30)


if __name__ == "__main__":
    main()
