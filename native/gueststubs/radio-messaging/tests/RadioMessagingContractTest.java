package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.messaging.IRadioMessaging;
import android.hardware.radio.messaging.IRadioMessagingIndication;
import android.hardware.radio.messaging.IRadioMessagingResponse;
import android.os.IBinder;
import android.os.RemoteException;

import java.lang.reflect.Array;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;

/** Isolated contract test. It never registers a service or accesses the real radio. */
public final class RadioMessagingContractTest {
    private static void check(boolean ok, String message) {
        if (!ok) throw new AssertionError(message);
    }
    private static final class Peer {
        final List<IBinder.DeathRecipient> deaths = new ArrayList<>();
        final IBinder binder = (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(),
                new Class<?>[]{IBinder.class}, (proxy, method, args) -> {
                    switch (method.getName()) {
                        case "linkToDeath": deaths.add((IBinder.DeathRecipient) args[0]); return null;
                        case "unlinkToDeath": return deaths.remove(args[0]);
                        case "isBinderAlive": return true;
                        case "hashCode": return System.identityHashCode(proxy);
                        case "equals": return proxy == args[0];
                        case "toString": return "synthetic-peer";
                        default: throw new UnsupportedOperationException(method.getName());
                    }
                });
        void die() { for (IBinder.DeathRecipient d : new ArrayList<>(deaths)) d.binderDied(); }
    }
    private static final class Response {
        final Peer peer = new Peer();
        String name;
        RadioResponseInfo info;
        final IRadioMessagingResponse proxy = (IRadioMessagingResponse) Proxy.newProxyInstance(
                IRadioMessagingResponse.class.getClassLoader(), new Class<?>[]{IRadioMessagingResponse.class},
                (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return peer.binder;
                    if (m.getName().equals("toString")) return "synthetic-response";
                    name = m.getName();
                    info = a != null && a.length > 0 && a[0] instanceof RadioResponseInfo
                            ? (RadioResponseInfo) a[0] : null;
                    return null;
                });
    }
    private static IRadioMessagingIndication indication(Peer peer) {
        return (IRadioMessagingIndication) Proxy.newProxyInstance(IRadioMessagingIndication.class.getClassLoader(),
                new Class<?>[]{IRadioMessagingIndication.class}, (p, m, a) -> {
                    if (m.getName().equals("asBinder")) return peer.binder;
                    if (m.getName().equals("toString")) return "synthetic-indication";
                    throw new UnsupportedOperationException(m.getName());
                });
    }
    private static Object argument(Class<?> type, int serial) {
        if (type == int.class) return serial;
        if (type == boolean.class) return false;
        if (type == String.class) return "";
        if (type.isArray()) return Array.newInstance(type.getComponentType(), 0);
        return null;
    }
    public static void main(String[] args) throws Exception {
        try { RadioMessagingService.withoutStateForTests("slot2"); throw new AssertionError("slot2 accepted"); }
        catch (IllegalArgumentException expected) { /* exactly one simulated slot */ }
        RadioMessagingService service = RadioMessagingService.withoutStateForTests("slot1");
        check(service.getInterfaceVersion() == IRadioMessaging.VERSION, "wrong interface version");
        check(service.getInterfaceHash().equals(IRadioMessaging.HASH), "wrong interface hash");
        try { service.getSmscAddress(1); throw new AssertionError("missing callback succeeded"); }
        catch (RemoteException expected) { /* explicit failure */ }
        Response first = new Response(); Peer firstIndication = new Peer();
        service.setResponseFunctions(first.proxy, indication(firstIndication));
        check(first.peer.deaths.size() == 1 && firstIndication.deaths.size() == 1, "death not tracked");
        try { service.setResponseFunctions(null, indication(new Peer())); throw new AssertionError("null response accepted"); }
        catch (RemoteException expected) { /* do not replace the live callback pair */ }
        try { service.setResponseFunctions(first.proxy, null); throw new AssertionError("null indication accepted"); }
        catch (RemoteException expected) { /* do not replace the live callback pair */ }
        service.getSmscAddress(9);
        check(first.info != null && first.info.serial == 9 && first.info.error == RadioError.REQUEST_NOT_SUPPORTED,
                "invalid binding damaged existing callback");
        IBinder.DeathRecipient oldDeath = first.peer.deaths.get(0);
        int serial = 10;
        for (Method m : IRadioMessaging.class.getDeclaredMethods()) {
            String name = m.getName();
            if (name.equals("setResponseFunctions") || name.equals("responseAcknowledgement") ||
                    name.equals("getInterfaceVersion") || name.equals("getInterfaceHash")) continue;
            Class<?>[] types = m.getParameterTypes();
            check(types.length > 0 && types[0] == int.class, "unexpected request signature: " + name);
            Object[] values = new Object[types.length];
            for (int i = 0; i < types.length; i++) values[i] = argument(types[i], i == 0 ? serial : 0);
            first.name = null; first.info = null;
            try { m.invoke(service, values); }
            catch (InvocationTargetException e) { throw new AssertionError("request threw: " + name, e.getCause()); }
            check((name + "Response").equals(first.name), "wrong callback: " + name + " -> " + first.name);
            int expectedError = name.equals("acknowledgeLastIncomingGsmSms")
                    ? RadioError.INVALID_STATE : RadioError.REQUEST_NOT_SUPPORTED;
            check(first.info != null && first.info.serial == serial && first.info.error == expectedError,
                    "wrong serial/error: " + name);
            serial++;
        }
        Response next = new Response(); Peer nextIndication = new Peer();
        service.setResponseFunctions(next.proxy, indication(nextIndication));
        oldDeath.binderDied();
        service.getSmscAddress(300);
        check(next.info != null && next.info.serial == 300, "old death cleared new callbacks");
        next.peer.die();
        try { service.getSmscAddress(301); throw new AssertionError("dead callback succeeded"); }
        catch (RemoteException expected) { /* death invalidates callback pair */ }
        check(nextIndication.deaths.isEmpty(), "remaining death recipient not unlinked");
        System.out.println("RADIO_MESSAGING_CONTRACT_OK requests=" + (serial - 10));
    }
}
