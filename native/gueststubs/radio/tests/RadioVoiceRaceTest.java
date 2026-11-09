package app.aemu.stub;

import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import java.lang.reflect.Proxy;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** Forces incoming and hangup to overlap; stale ringing must not follow a successful hangup. */
public final class RadioVoiceRaceTest {
    private static IBinder peer() {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class}, (p,m,a) -> {
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
    public static void main(String[] args) throws Exception {
        List<String> events = new CopyOnWriteArrayList<>();
        CountDownLatch entered = new CountDownLatch(1), release = new CountDownLatch(1);
        IBinder rb = peer(), ib = peer();
        IRadioVoiceResponse response = (IRadioVoiceResponse) Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceResponse.class}, (p,m,a) -> {
                    if (m.getName().equals("asBinder")) return rb;
                    events.add("response:" + m.getName()); return null;
                });
        IRadioVoiceIndication indication = (IRadioVoiceIndication) Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceIndication.class}, (p,m,a) -> {
                    if (m.getName().equals("asBinder")) return ib;
                    if (m.getName().equals("callStateChanged") && entered.getCount() != 0) {
                        events.add("incoming-state-enter"); entered.countDown();
                        if (!release.await(3, TimeUnit.SECONDS)) throw new IllegalStateException("synthetic timeout");
                    } else events.add("indication:" + m.getName());
                    return null;
                });
        RadioVoiceService service = new RadioVoiceService("slot1");
        service.setResponseFunctions(response, indication);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread incoming = new Thread(() -> { try { service.injectIncoming("+10000000000"); } catch (Throwable e) { failure.set(e); } });
        Thread hangup = new Thread(() -> { try { service.hangup(7, 1); } catch (Throwable e) { failure.set(e); } });
        incoming.start();
        if (!entered.await(3, TimeUnit.SECONDS)) throw new AssertionError("incoming indication did not start");
        hangup.start();
        Thread.sleep(100);
        release.countDown();
        incoming.join(3000); hangup.join(3000);
        if (incoming.isAlive() || hangup.isAlive() || failure.get() != null) throw new AssertionError("test did not complete", failure.get());
        int ring = events.indexOf("indication:callRing"), hungUp = events.indexOf("response:hangupConnectionResponse");
        if (ring < 0 || hungUp < 0 || ring > hungUp) throw new AssertionError("stale ring after hangup: " + events);
        System.out.println("RADIO_VOICE_RACE_OK");
    }
}
