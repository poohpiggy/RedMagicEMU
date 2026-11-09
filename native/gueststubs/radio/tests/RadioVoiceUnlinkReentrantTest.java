package app.aemu.stub;

import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import java.lang.reflect.Proxy;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

/** A local Binder may synchronously reenter service binding during unlink. */
public final class RadioVoiceUnlinkReentrantTest {
    private static final AtomicReference<RadioVoiceService> SERVICE = new AtomicReference<>();
    private static final AtomicBoolean ENTERED = new AtomicBoolean();

    private static IBinder peer(boolean reenter) {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
                (p, m, a) -> {
                    switch (m.getName()) {
                        case "linkToDeath": return null;
                        case "unlinkToDeath":
                            if (reenter && ENTERED.compareAndSet(false, true))
                                SERVICE.get().setResponseFunctions(response(peer(false)), indication(peer(false)));
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
        RadioVoiceService service = new RadioVoiceService("slot1", 4, 1000);
        SERVICE.set(service);
        try {
            service.setResponseFunctions(response(peer(true)), indication(peer(false)));
            service.setResponseFunctions(response(peer(false)), indication(peer(false)));
            if (!ENTERED.get()) throw new AssertionError("unlink did not reenter binding");
        } finally {
            service.close(); SERVICE.set(null);
        }
        System.out.println("RADIO_VOICE_UNLINK_REENTRANT_OK");
    }
}
