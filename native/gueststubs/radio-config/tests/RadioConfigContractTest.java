package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.config.IRadioConfig;
import android.hardware.radio.config.IRadioConfigIndication;
import android.hardware.radio.config.IRadioConfigResponse;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.Array;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;

/** Isolated contract; no service registration and no actual SIM or network. */
public final class RadioConfigContractTest {
    private static void check(boolean ok, String reason) { if (!ok) throw new AssertionError(reason); }
    private static final class Peer {
        final List<IBinder.DeathRecipient> deaths = new ArrayList<>();
        final IBinder binder = (IBinder)Proxy.newProxyInstance(IBinder.class.getClassLoader(),
                new Class<?>[]{IBinder.class}, (p,m,a) -> {
                    switch (m.getName()) {
                        case "linkToDeath": deaths.add((IBinder.DeathRecipient)a[0]); return null;
                        case "unlinkToDeath": return deaths.remove(a[0]);
                        case "isBinderAlive": return true;
                        case "hashCode": return System.identityHashCode(p);
                        case "equals": return p == a[0];
                        case "toString": return "synthetic-peer";
                        default: throw new UnsupportedOperationException(m.getName());
                    }
                });
        void die() { for (IBinder.DeathRecipient d : new ArrayList<>(deaths)) d.binderDied(); }
    }
    private static final class Response {
        final Peer peer = new Peer();
        String name;
        RadioResponseInfo info;
        final IRadioConfigResponse proxy = (IRadioConfigResponse)Proxy.newProxyInstance(
                IRadioConfigResponse.class.getClassLoader(), new Class<?>[]{IRadioConfigResponse.class},
                (p,m,a) -> {
                    if (m.getName().equals("asBinder")) return peer.binder;
                    if (m.getName().equals("toString")) return "synthetic-response";
                    name = m.getName();
                    info = a != null && a.length > 0 && a[0] instanceof RadioResponseInfo ? (RadioResponseInfo)a[0] : null;
                    return null;
                });
    }
    private static IRadioConfigIndication indication(Peer peer) {
        return (IRadioConfigIndication)Proxy.newProxyInstance(IRadioConfigIndication.class.getClassLoader(),
                new Class<?>[]{IRadioConfigIndication.class}, (p,m,a) -> {
                    if (m.getName().equals("asBinder")) return peer.binder;
                    if (m.getName().equals("toString")) return "synthetic-indication";
                    throw new UnsupportedOperationException(m.getName());
                });
    }
    private static Object argument(Class<?> type, int serial) {
        if (type == int.class) return serial;
        if (type == byte.class) return (byte)0;
        if (type == boolean.class) return false;
        if (type.isArray()) return Array.newInstance(type.getComponentType(), 0);
        return null;
    }
    public static void main(String[] args) throws Exception {
        try { new RadioConfigService("slot2"); throw new AssertionError("slot2 accepted"); }
        catch (IllegalArgumentException expected) { /* exactly one simulated slot */ }
        RadioConfigService service = new RadioConfigService("slot1");
        check(service.getInterfaceVersion() == IRadioConfig.VERSION, "wrong interface version");
        check(service.getInterfaceHash().equals(IRadioConfig.HASH), "wrong interface hash");
        try { service.getNumOfLiveModems(1); throw new AssertionError("missing callback succeeded"); }
        catch (RemoteException expected) { /* explicit failure */ }
        Response first = new Response(); Peer indication = new Peer();
        service.setResponseFunctions(first.proxy, indication(indication));
        check(first.peer.deaths.size() == 1 && indication.deaths.size() == 1, "death not tracked");
        try { service.setResponseFunctions(null, indication(new Peer())); throw new AssertionError("null response accepted"); }
        catch (RemoteException expected) { /* no callback change */ }
        try { service.setResponseFunctions(first.proxy, null); throw new AssertionError("null indication accepted"); }
        catch (RemoteException expected) { /* no callback change */ }
        IBinder.DeathRecipient oldDeath = first.peer.deaths.get(0);
        int serial = 10;
        for (Method m : IRadioConfig.class.getDeclaredMethods()) {
            String name = m.getName();
            if (name.equals("setResponseFunctions") || name.equals("getInterfaceVersion") || name.equals("getInterfaceHash")) continue;
            Class<?>[] types = m.getParameterTypes();
            check(types.length > 0 && types[0] == int.class, "unexpected request signature: " + name);
            Object[] values = new Object[types.length];
            for (int i = 0; i < types.length; i++) values[i] = argument(types[i], i == 0 ? serial : 0);
            first.name = null; first.info = null;
            try { m.invoke(service, values); }
            catch (InvocationTargetException e) { throw new AssertionError("request threw: " + name, e.getCause()); }
            check((name + "Response").equals(first.name), "wrong callback: " + name + " -> " + first.name);
            check(first.info != null && first.info.serial == serial && first.info.error == RadioError.REQUEST_NOT_SUPPORTED,
                    "wrong serial/error: " + name);
            serial++;
        }
        Response next = new Response(); Peer nextIndication = new Peer();
        service.setResponseFunctions(next.proxy, indication(nextIndication));
        oldDeath.binderDied();
        service.getNumOfLiveModems(300);
        check(next.info != null && next.info.serial == 300, "old death cleared new callbacks");
        next.peer.die();
        try { service.getNumOfLiveModems(301); throw new AssertionError("dead callback succeeded"); }
        catch (RemoteException expected) { /* death invalidates pair */ }
        check(nextIndication.deaths.isEmpty(), "remaining death recipient not unlinked");
        System.out.println("RADIO_CONFIG_CONTRACT_OK requests=" + (serial - 10));
    }
}
