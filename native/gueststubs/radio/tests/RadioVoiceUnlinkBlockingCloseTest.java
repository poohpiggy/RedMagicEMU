package app.aemu.stub;

import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** A blocked local unlink must not make close wait indefinitely for the state lock. */
public final class RadioVoiceUnlinkBlockingCloseTest {
    private static IBinder peer(CountDownLatch entered, CountDownLatch release) {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
                (p, m, a) -> {
                    switch (m.getName()) {
                        case "linkToDeath": return null;
                        case "unlinkToDeath":
                            if (entered != null) { entered.countDown(); release.await(); }
                            return true;
                        case "isBinderAlive": return true;
                        case "hashCode": return System.identityHashCode(p);
                        case "equals": return p == a[0];
                        default: throw new UnsupportedOperationException(m.getName());
                    }
                });
    }

    private static IRadioVoiceResponse response(IBinder binder) {
        return (IRadioVoiceResponse) Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceResponse.class}, (p, m, a) -> m.getName().equals("asBinder") ? binder : null);
    }

    private static IRadioVoiceIndication indication(IBinder binder) {
        return (IRadioVoiceIndication) Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceIndication.class}, (p, m, a) -> m.getName().equals("asBinder") ? binder : null);
    }

    public static void main(String[] args) throws Exception {
        RadioVoiceService service = new RadioVoiceService("slot1", 4, 300);
        CountDownLatch entered = new CountDownLatch(1), release = new CountDownLatch(1);
        CountDownLatch closeFinished = new CountDownLatch(1);
        AtomicReference<Throwable> closeFailure = new AtomicReference<>();
        service.setResponseFunctions(response(peer(entered, release)), indication(peer(null, null)));
        Thread rebinding = new Thread(() -> {
            try { service.setResponseFunctions(response(peer(null, null)), indication(peer(null, null))); }
            catch (Exception ignored) { /* shutdown can invalidate this binding */ }
        });
        Thread closing = new Thread(() -> {
            try { service.close(); }
            catch (Throwable error) { closeFailure.set(error); }
            finally { closeFinished.countDown(); }
        });
        boolean finishedWhileBlocked = false;
        try {
            rebinding.start();
            if (!entered.await(2, TimeUnit.SECONDS)) throw new AssertionError("unlink did not block");
            closing.start();
            finishedWhileBlocked = closeFinished.await(2, TimeUnit.SECONDS);
        } finally {
            release.countDown(); rebinding.join(3000); closing.join(3000);
        }
        if (rebinding.isAlive() || closing.isAlive()) throw new AssertionError("test threads did not stop");
        if (!finishedWhileBlocked) throw new AssertionError("close was blocked past deadline by unlink");
        if (!(closeFailure.get() instanceof IllegalStateException))
            throw new AssertionError("close did not report blocked unlink", closeFailure.get());
        service.close();
        System.out.println("RADIO_VOICE_UNLINK_BLOCKING_CLOSE_OK");
    }
}
