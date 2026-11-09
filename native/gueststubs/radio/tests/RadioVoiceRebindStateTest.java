package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.voice.Call;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** Rebinding during an incoming indication must resynchronize the new callback. */
public final class RadioVoiceRebindStateTest {
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

    private static IRadioVoiceResponse response(IBinder binder, AtomicReference<Call[]> calls,
            AtomicReference<RadioResponseInfo> info, CountDownLatch received) {
        return (IRadioVoiceResponse) Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceResponse.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return binder;
                    if (m.getName().equals("getCurrentCallsResponse")) {
                        info.set((RadioResponseInfo) a[0]); calls.set((Call[]) a[1]); received.countDown();
                    }
                    return null;
                });
    }

    public static void main(String[] args) throws Exception {
        CountDownLatch oldEntered = new CountDownLatch(1), oldRelease = new CountDownLatch(1);
        CountDownLatch newState = new CountDownLatch(1), newReply = new CountDownLatch(1);
        AtomicReference<Call[]> calls = new AtomicReference<>();
        AtomicReference<RadioResponseInfo> info = new AtomicReference<>();
        AtomicReference<Throwable> injection = new AtomicReference<>();
        IBinder oldIndBinder = peer(), newIndBinder = peer();
        IRadioVoiceResponse oldResponse = response(peer(), new AtomicReference<>(), new AtomicReference<>(), new CountDownLatch(1));
        IRadioVoiceResponse newResponse = response(peer(), calls, info, newReply);
        IRadioVoiceIndication oldIndication = (IRadioVoiceIndication) Proxy.newProxyInstance(
                IRadioVoiceIndication.class.getClassLoader(), new Class<?>[]{IRadioVoiceIndication.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return oldIndBinder;
                    if (m.getName().equals("callStateChanged")) { oldEntered.countDown(); oldRelease.await(); }
                    return null;
                });
        IRadioVoiceIndication newIndication = (IRadioVoiceIndication) Proxy.newProxyInstance(
                IRadioVoiceIndication.class.getClassLoader(), new Class<?>[]{IRadioVoiceIndication.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return newIndBinder;
                    if (m.getName().equals("callStateChanged")) newState.countDown();
                    return null;
                });
        RadioVoiceService service = new RadioVoiceService("slot1", 4, 3000);
        service.setResponseFunctions(oldResponse, oldIndication);
        Thread incoming = new Thread(() -> {
            try { boolean accepted = service.injectIncoming("10086");
                  injection.set(new AssertionError("injection returned instead of reporting partial delivery: " + accepted)); }
            catch (Throwable error) { injection.set(error); }
        });
        try {
            incoming.start();
            if (!oldEntered.await(2, TimeUnit.SECONDS)) throw new AssertionError("old callback did not start");
            service.setResponseFunctions(newResponse, newIndication);
            oldRelease.countDown(); incoming.join(3000);
            if (incoming.isAlive()) throw new AssertionError("injection did not finish");
            if (!(injection.get() instanceof RemoteException))
                throw new AssertionError("injection must report partial delivery", injection.get());
            service.getCurrentCalls(42);
            if (!newReply.await(2, TimeUnit.SECONDS)) throw new AssertionError("new callback got no calls response");
            RadioResponseInfo result = info.get(); Call[] current = calls.get();
            if (result == null || result.serial != 42 || result.error != RadioError.NONE || current == null ||
                    current.length != 1 || current[0].state != Call.STATE_INCOMING || !"10086".equals(current[0].number))
                throw new AssertionError("incoming state was not preserved across rebind");
            if (!newState.await(2, TimeUnit.SECONDS))
                throw new AssertionError("new callback was not notified of preserved incoming state");
        } finally {
            oldRelease.countDown(); incoming.join(3000);
            service.close();
        }
        System.out.println("RADIO_VOICE_REBIND_STATE_OK");
    }
}
