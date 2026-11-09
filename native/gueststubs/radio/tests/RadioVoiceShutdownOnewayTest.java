package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.Field;
import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/** A live Binder client must receive a response while the service is draining. */
public final class RadioVoiceShutdownOnewayTest {
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

    public static void main(String[] args) throws Exception {
        CountDownLatch entered = new CountDownLatch(1), release = new CountDownLatch(1);
        CountDownLatch replied = new CountDownLatch(1);
        AtomicInteger replyCount = new AtomicInteger();
        AtomicReference<RadioResponseInfo> result = new AtomicReference<>();
        IBinder responseBinder = peer(), indicationBinder = peer();
        IRadioVoiceResponse response = (IRadioVoiceResponse) Proxy.newProxyInstance(
                IRadioVoiceResponse.class.getClassLoader(), new Class<?>[]{IRadioVoiceResponse.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return responseBinder;
                    if (m.getName().equals("getCurrentCallsResponse")) {
                        result.set((RadioResponseInfo) a[0]); replyCount.incrementAndGet(); replied.countDown();
                    }
                    return null;
                });
        IRadioVoiceIndication indication = (IRadioVoiceIndication) Proxy.newProxyInstance(
                IRadioVoiceIndication.class.getClassLoader(), new Class<?>[]{IRadioVoiceIndication.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return indicationBinder;
                    if (m.getName().equals("callStateChanged")) { entered.countDown(); release.await(); }
                    return null;
                });
        RadioVoiceService service = new RadioVoiceService("slot1", 4, 3000);
        service.setResponseFunctions(response, indication);
        Thread incoming = new Thread(() -> { try { service.injectIncoming("+10000000000"); } catch (RemoteException ignored) {} });
        AtomicReference<Throwable> closeFailure = new AtomicReference<>();
        Thread closing = new Thread(() -> { try { service.close(); } catch (Throwable t) { closeFailure.set(t); } });
        try {
            incoming.start();
            if (!entered.await(2, TimeUnit.SECONDS)) throw new AssertionError("worker not blocked");
            closing.start();
            Field field = RadioVoiceService.class.getDeclaredField("events"); field.setAccessible(true);
            ThreadPoolExecutor executor = (ThreadPoolExecutor) field.get(service);
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
            while (!executor.isShutdown() && System.nanoTime() < deadline) Thread.sleep(5);
            if (!executor.isShutdown()) throw new AssertionError("service not draining");
            try { service.getCurrentCalls(55); }
            catch (RemoteException ignored) { /* this exception is not delivered by one-way Binder */ }
            if (!replied.await(1, TimeUnit.SECONDS))
                throw new AssertionError("oneway request during shutdown received no response");
            RadioResponseInfo info = result.get();
            if (info == null || info.serial != 55 || info.error != RadioError.RADIO_NOT_AVAILABLE)
                throw new AssertionError("wrong shutdown response");
            release.countDown(); incoming.join(3000); closing.join(3000);
            if (incoming.isAlive() || closing.isAlive() || closeFailure.get() != null)
                throw new AssertionError("test did not cleanly terminate", closeFailure.get());
            if (replyCount.get() != 1) throw new AssertionError("shutdown request answered more than once");
        } finally {
            release.countDown(); incoming.join(3000); closing.join(3000);
            try { service.close(); } catch (IllegalStateException ignored) { /* incomplete stop remains visible */ }
        }
        System.out.println("RADIO_VOICE_SHUTDOWN_ONEWAY_OK");
    }
}
