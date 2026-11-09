package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import android.os.RemoteException;

import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** Deterministic timeout/close tests using synthetic callbacks; no service is registered. */
public final class RadioVoiceTimeoutCloseTest {
    private static IBinder peer() {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
                (p, m, a) -> {
                    switch (m.getName()) {
                        case "linkToDeath": return null;
                        case "unlinkToDeath": return true;
                        case "isBinderAlive": return true;
                        case "toString": return "synthetic-peer";
                        case "hashCode": return System.identityHashCode(p);
                        case "equals": return p == a[0];
                        default: throw new UnsupportedOperationException(m.getName());
                    }
                });
    }

    private static final class Fixture {
        final CountDownLatch entered = new CountDownLatch(1);
        final CountDownLatch release = new CountDownLatch(1);
        final AtomicInteger replies = new AtomicInteger();
        final AtomicInteger rings = new AtomicInteger();
        final AtomicReference<RadioResponseInfo> info = new AtomicReference<>();
        final IBinder responseBinder = peer(), indicationBinder = peer();
        final IRadioVoiceResponse response = (IRadioVoiceResponse) Proxy.newProxyInstance(
                IRadioVoiceResponse.class.getClassLoader(), new Class<?>[]{IRadioVoiceResponse.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return responseBinder;
                    if (m.getName().equals("getCurrentCallsResponse")) {
                        info.set((RadioResponseInfo) a[0]); replies.incrementAndGet();
                    }
                    return null;
                });
        final IRadioVoiceIndication indication = (IRadioVoiceIndication) Proxy.newProxyInstance(
                IRadioVoiceIndication.class.getClassLoader(), new Class<?>[]{IRadioVoiceIndication.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return indicationBinder;
                    if (m.getName().equals("callStateChanged")) {
                        entered.countDown();
                        // Simulate an IPC call that does not finish merely because the caller is interrupted.
                        boolean waiting = true;
                        while (waiting) {
                            try { release.await(); waiting = false; }
                            catch (InterruptedException ignored) { /* wait for explicit release */ }
                        }
                    }
                    if (m.getName().equals("callRing")) rings.incrementAndGet();
                    return null;
                });
    }

    private static void timeout() throws Exception {
        Fixture f = new Fixture(); RadioVoiceService service = new RadioVoiceService("slot1", 4, 200);
        service.setResponseFunctions(f.response, f.indication);
        Thread incoming = new Thread(() -> { try { service.injectIncoming("+10000000000"); } catch (RemoteException ignored) {} });
        try {
            incoming.start();
            if (!f.entered.await(2, TimeUnit.SECONDS)) throw new AssertionError("incoming callback not reached");
            try { service.getCurrentCalls(123); } catch (RemoteException ignored) { /* oneway errors aren't a reply */ }
            f.release.countDown(); incoming.join(3000); Thread.sleep(100);
            RadioResponseInfo result = f.info.get();
            if (f.replies.get() != 1 || result == null || result.serial != 123 ||
                    (result.error != RadioError.NONE && result.error != RadioError.NO_RESOURCES))
                throw new AssertionError("timed-out oneway request lacks exactly one explicit response");
        } finally {
            f.release.countDown(); incoming.join(3000); service.close();
        }
        System.out.println("RADIO_VOICE_TIMEOUT_OK");
    }

    private static void close() throws Exception {
        Fixture f = new Fixture(); RadioVoiceService service = new RadioVoiceService("slot1", 4, 2000);
        service.setResponseFunctions(f.response, f.indication);
        AtomicReference<Boolean> accepted = new AtomicReference<>();
        Thread incoming = new Thread(() -> { try { accepted.set(service.injectIncoming("+10000000000")); }
                                             catch (RemoteException ignored) {} });
        boolean cleanClose = true;
        try {
            incoming.start();
            if (!f.entered.await(2, TimeUnit.SECONDS)) throw new AssertionError("incoming callback not reached");
            try { service.close(); } catch (IllegalStateException expected) { cleanClose = false; }
            try {
                service.setResponseFunctions(f.response, f.indication);
                throw new AssertionError("service accepted a new binding after shutdown began");
            } catch (RemoteException expected) { /* shutdown is irreversible even if waiting timed out */ }
            f.release.countDown(); incoming.join(3000);
            if (incoming.isAlive()) throw new AssertionError("incoming did not terminate after release");
            if (cleanClose && (f.rings.get() != 0 || Boolean.TRUE.equals(accepted.get())))
                throw new AssertionError("successful close allowed a later ring or successful injection");
        } finally {
            f.release.countDown(); incoming.join(3000);
            try { service.close(); } catch (IllegalStateException expected) { /* explicitly not a clean stop */ }
        }
        System.out.println("RADIO_VOICE_CLOSE_OK");
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("mode required");
        if (args[0].equals("timeout")) timeout();
        else if (args[0].equals("close")) close();
        else throw new IllegalArgumentException("unknown mode");
    }
}
