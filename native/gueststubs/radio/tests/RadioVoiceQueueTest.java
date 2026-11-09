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
import java.util.concurrent.atomic.AtomicReference;

/** Bounded-queue, close and reentry checks against the real v4 method contracts. */
public final class RadioVoiceQueueTest {
    private static IBinder peer() {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class}, (p,m,a) -> {
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
        RadioVoiceService service = new RadioVoiceService("slot1", 1, 5000);
        CountDownLatch entered = new CountDownLatch(1), release = new CountDownLatch(1);
        AtomicReference<RadioResponseInfo> busy = new AtomicReference<>(), queued = new AtomicReference<>();
        AtomicReference<Throwable> incomingResult = new AtomicReference<>(), waitingResult = new AtomicReference<>();
        AtomicReference<Throwable> closeResult = new AtomicReference<>();
        IBinder rb = peer(), ib = peer();
        IRadioVoiceResponse response = (IRadioVoiceResponse) Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceResponse.class}, (p,m,a) -> {
                    if (m.getName().equals("asBinder")) return rb;
                    if (m.getName().equals("hangupConnectionResponse")) busy.set((RadioResponseInfo) a[0]);
                    if (m.getName().equals("getCurrentCallsResponse")) queued.set((RadioResponseInfo) a[0]);
                    return null;
                });
        IRadioVoiceIndication indication = (IRadioVoiceIndication) Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceIndication.class}, (p,m,a) -> {
                    if (m.getName().equals("asBinder")) return ib;
                    if (m.getName().equals("callStateChanged")) { entered.countDown(); release.await(); }
                    return null;
                });
        service.setResponseFunctions(response, indication);
        Thread incoming = new Thread(() -> { try { service.injectIncoming("+10000000000"); } catch (Throwable e) { incomingResult.set(e); } });
        Thread waiting = new Thread(() -> { try { service.getCurrentCalls(101); } catch (Throwable e) { waitingResult.set(e); } });
        incoming.start();
        if (!entered.await(2, TimeUnit.SECONDS)) throw new AssertionError("worker not entered");
        waiting.start();
        Field f = RadioVoiceService.class.getDeclaredField("events"); f.setAccessible(true);
        ThreadPoolExecutor executor = (ThreadPoolExecutor) f.get(service);
        long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
        while (executor.getQueue().size() != 1 && System.nanoTime() < end) Thread.sleep(5);
        if (executor.getQueue().size() != 1) throw new AssertionError("queue not full");
        service.hangup(102, 1);
        RadioResponseInfo info = busy.get();
        if (info == null || info.serial != 102 || info.error != RadioError.NO_RESOURCES)
            throw new AssertionError("queue overflow did not return explicit NO_RESOURCES");
        Thread closing = new Thread(() -> { try { service.close(); } catch (Throwable e) { closeResult.set(e); } });
        closing.start(); release.countDown();
        incoming.join(3000); waiting.join(3000); closing.join(3000);
        RadioResponseInfo queuedInfo = queued.get();
        if (incoming.isAlive() || waiting.isAlive() || closing.isAlive() || closeResult.get() != null ||
                waitingResult.get() != null || queuedInfo == null || queuedInfo.serial != 101)
            throw new AssertionError("close did not finish accepted request before returning");
        try { service.getCurrentCalls(103); throw new AssertionError("closed service accepted request"); }
        catch (RemoteException expected) { /* explicitly stopped */ }
        System.out.println("RADIO_VOICE_QUEUE_OK");
    }
}
