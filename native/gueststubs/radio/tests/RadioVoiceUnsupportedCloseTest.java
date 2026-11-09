package app.aemu.stub;

import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** An unsupported response still in flight must prevent close from claiming success. */
public final class RadioVoiceUnsupportedCloseTest {
    private static IBinder peer() {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
                (p, m, a) -> {
                    switch (m.getName()) {
                        case "linkToDeath": return null;
                        case "unlinkToDeath": return true;
                        case "isBinderAlive": return true;
                        case "hashCode": return System.identityHashCode(p);
                        case "equals": return p == a[0];
                        default: throw new UnsupportedOperationException(m.getName());
                    }
                });
    }
    public static void main(String[] args) throws Exception {
        CountDownLatch entered = new CountDownLatch(1), release = new CountDownLatch(1), closeFinished = new CountDownLatch(1);
        AtomicReference<Throwable> closeFailure = new AtomicReference<>();
        IBinder rb = peer(), ib = peer();
        IRadioVoiceResponse response = (IRadioVoiceResponse) Proxy.newProxyInstance(
                IRadioVoiceResponse.class.getClassLoader(), new Class<?>[]{IRadioVoiceResponse.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return rb;
                    if (m.getName().equals("getMuteResponse")) { entered.countDown(); release.await(); }
                    return null;
                });
        IRadioVoiceIndication indication = (IRadioVoiceIndication) Proxy.newProxyInstance(
                IRadioVoiceIndication.class.getClassLoader(), new Class<?>[]{IRadioVoiceIndication.class},
                (p, m, a) -> m.getName().equals("asBinder") ? ib : null);
        RadioVoiceService service = new RadioVoiceService("slot1", 4, 300);
        service.setResponseFunctions(response, indication);
        Thread request = new Thread(() -> { try { service.getMute(42); } catch (Exception ignored) {} });
        Thread closing = new Thread(() -> {
            try { service.close(); } catch (Throwable error) { closeFailure.set(error); }
            finally { closeFinished.countDown(); }
        });
        boolean finishedWhileBlocked;
        try {
            request.start();
            if (!entered.await(2, TimeUnit.SECONDS)) throw new AssertionError("unsupported callback not entered");
            closing.start();
            finishedWhileBlocked = closeFinished.await(2, TimeUnit.SECONDS);
        } finally {
            release.countDown(); request.join(3000); closing.join(3000);
        }
        if (request.isAlive() || closing.isAlive()) throw new AssertionError("test threads did not stop");
        if (!finishedWhileBlocked) throw new AssertionError("close did not report its timeout");
        if (!(closeFailure.get() instanceof IllegalStateException))
            throw new AssertionError("close claimed success while unsupported response was in flight", closeFailure.get());
        service.close();
        System.out.println("RADIO_VOICE_UNSUPPORTED_CLOSE_OK");
    }
}
