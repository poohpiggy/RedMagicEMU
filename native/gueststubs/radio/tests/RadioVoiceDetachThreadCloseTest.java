package app.aemu.stub;

import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import java.lang.reflect.Field;
import java.lang.reflect.Proxy;

/** A successful close must terminate its owned detach worker. */
public final class RadioVoiceDetachThreadCloseTest {
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
        IBinder rb = peer(), ib = peer();
        IRadioVoiceResponse response = (IRadioVoiceResponse) Proxy.newProxyInstance(
                IRadioVoiceResponse.class.getClassLoader(), new Class<?>[]{IRadioVoiceResponse.class},
                (p, m, a) -> m.getName().equals("asBinder") ? rb : null);
        IRadioVoiceIndication indication = (IRadioVoiceIndication) Proxy.newProxyInstance(
                IRadioVoiceIndication.class.getClassLoader(), new Class<?>[]{IRadioVoiceIndication.class},
                (p, m, a) -> m.getName().equals("asBinder") ? ib : null);
        RadioVoiceService service = new RadioVoiceService("slot1", 4, 1000);
        service.setResponseFunctions(response, indication);
        Field field = RadioVoiceService.class.getDeclaredField("detachThread"); field.setAccessible(true);
        Thread worker = (Thread) field.get(service);
        if (worker == null || !worker.isAlive()) throw new AssertionError("detach worker did not start");
        service.close();
        worker.join(1500);
        if (worker.isAlive()) throw new AssertionError("close returned while detach worker remains alive");
        service.close();
        System.out.println("RADIO_VOICE_DETACH_THREAD_CLOSE_OK");
    }
}
