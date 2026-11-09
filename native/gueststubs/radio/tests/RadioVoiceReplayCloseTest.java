package app.aemu.stub;

import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** Successful close must not leave a rebinding state replay in flight. */
public final class RadioVoiceReplayCloseTest {
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

    private static IRadioVoiceResponse response() {
        IBinder binder = peer();
        return (IRadioVoiceResponse) Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceResponse.class}, (p, m, a) -> m.getName().equals("asBinder") ? binder : null);
    }

    private static IRadioVoiceIndication indication(CountDownLatch entered, CountDownLatch release) {
        IBinder binder = peer();
        return (IRadioVoiceIndication) Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceIndication.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return binder;
                    if (m.getName().equals("callStateChanged") && entered != null) {
                        entered.countDown();
                        release.await();
                    }
                    return null;
                });
    }

    public static void main(String[] args) throws Exception {
        RadioVoiceService service = new RadioVoiceService("slot1", 4, 500);
        CountDownLatch replayEntered = new CountDownLatch(1), replayRelease = new CountDownLatch(1);
        CountDownLatch closeFinished = new CountDownLatch(1);
        AtomicReference<Throwable> closeFailure = new AtomicReference<>();
        AtomicReference<Throwable> replayFailure = new AtomicReference<>();
        service.setResponseFunctions(response(), indication(null, null));
        if (!service.injectIncoming("10086")) throw new AssertionError("incoming call was not established");
        Thread replay = new Thread(() -> {
            try { service.setResponseFunctions(response(), indication(replayEntered, replayRelease)); }
            catch (Throwable error) { replayFailure.set(error); }
        });
        Thread closing = new Thread(() -> {
            try { service.close(); }
            catch (Throwable error) { closeFailure.set(error); }
            finally { closeFinished.countDown(); }
        });
        boolean finishedWhileBlocked = false;
        try {
            replay.start();
            if (!replayEntered.await(2, TimeUnit.SECONDS)) throw new AssertionError("replay did not enter callback");
            closing.start();
            finishedWhileBlocked = closeFinished.await(2, TimeUnit.SECONDS);
        } finally {
            replayRelease.countDown();
            replay.join(3000); closing.join(3000);
        }
        if (replay.isAlive() || closing.isAlive()) throw new AssertionError("test threads did not stop");
        if (!finishedWhileBlocked) throw new AssertionError("close did not report its timeout");
        if (!(closeFailure.get() instanceof IllegalStateException))
            throw new AssertionError("close claimed success while replay was still in flight", closeFailure.get());
        service.close();
        System.out.println("RADIO_VOICE_REPLAY_CLOSE_OK");
    }
}
