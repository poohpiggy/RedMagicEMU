package app.aemu.stub;

import android.os.Binder;
import android.os.IBinder;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Host-only contract tests; no service manager or firmware tree is touched. */
public final class RadioCanaryRegistrationTest {
    private static final String TOKEN = "0123456789abcdef0123456789abcdef";
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    private static final class Backend implements RadioCanaryRegistration.Backend {
        boolean declared = true, exists, failAdd, ready = true; int adds; CountDownLatch block;
        String published;
        public boolean declared(String name) { return declared; }
        public boolean exists(String name) { return exists; }
        public void add(String name, IBinder binder) throws Exception {
            if (block != null) block.await();
            if (failAdd) throw new IllegalArgumentException("injected");
            adds++; published = name;
        }
        public boolean ready(String name, IBinder binder) { return ready && name.equals(published); }
    }
    private static void failure(Backend b, long timeout) throws Exception {
        try (RadioCanaryRegistration r = new RadioCanaryRegistration(b, TOKEN, new Binder(), timeout)) {
            try { r.start(); throw new AssertionError("false success"); }
            catch (RadioCanaryRegistration.StartFailure expected) {
                check(r.mustExitProcess() && !r.isReady(), "failure not closed");
            }
        }
    }
    public static void main(String[] args) throws Exception {
        Backend ok = new Backend(); RadioCanaryRegistration reg = new RadioCanaryRegistration(ok, TOKEN, new Binder(), 1000);
        check(!reg.isReady(), "premature ready"); reg.start();
        check(reg.isReady() && ok.adds == 1, "missing successful publication");
        check(ok.published.equals("android.hardware.radio.config.IRadioConfig/rme" + TOKEN), "wrong name");
        try { reg.start(); throw new AssertionError("repeated start"); } catch (IllegalStateException expected) { }
        reg.close(); check(reg.mustExitProcess() && !reg.isReady(), "close cannot unregister");
        Backend absent = new Backend(); absent.declared = false; failure(absent, 1000); check(absent.adds == 0, "undeclared published");
        Backend collision = new Backend(); collision.exists = true; failure(collision, 1000); check(collision.adds == 0, "collision published");
        Backend denied = new Backend(); denied.failAdd = true; failure(denied, 1000);
        Backend unready = new Backend(); unready.ready = false; failure(unready, 100);
        Backend blocked = new Backend(); blocked.block = new CountDownLatch(1); long started = System.nanoTime();
        failure(blocked, 100); check(TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - started) < 3000, "unbounded"); blocked.block.countDown();
        for (String token : new String[]{"default", "slot1", "../escape", "0123456789ABCDEF0123456789ABCDEF"}) {
            try { new RadioCanaryRegistration(new Backend(), token, new Binder(), 1000); throw new AssertionError("bad token"); }
            catch (IllegalArgumentException expected) { }
        }
        System.out.println("RADIO_CANARY_REGISTRATION_OK");
    }
}
