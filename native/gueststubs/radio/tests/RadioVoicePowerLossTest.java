package app.aemu.stub;

import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.voice.Call;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.Proxy;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** Isolated regression: a virtual call must not outlive its single radio session. */
public final class RadioVoicePowerLossTest {
    static final class ReadySlot extends SyntheticSingleSlot {
        int listenerCount;
        @Override synchronized boolean isNetworkRegistered() { return true; }
        @Override synchronized boolean isSimReady() { return true; }
        @Override synchronized void addDeliveryListener(Runnable listener) { super.addDeliveryListener(listener); listenerCount++; }
        @Override synchronized void removeDeliveryListener(Runnable listener) { super.removeDeliveryListener(listener); listenerCount--; }
    }
    static IBinder binder() {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
            (p, m, a) -> {
                if (m.getName().equals("unlinkToDeath")) return true;
                if (m.getName().equals("isBinderAlive")) return true;
                if (m.getName().equals("hashCode")) return System.identityHashCode(p);
                if (m.getName().equals("equals")) return p == a[0];
                return null;
            });
    }
    static void check(boolean condition, String reason) { if (!condition) throw new AssertionError(reason); }
    public static void main(String[] args) throws Exception {
        ReadySlot slot = new ReadySlot(); slot.setRadioPower(true);
        RadioVoiceService voice = new RadioVoiceService("slot1", 16, 2000, slot);
        IBinder rb = binder(), ib = binder();
        AtomicReference<Call[]> calls = new AtomicReference<>();
        AtomicReference<RadioResponseInfo> info = new AtomicReference<>();
        AtomicInteger changes = new AtomicInteger();
        AtomicInteger rings = new AtomicInteger();
        AtomicBoolean interruptIncoming = new AtomicBoolean();
        IRadioVoiceResponse response = (IRadioVoiceResponse) Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),
            new Class<?>[]{IRadioVoiceResponse.class}, (p, m, a) -> {
                if (m.getName().equals("asBinder")) return rb;
                if (m.getName().equals("getCurrentCallsResponse")) { info.set((RadioResponseInfo)a[0]); calls.set((Call[])a[1]); }
                return null;
            });
        IRadioVoiceIndication indication = (IRadioVoiceIndication) Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),
            new Class<?>[]{IRadioVoiceIndication.class}, (p, m, a) -> {
                if (m.getName().equals("asBinder")) return ib;
                if (m.getName().equals("callStateChanged")) {
                    changes.incrementAndGet();
                    if (interruptIncoming.compareAndSet(true, false)) {
                        slot.setRadioPower(false); slot.setRadioPower(true);
                    }
                }
                if (m.getName().equals("callRing")) rings.incrementAndGet();
                return null;
            });
        try {
            voice.setResponseFunctions(response, indication);
            check(voice.injectIncoming("+10000000000"), "incoming rejected");
            voice.getCurrentCalls(1); check(calls.get().length == 1, "incoming absent");
            int before = changes.get(); slot.setRadioPower(false);
            voice.getCurrentCalls(2);
            check(calls.get().length == 0, "call remained after power off");
            check(changes.get() > before, "no call state notification after power off");
            slot.setRadioPower(true); voice.getCurrentCalls(3);
            check(calls.get().length == 0, "call revived after power cycle");
            int previousRings = rings.get(); interruptIncoming.set(true);
            try { voice.injectIncoming("+10000000000"); throw new AssertionError("interrupted incoming succeeded"); }
            catch (RemoteException expected) { /* The state change crossed the call notification. */ }
            voice.getCurrentCalls(5);
            check(calls.get().length == 0, "interrupted call remained after power cycle");
            check(rings.get() == previousRings, "ring delivered after power interruption");
            check(voice.injectIncoming("+10000000000"), "second incoming rejected");
            before = changes.get(); slot.disconnect(); voice.getCurrentCalls(4);
            check(calls.get().length == 0, "call remained after disconnect");
            check(changes.get() > before, "no call state notification after disconnect");
            System.out.println("RADIO_VOICE_POWER_LOSS_OK");
        } finally { voice.close(); }
        check(slot.listenerCount == 0, "voice listener remained registered after close");
    }
}
