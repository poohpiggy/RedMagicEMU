package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.modem.IRadioModemIndication;
import android.hardware.radio.modem.IRadioModemResponse;
import android.hardware.radio.network.IRadioNetworkIndication;
import android.hardware.radio.network.IRadioNetworkResponse;
import android.os.IBinder;
import java.lang.reflect.Proxy;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/** A blocked network client must not hold up modem power responses. */
public final class RadioNetworkBlockedIndicationTest {
    private static void check(boolean value, String message) { if (!value) throw new AssertionError(message); }
    private static IBinder peer() {
        return (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class}, (p,m,a) -> {
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
        SyntheticSingleSlot model = SyntheticSingleSlot.forTests();
        RadioNetworkService network = new RadioNetworkService("slot1", model);
        IBinder rb=peer(), ib=peer();
        CountDownLatch initial = new CountDownLatch(1), blocked = new CountDownLatch(1), release = new CountDownLatch(1);
        AtomicInteger count = new AtomicInteger();
        IRadioNetworkResponse response = (IRadioNetworkResponse) Proxy.newProxyInstance(IRadioNetworkResponse.class.getClassLoader(), new Class<?>[]{IRadioNetworkResponse.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return rb;
            throw new AssertionError("unexpected network response");
        });
        IRadioNetworkIndication indication = (IRadioNetworkIndication) Proxy.newProxyInstance(IRadioNetworkIndication.class.getClassLoader(), new Class<?>[]{IRadioNetworkIndication.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return ib;
            if (m.getName().equals("currentSignalStrength")) {
                if (count.incrementAndGet() == 1) initial.countDown();
                else { blocked.countDown(); release.await(15, TimeUnit.SECONDS); }
                return null;
            }
            throw new AssertionError("unexpected network indication");
        });
        network.setResponseFunctions(response, indication);
        check(initial.await(2, TimeUnit.SECONDS), "initial signal missing");
        RadioModemService modem = new RadioModemService("slot1", model);
        IBinder mrb=peer(), mib=peer();
        final RadioResponseInfo[] result = {null};
        IRadioModemResponse mr = (IRadioModemResponse) Proxy.newProxyInstance(IRadioModemResponse.class.getClassLoader(), new Class<?>[]{IRadioModemResponse.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return mrb;
            if (m.getName().equals("setRadioPowerResponse")) { result[0]=(RadioResponseInfo)a[0]; return null; }
            throw new AssertionError("unexpected modem response");
        });
        IRadioModemIndication mi = (IRadioModemIndication) Proxy.newProxyInstance(IRadioModemIndication.class.getClassLoader(), new Class<?>[]{IRadioModemIndication.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return mib;
            if (m.getName().equals("rilConnected") || m.getName().equals("radioStateChanged")) return null;
            throw new AssertionError("unexpected modem indication");
        });
        modem.setResponseFunctions(mr, mi);
        try {
            modem.setRadioPower(1, true, false, false);
            check(result[0].error == RadioError.NONE && blocked.await(2, TimeUnit.SECONDS), "signal callback did not block");
            CountDownLatch returned = new CountDownLatch(1);
            Thread power = new Thread(() -> { try { modem.setRadioPower(2, false, false, false); } catch (Exception e) { throw new RuntimeException(e); } finally { returned.countDown(); } });
            power.start();
            check(returned.await(1, TimeUnit.SECONDS), "blocked signal callback delayed power request");
            check(result[0].serial == 2 && result[0].error == RadioError.NONE, "power OFF failed while callback blocked");
            long replacementStart = System.nanoTime();
            try { network.setResponseFunctions(response, indication); throw new AssertionError("replacement succeeded while delivery blocked"); }
            catch (android.os.RemoteException expected) { }
            check(System.nanoTime() - replacementStart < TimeUnit.SECONDS.toNanos(4), "replacement did not time out");
            long closeStart = System.nanoTime();
            try { network.close(); throw new AssertionError("close succeeded while delivery blocked"); }
            catch (IllegalStateException expected) { }
            check(System.nanoTime() - closeStart < TimeUnit.SECONDS.toNanos(4), "close did not time out");
        } finally {
            release.countDown();
            network.close();
            modem.detachStateListener();
        }
        System.out.println("RADIO_NETWORK_BLOCKED_INDICATION_OK");
    }
}
