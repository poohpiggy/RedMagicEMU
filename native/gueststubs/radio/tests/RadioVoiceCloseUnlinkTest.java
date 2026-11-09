package app.aemu.stub;

import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import java.lang.reflect.Proxy;
import java.util.concurrent.atomic.AtomicInteger;

/** A close retry must not forget an unsuccessful death-recipient detach. */
public final class RadioVoiceCloseUnlinkTest {
    private static IBinder peer(boolean failFirst, AtomicInteger unlinks) {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
                (p, m, a) -> {
                    switch (m.getName()) {
                        case "linkToDeath": return null;
                        case "unlinkToDeath":
                            int attempt = unlinks.incrementAndGet();
                            if (failFirst && attempt == 1) throw new IllegalStateException("synthetic unlink failure");
                            return true;
                        case "isBinderAlive": return true;
                        case "hashCode": return System.identityHashCode(p);
                        case "equals": return p == a[0];
                        default: throw new UnsupportedOperationException(m.getName());
                    }
                });
    }

    public static void main(String[] args) throws Exception {
        AtomicInteger responseUnlinks = new AtomicInteger(), indicationUnlinks = new AtomicInteger();
        IBinder responseBinder = peer(true, responseUnlinks), indicationBinder = peer(false, indicationUnlinks);
        IRadioVoiceResponse response = (IRadioVoiceResponse) Proxy.newProxyInstance(
                IRadioVoiceResponse.class.getClassLoader(), new Class<?>[]{IRadioVoiceResponse.class},
                (p, m, a) -> m.getName().equals("asBinder") ? responseBinder : null);
        IRadioVoiceIndication indication = (IRadioVoiceIndication) Proxy.newProxyInstance(
                IRadioVoiceIndication.class.getClassLoader(), new Class<?>[]{IRadioVoiceIndication.class},
                (p, m, a) -> m.getName().equals("asBinder") ? indicationBinder : null);
        RadioVoiceService service = new RadioVoiceService("slot1", 4, 1000);
        service.setResponseFunctions(response, indication);
        Throwable firstFailure = null;
        try { service.close(); }
        catch (Throwable error) { firstFailure = error; }
        if (!(firstFailure instanceof RuntimeException))
            throw new AssertionError("first close must report the detach failure", firstFailure);
        if (responseUnlinks.get() != 1 || indicationUnlinks.get() != 1)
            throw new AssertionError("first close must attempt detaching both callbacks");
        Throwable retryFailure = null;
        try { service.close(); }
        catch (Throwable error) { retryFailure = error; }
        if (responseUnlinks.get() < 2 && retryFailure == null)
            throw new AssertionError("close retry reported success without retrying failed detach");
        if (retryFailure != null && !(retryFailure instanceof RuntimeException))
            throw new AssertionError("unexpected retry failure", retryFailure);
        System.out.println("RADIO_VOICE_CLOSE_UNLINK_OK");
    }
}
