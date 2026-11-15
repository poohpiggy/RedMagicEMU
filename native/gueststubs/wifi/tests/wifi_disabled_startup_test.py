"""Exercise WifiStub's real startup callback with an initially disconnected service.

The fixture replaces Android and network dependencies only; the callback body is
compiled directly from WifiStub.java. No guest or phone is started.
"""
from pathlib import Path
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parent.parent / "src/app/aemu/stub/WifiStub.java"
text = SOURCE.read_text(encoding="utf-8")
signature = "@Override public void startWifi() throws Exception {"
start = text.index(signature)
pos = start + len(signature)
depth = 1
while depth:
    if text[pos] == "{":
        depth += 1
    elif text[pos] == "}":
        depth -= 1
    pos += 1
callback = text[start:pos]

fixture = r'''
package app.aemu.stub;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
class WifiDisabledStartupTest {
    interface IBinder {}
    static class Refl {
        static int notificationErrors;
        static void log(String message) {
            if (message.startsWith("initial Wi-Fi state announcement failed:")) notificationErrors++;
        }
    }
    static class Network {}
    static final class Service implements IBinder {
        boolean enabled, connected;
        final Object transitionLock = new Object();
        Service(boolean enabled, boolean connected) { this.enabled = enabled; this.connected = connected; }
        Object networkTransitionLock() { return transitionLock; }
        boolean isEnabled() { return enabled; }
        boolean isConnected() { return connected; }
        Object wifiInfo(boolean ignored) { return new Object(); }
    }
    static final class Guest { Network network() { return new Network(); } }
    static final class Listener {
        int starts;
        boolean onConnectionChanged(boolean connected, Object info) { starts++; return true; }
    }
    static IBinder published;
    static boolean failPublication, failAnnouncement, racePublication;
    static CountDownLatch racingMutationFinished;
    static int cleanups, restorations;
    static int announcements;
    static boolean announcedEnabled, announcedConnected;
    static final class WifiStateBroadcasts {
        static void publishInitial(Object context, boolean enabled, boolean connected) {
            if (published == null) throw new AssertionError("announced before Binder publication");
            if (failAnnouncement) throw new IllegalStateException("simulated announcement failure");
            announcedEnabled = enabled; announcedConnected = connected;
            announcements++;
        }
    }
    public static final class Manager {
        public static void addService(String name, IBinder service) {
            if (failPublication) throw new IllegalStateException("simulated publication failure");
            published = service;
            if (racePublication) {
                final Service target = (Service) service;
                final CountDownLatch started = new CountDownLatch(1);
                racingMutationFinished = new CountDownLatch(1);
                Thread racer = new Thread(() -> {
                    started.countDown();
                    synchronized (target.transitionLock) {
                        target.enabled = false;
                        target.connected = false;
                    }
                    racingMutationFinished.countDown();
                });
                racer.start();
                try { started.await(2, TimeUnit.SECONDS); racingMutationFinished.await(200, TimeUnit.MILLISECONDS); }
                catch (InterruptedException e) { throw new IllegalStateException(e); }
            }
        }
    }
    static boolean waitForNetwork(Object context, Network n, boolean present) { return true; }
    static boolean isOurs(IBinder actual, IBinder expected) { return actual == expected; }
    static IBinder checkService(String name) { return published; }
    static void check(boolean value, String message) { if (!value) throw new AssertionError(message); }
    static void exercise(boolean initiallyEnabled, boolean initiallyConnected, boolean fail,
            boolean notificationFailure) throws Exception {
        final Service service = new Service(initiallyEnabled, initiallyConnected);
        final Guest guest = new Guest();
        final Listener listener = new Listener();
        final Class<?> manager = Manager.class;
        final Object guestContext = new Object();
        published = null;
        failPublication = fail;
        failAnnouncement = notificationFailure;
        Refl.notificationErrors = 0;
        cleanups = restorations = announcements = 0;
        WifiEthernetHandoff.Backend backend = new WifiEthernetHandoff.Backend() {
            boolean replacementAttempted;
            public Object oldNetwork() { return new Object(); }
            public void stopEthernet() {}
            public boolean waitGone(Object network) { return true; }
            CALLBACK
            public boolean cleanupWifi() { cleanups++; return true; }
            public void restoreEthernet() { restorations++; }
            public boolean waitEthernetRestored() { return true; }
        };
        if (fail) {
            try { new WifiEthernetHandoff(backend).run(); throw new AssertionError("publication should fail"); }
            catch (WifiEthernetHandoff.FailedAndRestored expected) {
                check(cleanups == 1 && restorations == 1 && published == null,
                        "publication failure did not restore Ethernet");
                check(announcements == 0, "failed handoff must not announce initial state");
            }
            return;
        }
        new WifiEthernetHandoff(backend).run();
        check(published == service, "Wi-Fi service was not published");
        if (notificationFailure) {
            check(announcements == 0 && Refl.notificationErrors == 1,
                    "failed notification must be reported without losing the confirmed network");
        } else {
            check(announcements == 1, "published service must announce its initial state");
            check(announcedEnabled == initiallyEnabled && announcedConnected == initiallyConnected,
                    "announcement did not match actual service state");
        }
        check(listener.starts == (initiallyConnected ? 1 : 0),
                "network should start only for an initially connected service");
        if (racePublication) check(racingMutationFinished.await(2, TimeUnit.SECONDS), "racing mutation did not finish");
    }
    public static void main(String[] args) throws Exception {
        exercise(true, true, false, false);
        try { exercise(false, false, false, false); exercise(true, false, false, false); }
        catch (Exception failure) {
            throw new AssertionError("disabled or forgotten Wi-Fi must publish its service without connecting", failure);
        }
        exercise(false, false, true, false);
        exercise(true, true, true, false);
        exercise(true, true, false, true);
        racePublication = true;
        exercise(true, true, false, false);
        System.out.println("Wi-Fi startup accepts disconnected persisted state");
    }
}
'''.replace("CALLBACK", callback)

with tempfile.TemporaryDirectory(prefix="rme-wifi-startup-") as tmp:
    java = Path(tmp) / "WifiDisabledStartupTest.java"
    java.write_text(fixture, encoding="utf-8")
    handoff = SOURCE.parent / "WifiEthernetHandoff.java"
    subprocess.run(["javac", "-d", tmp, str(java), str(handoff)], check=True)
    subprocess.run(["java", "-cp", tmp, "app.aemu.stub.WifiDisabledStartupTest"], check=True)
