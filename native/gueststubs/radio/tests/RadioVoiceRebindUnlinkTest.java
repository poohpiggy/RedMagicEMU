package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.voice.Call;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** A failed old-peer detach must not silently leave a published callback unsynchronized. */
public final class RadioVoiceRebindUnlinkTest {
    private static IBinder peer(boolean failUnlink, AtomicInteger unlinks) {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
                (p, m, a) -> {
                    switch (m.getName()) {
                        case "linkToDeath": return null;
                        case "unlinkToDeath":
                            int attempt = unlinks.incrementAndGet();
                            if (failUnlink && attempt == 1) throw new IllegalStateException("synthetic unlink failure");
                            return true;
                        case "isBinderAlive": return true;
                        case "hashCode": return System.identityHashCode(p);
                        case "equals": return p == a[0];
                        default: throw new UnsupportedOperationException(m.getName());
                    }
                });
    }

    private static IRadioVoiceResponse response(IBinder binder, AtomicReference<RadioResponseInfo> info,
            AtomicReference<Call[]> calls, CountDownLatch replied) {
        return (IRadioVoiceResponse) Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceResponse.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return binder;
                    if (m.getName().equals("getCurrentCallsResponse")) {
                        info.set((RadioResponseInfo) a[0]); calls.set((Call[]) a[1]); replied.countDown();
                    }
                    return null;
                });
    }

    private static IRadioVoiceIndication indication(IBinder binder, CountDownLatch stateChanged) {
        return (IRadioVoiceIndication) Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceIndication.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return binder;
                    if (m.getName().equals("callStateChanged")) stateChanged.countDown();
                    return null;
                });
    }

    public static void main(String[] args) throws Exception {
        AtomicInteger oldResponseUnlinks = new AtomicInteger(), oldIndicationUnlinks = new AtomicInteger();
        CountDownLatch newState = new CountDownLatch(1), newReply = new CountDownLatch(1);
        AtomicReference<RadioResponseInfo> info = new AtomicReference<>();
        AtomicReference<Call[]> calls = new AtomicReference<>();
        RadioVoiceService service = new RadioVoiceService("slot1", 4, 2000);
        try {
            service.setResponseFunctions(
                    response(peer(true, oldResponseUnlinks), new AtomicReference<>(), new AtomicReference<>(), new CountDownLatch(1)),
                    indication(peer(false, oldIndicationUnlinks), new CountDownLatch(1)));
            if (!service.injectIncoming("10086")) throw new AssertionError("incoming call was not established");
            Throwable diagnostic = null;
            try {
                service.setResponseFunctions(response(peer(false, new AtomicInteger()), info, calls, newReply),
                        indication(peer(false, new AtomicInteger()), newState));
            } catch (Throwable error) { diagnostic = error; }
            service.getCurrentCalls(42);
            if (!newReply.await(2, TimeUnit.SECONDS)) throw new AssertionError("new callback was not published");
            RadioResponseInfo result = info.get(); Call[] current = calls.get();
            if (result == null || result.serial != 42 || result.error != RadioError.NONE || current == null ||
                    current.length != 1 || current[0].state != Call.STATE_INCOMING || !"10086".equals(current[0].number))
                throw new AssertionError("incoming state was not preserved");
            if (!(diagnostic instanceof RuntimeException))
                throw new AssertionError("old detach failure was not exposed as a diagnostic", diagnostic);
            if (oldResponseUnlinks.get() != 1 || oldIndicationUnlinks.get() != 1)
                throw new AssertionError("both old callbacks must be detached even when the first detach fails");
            if (!newState.await(2, TimeUnit.SECONDS))
                throw new AssertionError("published callback did not receive preserved state notification");
        } finally {
            service.close();
        }
        System.out.println("RADIO_VOICE_REBIND_UNLINK_OK");
    }
}
