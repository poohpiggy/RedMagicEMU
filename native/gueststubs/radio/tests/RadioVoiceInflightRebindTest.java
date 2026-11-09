package app.aemu.stub;

import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/** A retired callback must stay death-linked while a call to that callback is in flight. */
public final class RadioVoiceInflightRebindTest {
    private static IBinder peer(AtomicInteger unlinks) {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
                (p, m, a) -> {
                    switch (m.getName()) {
                        case "linkToDeath": return null;
                        case "unlinkToDeath": unlinks.incrementAndGet(); return true;
                        case "isBinderAlive": return true;
                        case "hashCode": return System.identityHashCode(p);
                        case "equals": return p == a[0];
                        default: throw new UnsupportedOperationException(m.getName());
                    }
                });
    }
    private static IRadioVoiceResponse response(IBinder binder, CountDownLatch entered, CountDownLatch release) {
        return (IRadioVoiceResponse) Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceResponse.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return binder;
                    if (m.getName().equals("getCurrentCallsResponse") && entered != null) {
                        entered.countDown(); release.await();
                    }
                    return null;
                });
    }
    private static IRadioVoiceIndication indication(IBinder binder) {
        return (IRadioVoiceIndication) Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceIndication.class}, (p, m, a) -> m.getName().equals("asBinder") ? binder : null);
    }
    public static void main(String[] args) throws Exception {
        AtomicInteger oldResponseUnlinks = new AtomicInteger(), oldIndicationUnlinks = new AtomicInteger();
        CountDownLatch entered = new CountDownLatch(1), release = new CountDownLatch(1), rebindStarted = new CountDownLatch(1);
        RadioVoiceService service = new RadioVoiceService("slot1", 4, 1500);
        service.setResponseFunctions(response(peer(oldResponseUnlinks), entered, release), indication(peer(oldIndicationUnlinks)));
        Thread request = new Thread(() -> { try { service.getCurrentCalls(43); } catch (Exception ignored) {} });
        Thread rebind = new Thread(() -> {
            rebindStarted.countDown();
            try { service.setResponseFunctions(response(peer(new AtomicInteger()), null, null), indication(peer(new AtomicInteger()))); }
            catch (Exception ignored) { /* bounded rebind may time out while old callback drains */ }
        });
        boolean prematurelyUnlinked;
        try {
            request.start();
            if (!entered.await(2, TimeUnit.SECONDS)) throw new AssertionError("old callback not entered");
            rebind.start();
            if (!rebindStarted.await(2, TimeUnit.SECONDS)) throw new AssertionError("rebind did not start");
            rebind.join(500);
            prematurelyUnlinked = oldResponseUnlinks.get() != 0 || oldIndicationUnlinks.get() != 0;
        } finally {
            release.countDown(); request.join(3000); rebind.join(3000);
        }
        if (request.isAlive() || rebind.isAlive()) throw new AssertionError("test threads did not stop");
        service.close();
        if (prematurelyUnlinked) throw new AssertionError("retired callback unlinked while its response was in flight");
        if (oldResponseUnlinks.get() == 0 || oldIndicationUnlinks.get() == 0)
            throw new AssertionError("retired callbacks were not detached after response finished");
        System.out.println("RADIO_VOICE_INFLIGHT_REBIND_OK");
    }
}
