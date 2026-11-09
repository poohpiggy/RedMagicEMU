package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.messaging.IRadioMessagingIndication;
import android.hardware.radio.messaging.IRadioMessagingResponse;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** An old in-flight callback may fail after a new client binds; it must not clear the new PDU. */
public final class RadioMessagingRaceTest {
    private static void check(boolean value, String message) { if (!value) throw new AssertionError(message); }
    private static IBinder binder() {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class},
                (p,m,a) -> {
                    switch (m.getName()) {
                        case "linkToDeath": return null;
                        case "unlinkToDeath": return true;
                        case "isBinderAlive": return true;
                        case "hashCode": return System.identityHashCode(p);
                        case "equals": return p == a[0];
                        case "toString": return "synthetic-peer";
                        default: throw new UnsupportedOperationException(m.getName());
                    }
                });
    }
    private static final class Peer {
        final IBinder r = binder(), i = binder();
        final CountDownLatch entered = new CountDownLatch(1), release = new CountDownLatch(1);
        boolean pauseAndFail;
        RadioResponseInfo responseInfo;
        Peer(boolean pauseAndFail) { this.pauseAndFail = pauseAndFail; }
        final IRadioMessagingResponse response = (IRadioMessagingResponse) Proxy.newProxyInstance(
                IRadioMessagingResponse.class.getClassLoader(), new Class<?>[]{IRadioMessagingResponse.class},
                (p,m,a) -> {
                    if (m.getName().equals("asBinder")) return r;
                    if (m.getName().equals("toString")) return "synthetic-response";
                    if (a != null && a.length > 0 && a[0] instanceof RadioResponseInfo) responseInfo = (RadioResponseInfo)a[0];
                    return null;
                });
        final IRadioMessagingIndication indication = (IRadioMessagingIndication) Proxy.newProxyInstance(
                IRadioMessagingIndication.class.getClassLoader(), new Class<?>[]{IRadioMessagingIndication.class},
                (p,m,a) -> {
                    if (m.getName().equals("asBinder")) return i;
                    if (m.getName().equals("toString")) return "synthetic-indication";
                    if (!m.getName().equals("newSms")) throw new AssertionError("unexpected indication");
                    entered.countDown();
                    if (pauseAndFail) {
                        if (!release.await(5, TimeUnit.SECONDS)) throw new AssertionError("test timed out");
                        throw new RemoteException("old client failed");
                    }
                    return null;
                });
    }
    public static void main(String[] args) throws Exception {
        RadioMessagingService service = RadioMessagingService.withoutStateForTests("slot1");
        Peer old = new Peer(true), next = new Peer(false);
        service.setResponseFunctions(old.response, old.indication);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread oldDelivery = new Thread(() -> {
            try { service.injectIncomingGsmSms(new byte[]{1}); }
            catch (Throwable t) { failure.set(t); }
        });
        oldDelivery.start();
        check(old.entered.await(5, TimeUnit.SECONDS), "old callback did not start");
        service.setResponseFunctions(next.response, next.indication);
        service.injectIncomingGsmSms(new byte[]{2});
        old.release.countDown();
        oldDelivery.join(5000);
        check(!oldDelivery.isAlive() && failure.get() instanceof RemoteException, "old failure not observed");
        service.acknowledgeLastIncomingGsmSms(510, true, 0);
        check(next.responseInfo != null && next.responseInfo.serial == 510 && next.responseInfo.error == RadioError.NONE,
                "old failure damaged new pending PDU");
        System.out.println("RADIO_MESSAGING_RACE_OK");
    }
}
