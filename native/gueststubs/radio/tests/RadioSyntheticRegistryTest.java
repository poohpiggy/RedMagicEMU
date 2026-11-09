package app.aemu.stub;

import android.os.Binder;
import android.os.IBinder;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Pure JVM contract for a test-only service registry; it never publishes a real Radio name. */
public final class RadioSyntheticRegistryTest {
    private static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    private static final String TOKEN = "0123456789abcdef0123456789abcdef";

    private static final class Backend implements RadioSyntheticRegistry.Backend {
        final Map<String, IBinder> entries = new LinkedHashMap<>();
        int adds; int failAddAt = -1; boolean notReady; CountDownLatch blocked;
        @Override public boolean exists(String name) { return entries.containsKey(name); }
        @Override public void add(String name, IBinder binder) throws Exception {
            if (blocked != null) blocked.await();
            if (++adds == failAddAt) throw new Exception("injected registration failure");
            if (entries.put(name, binder) != null) throw new AssertionError("overwrote registration");
        }
        @Override public boolean ready(String name, IBinder expected) {
            return !notReady && entries.get(name) == expected;
        }
    }

    private static IBinder[] binders() {
        IBinder[] result = new IBinder[7];
        for (int i = 0; i < result.length; i++) result[i] = new Binder();
        return result;
    }
    private static void expectFailure(Backend backend, String token, long timeout) throws Exception {
        try (RadioSyntheticRegistry registry = new RadioSyntheticRegistry(backend, token, binders(), timeout)) {
            try { registry.start(); throw new AssertionError("failure incorrectly reported ready"); }
            catch (RadioSyntheticRegistry.StartFailure expected) {
                check(registry.mustExitProcess(), "failure did not require process exit");
                check(!registry.isReady(), "failed registry reported ready");
            }
        }
    }
    public static void main(String[] args) throws Exception {
        Backend backend = new Backend();
        try (RadioSyntheticRegistry registry = new RadioSyntheticRegistry(backend, TOKEN, binders(), 1000)) {
            check(!registry.isReady(), "ready before registration");
            registry.start();
            check(registry.isReady() && backend.adds == 7, "not all services ready");
            for (String name : backend.entries.keySet()) {
                check(name.startsWith("app.redmagicemu.test.radio." + TOKEN + "."), "unexpected name");
                check(!name.contains("slot1") && !name.contains("slot2") && !name.startsWith("android."), "real service name");
            }
            try { registry.start(); throw new AssertionError("duplicate start accepted"); }
            catch (IllegalStateException expected) { }
        }
        Backend collision = new Backend();
        collision.entries.put("app.redmagicemu.test.radio." + TOKEN + ".voice", new Binder());
        expectFailure(collision, TOKEN, 1000);
        check(collision.adds == 0, "collision allowed partial publication");
        Backend partial = new Backend(); partial.failAddAt = 3;
        expectFailure(partial, TOKEN, 1000);
        check(partial.entries.size() == 2, "partial publication was disguised as cleanup");
        Backend unready = new Backend(); unready.notReady = true;
        expectFailure(unready, TOKEN, 1000);
        Backend timeout = new Backend(); timeout.blocked = new CountDownLatch(1);
        long started = System.nanoTime();
        expectFailure(timeout, TOKEN, 100);
        check(TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - started) < 3000, "registration timeout unbounded");
        timeout.blocked.countDown();
        try { new RadioSyntheticRegistry(new Backend(), "slot1", binders(), 1000); throw new AssertionError("bad token accepted"); }
        catch (IllegalArgumentException expected) { }
        System.out.println("RADIO_SYNTHETIC_REGISTRY_OK");
    }
}
