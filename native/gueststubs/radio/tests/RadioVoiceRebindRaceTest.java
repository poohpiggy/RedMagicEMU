package app.aemu.stub;

import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import android.os.RemoteException;

import java.lang.reflect.Field;
import java.lang.reflect.Proxy;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** A queued one-way rebind must not send a later request's serial to the retired callback. */
public final class RadioVoiceRebindRaceTest {
    private static IBinder peer() {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
                (p, m, a) -> {
                    switch (m.getName()) {
                        case "linkToDeath": return null;
                        case "unlinkToDeath": return true;
                        case "isBinderAlive": return true;
                        case "toString": return "synthetic-peer";
                        case "hashCode": return System.identityHashCode(p);
                        case "equals": return p == a[0];
                        default: throw new UnsupportedOperationException(m.getName());
                    }
                });
    }

    private static final class Fixture {
        final CountDownLatch entered = new CountDownLatch(1), release = new CountDownLatch(1);
        final Queue<Integer> oldSerials = new ConcurrentLinkedQueue<>(), newSerials = new ConcurrentLinkedQueue<>();
        final IBinder oldResponseBinder = peer(), oldIndicationBinder = peer();
        final IBinder newResponseBinder = peer(), newIndicationBinder = peer();
        final IRadioVoiceResponse oldResponse = response(oldResponseBinder, oldSerials);
        final IRadioVoiceResponse newResponse = response(newResponseBinder, newSerials);
        final IRadioVoiceIndication oldIndication = indication(oldIndicationBinder, true);
        final IRadioVoiceIndication newIndication = indication(newIndicationBinder, false);

        IRadioVoiceResponse response(IBinder binder, Queue<Integer> serials) {
            return (IRadioVoiceResponse) Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),
                    new Class<?>[]{IRadioVoiceResponse.class}, (p, m, a) -> {
                        if (m.getName().equals("asBinder")) return binder;
                        if (m.getName().equals("getMuteResponse") || m.getName().equals("getCurrentCallsResponse")) {
                            RadioResponseInfo info = (RadioResponseInfo) a[0];
                            if (info == null) throw new AssertionError("missing response info");
                            serials.add(info.serial);
                        }
                        return null;
                    });
        }

        IRadioVoiceIndication indication(IBinder binder, boolean block) {
            return (IRadioVoiceIndication) Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),
                    new Class<?>[]{IRadioVoiceIndication.class}, (p, m, a) -> {
                        if (m.getName().equals("asBinder")) return binder;
                        if (block && m.getName().equals("callStateChanged")) {
                            entered.countDown();
                            boolean waiting = true;
                            while (waiting) {
                                try { release.await(); waiting = false; }
                                catch (InterruptedException ignored) { /* explicit release only */ }
                            }
                        }
                        return null;
                    });
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1 || !(args[0].equals("direct") || args[0].equals("full")))
            throw new IllegalArgumentException("direct or full mode required");
        Fixture f = new Fixture(); RadioVoiceService service = new RadioVoiceService("slot1", 1, 250);
        service.setResponseFunctions(f.oldResponse, f.oldIndication);
        Thread incoming = new Thread(() -> { try { service.injectIncoming("+10000000000"); }
                                            catch (RemoteException ignored) {} });
        AtomicReference<Throwable> rebindResult = new AtomicReference<>();
        Thread rebind = new Thread(() -> { try { service.setResponseFunctions(f.newResponse, f.newIndication); }
                                          catch (Throwable error) { rebindResult.set(error); } });
        Thread oldRequest = new Thread(() -> { try { service.getCurrentCalls(777); } catch (RemoteException ignored) {} });
        Thread newRequest = new Thread(() -> { try {
            if (args[0].equals("direct")) service.getMute(911);
            else service.getCurrentCalls(912);
        } catch (RemoteException ignored) {} });
        try {
            incoming.start();
            if (!f.entered.await(2, TimeUnit.SECONDS)) throw new AssertionError("worker not blocked");
            if (args[0].equals("full")) {
                oldRequest.start();
                Field field = RadioVoiceService.class.getDeclaredField("events"); field.setAccessible(true);
                ThreadPoolExecutor executor = (ThreadPoolExecutor) field.get(service);
                long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
                while (executor.getQueue().size() != 1 && System.nanoTime() < end) Thread.sleep(5);
                if (executor.getQueue().size() != 1) throw new AssertionError("ordinary request not queued");
            }
            rebind.start(); rebind.join(2000);
            if (rebind.isAlive()) throw new AssertionError("rebind did not complete or time out");
            if (rebindResult.get() != null && !(rebindResult.get() instanceof RemoteException))
                throw new AssertionError("unexpected rebind error", rebindResult.get());
            newRequest.start();
            // Give the request time to be dispatched while the original event remains blocked.
            Thread.sleep(350);
            f.release.countDown();
            incoming.join(3000); oldRequest.join(3000); newRequest.join(3000);
            if (incoming.isAlive() || oldRequest.isAlive() || newRequest.isAlive())
                throw new AssertionError("test worker did not terminate");
            int later = args[0].equals("direct") ? 911 : 912;
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
            while ((count(f.newSerials, later) == 0 ||
                    (args[0].equals("full") && count(f.oldSerials, 777) == 0)) && System.nanoTime() < deadline)
                Thread.sleep(5);
            if (f.oldSerials.contains(later)) throw new AssertionError("later serial sent to previous callback: " + later);
            if (count(f.newSerials, later) != 1) throw new AssertionError("new callback did not receive exactly one later response: " + later);
            if (args[0].equals("full") && (f.newSerials.contains(777) || count(f.oldSerials, 777) != 1))
                throw new AssertionError("old in-flight request was not returned to its original callback");
        } finally {
            f.release.countDown(); incoming.join(3000); rebind.join(3000); oldRequest.join(3000); newRequest.join(3000);
            try { service.close(); } catch (IllegalStateException ignored) { /* report as separate cleanup boundary */ }
        }
        System.out.println("RADIO_VOICE_REBIND_RACE_OK mode=" + args[0]);
    }

    private static int count(Queue<Integer> serials, int target) {
        int count = 0;
        for (Integer serial : serials) if (serial == target) count++;
        return count;
    }
}
