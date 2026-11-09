package app.aemu.stub;

import android.hardware.radio.voice.IRadioVoiceIndication;
import android.hardware.radio.voice.IRadioVoiceResponse;
import android.os.IBinder;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/** A background detach failure remains externally observable after a successful retry. */
public final class RadioVoiceAsyncCleanupDiagnosticTest {
    private static IBinder peer(AtomicInteger unlinks, CountDownLatch failedOnce) {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
                (p, m, a) -> {
                    switch (m.getName()) {
                        case "linkToDeath": return null;
                        case "unlinkToDeath":
                            if (unlinks.incrementAndGet() == 1 && failedOnce != null) {
                                failedOnce.countDown();
                                throw new IllegalStateException("synthetic asynchronous detach failure");
                            }
                            return true;
                        case "isBinderAlive": return true;
                        case "hashCode": return System.identityHashCode(p);
                        case "equals": return p == a[0];
                        default: throw new UnsupportedOperationException(m.getName());
                    }
                });
    }
    private static IRadioVoiceResponse response(IBinder binder, CountDownLatch entered, CountDownLatch release) {
        return (IRadioVoiceResponse) Proxy.newProxyInstance(IRadioVoiceResponse.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceResponse.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return binder;
                    if (m.getName().equals("getCurrentCallsResponse") && entered != null) {
                        entered.countDown(); release.await();
                    }
                    return null;
                });
    }
    private static IRadioVoiceIndication indication(IBinder binder) {
        return (IRadioVoiceIndication) Proxy.newProxyInstance(IRadioVoiceIndication.class.getClassLoader(),
                new Class<?>[]{IRadioVoiceIndication.class}, (p, m, a) -> m.getName().equals("asBinder") ? binder : null);
    }
    private static RuntimeException diagnostic(Method method, RadioVoiceService service) throws Exception {
        Object value = method.invoke(service);
        if (value != null && !(value instanceof RuntimeException))
            throw new AssertionError("unexpected diagnostic type");
        return (RuntimeException) value;
    }
    public static void main(String[] args) throws Exception {
        CountDownLatch entered = new CountDownLatch(1), release = new CountDownLatch(1), failure = new CountDownLatch(1);
        AtomicInteger oldUnlinks = new AtomicInteger();
        RadioVoiceService service = new RadioVoiceService("slot1", 4, 2000);
        service.setResponseFunctions(response(peer(oldUnlinks, failure), entered, release),
                indication(peer(new AtomicInteger(), null)));
        Thread request = new Thread(() -> { try { service.getCurrentCalls(42); } catch (Exception ignored) {} });
        boolean closed = false;
        try {
            request.start();
            if (!entered.await(2, TimeUnit.SECONDS)) throw new AssertionError("old callback did not enter");
            service.setResponseFunctions(response(peer(new AtomicInteger(), null), null, null),
                    indication(peer(new AtomicInteger(), null)));
            release.countDown(); request.join(3000);
            if (request.isAlive()) throw new AssertionError("old request did not finish");
            if (!failure.await(2, TimeUnit.SECONDS)) throw new AssertionError("asynchronous detach did not fail");
            Method getter;
            try { getter = RadioVoiceService.class.getMethod("getAsynchronousCleanupFailure"); }
            catch (NoSuchMethodException missing) { throw new AssertionError("background detach diagnostic is not public", missing); }
            RuntimeException observed = null;
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
            while (observed == null && System.nanoTime() < deadline) {
                observed = diagnostic(getter, service);
                if (observed == null) Thread.sleep(5);
            }
            if (observed == null || !"synthetic asynchronous detach failure".equals(observed.getMessage()))
                throw new AssertionError("background detach failure not observable", observed);
            service.close(); closed = true;
            if (oldUnlinks.get() < 2) throw new AssertionError("close did not retry failed detach");
            RuntimeException afterClose = diagnostic(getter, service);
            if (afterClose == null || !"synthetic asynchronous detach failure".equals(afterClose.getMessage()))
                throw new AssertionError("successful retry erased the historical failure", afterClose);
        } finally {
            release.countDown(); request.join(3000);
            if (!closed) service.close();
        }
        System.out.println("RADIO_VOICE_ASYNC_CLEANUP_DIAGNOSTIC_OK");
    }
}
