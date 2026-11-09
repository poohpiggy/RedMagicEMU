package app.aemu.stub;

import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** A new framework peer must hear about a queued power-off call removal. */
public final class RadioVoicePowerRebindTest {
    static IBinder binder() {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class}, (p,m,a) -> {
            if (m.getName().equals("unlinkToDeath") || m.getName().equals("isBinderAlive")) return true;
            if (m.getName().equals("hashCode")) return System.identityHashCode(p);
            if (m.getName().equals("equals")) return p == a[0];
            return null;
        });
    }
    static IRadioVoiceResponse response() {
        IBinder b = binder();
        return (IRadioVoiceResponse) Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(), new Class<?>[]{IRadioVoiceResponse.class},
            (p,m,a) -> m.getName().equals("asBinder") ? b : null);
    }
    public static void main(String[] args) throws Exception {
        RadioVoicePowerLossTest.ReadySlot slot = new RadioVoicePowerLossTest.ReadySlot(); slot.setRadioPower(true);
        RadioVoiceService voice = new RadioVoiceService("slot1", 8, 3000, slot);
        CountDownLatch entered = new CountDownLatch(1), release = new CountDownLatch(1), newState = new CountDownLatch(1);
        IBinder oldBinder = binder(), newBinder = binder();
        IRadioVoiceIndication oldInd = (IRadioVoiceIndication) Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(), new Class<?>[]{IRadioVoiceIndication.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return oldBinder;
            if (m.getName().equals("callStateChanged")) { entered.countDown(); release.await(); }
            return null;
        });
        IRadioVoiceIndication newInd = (IRadioVoiceIndication) Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(), new Class<?>[]{IRadioVoiceIndication.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return newBinder;
            if (m.getName().equals("callStateChanged")) newState.countDown();
            return null;
        });
        AtomicReference<Throwable> incomingFailure = new AtomicReference<>();
        Thread incoming = new Thread(() -> { try { voice.injectIncoming("10086"); } catch (Throwable e) { incomingFailure.set(e); } });
        try {
            voice.setResponseFunctions(response(), oldInd); incoming.start();
            if (!entered.await(2, TimeUnit.SECONDS)) throw new AssertionError("old call callback did not start");
            slot.setRadioPower(false);
            voice.setResponseFunctions(response(), newInd);
            release.countDown(); incoming.join(3000);
            if (incoming.isAlive() || !(incomingFailure.get() instanceof RemoteException)) throw new AssertionError("interrupted call did not report failure", incomingFailure.get());
            if (!newState.await(2, TimeUnit.SECONDS)) throw new AssertionError("new callback missed call removal");
        } finally { release.countDown(); incoming.join(3000); voice.close(); }
        System.out.println("RADIO_VOICE_POWER_REBIND_OK");
    }
}
