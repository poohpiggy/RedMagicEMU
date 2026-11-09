package app.aemu.stub;

import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** Synthetic death between the two incoming-call indications. */
public final class RadioVoiceDeathRaceTest {
    private static IBinder peer(AtomicReference<IBinder.DeathRecipient> death) {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class}, (p, m, a) -> {
            switch (m.getName()) {
                case "linkToDeath": if (death != null) death.set((IBinder.DeathRecipient) a[0]); return null;
                case "unlinkToDeath": return true;
                case "isBinderAlive": return true;
                case "hashCode": return System.identityHashCode(p);
                case "equals": return p == a[0];
                default: return null;
            }
        });
    }
    public static void main(String[] args) throws Exception {
        AtomicReference<IBinder.DeathRecipient> death = new AtomicReference<>();
        CountDownLatch entered = new CountDownLatch(1), release = new CountDownLatch(1);
        AtomicInteger rings = new AtomicInteger(); AtomicReference<Object> result = new AtomicReference<>();
        IBinder responseBinder = peer(death), indicationBinder = peer(null);
        IRadioVoiceResponse response = (IRadioVoiceResponse) Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),
            new Class<?>[]{IRadioVoiceResponse.class}, (p, m, a) -> m.getName().equals("asBinder") ? responseBinder : null);
        IRadioVoiceIndication indication = (IRadioVoiceIndication) Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),
            new Class<?>[]{IRadioVoiceIndication.class}, (p, m, a) -> {
                if (m.getName().equals("asBinder")) return indicationBinder;
                if (m.getName().equals("callStateChanged")) { entered.countDown(); release.await(); }
                if (m.getName().equals("callRing")) rings.incrementAndGet();
                return null;
            });
        RadioVoiceService service = new RadioVoiceService("slot1", 2, 3000);
        service.setResponseFunctions(response, indication);
        Thread incoming = new Thread(() -> { try { result.set(service.injectIncoming("10086")); }
                                             catch (Throwable error) { result.set(error); } });
        try {
            incoming.start();
            if (!entered.await(2, TimeUnit.SECONDS)) throw new AssertionError("callback not entered");
            death.get().binderDied(); release.countDown(); incoming.join(2000);
            if (incoming.isAlive() || rings.get() != 0 || !(result.get() instanceof RemoteException))
                throw new AssertionError("death during first indication did not stop ring and report uncertainty");
        } finally { release.countDown(); incoming.join(3000); service.close(); }
        System.out.println("RADIO_VOICE_DEATH_RACE_OK");
    }
}
