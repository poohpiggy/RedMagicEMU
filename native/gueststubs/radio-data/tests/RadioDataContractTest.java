package app.aemu.stub;

import android.hardware.radio.RadioError;
import android.hardware.radio.RadioResponseInfo;
import android.hardware.radio.data.IRadioData;
import android.hardware.radio.data.IRadioDataIndication;
import android.hardware.radio.data.IRadioDataResponse;
import android.os.IBinder;
import android.os.RemoteException;
import java.lang.reflect.Array;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;

/** Contract for the unregistered, no-real-bearer, single-slot data service. */
public final class RadioDataContractTest {
    private static void check(boolean ok, String reason) { if (!ok) throw new AssertionError(reason); }
    private static final class Peer {
        final List<IBinder.DeathRecipient> deaths = new ArrayList<>();
        final IBinder binder = (IBinder) Proxy.newProxyInstance(IBinder.class.getClassLoader(), new Class<?>[]{IBinder.class}, (p,m,a) -> {
            switch (m.getName()) {
                case "linkToDeath": deaths.add((IBinder.DeathRecipient)a[0]); return null;
                case "unlinkToDeath": return deaths.remove(a[0]);
                case "isBinderAlive": return true;
                case "hashCode": return System.identityHashCode(p);
                case "equals": return p == a[0];
                default: throw new UnsupportedOperationException(m.getName());
            }
        });
        void die() { for (IBinder.DeathRecipient d : new ArrayList<>(deaths)) d.binderDied(); }
    }
    private static final class Response {
        final Peer peer = new Peer(); String name; RadioResponseInfo info; Object value;
        final IRadioDataResponse proxy = (IRadioDataResponse) Proxy.newProxyInstance(IRadioDataResponse.class.getClassLoader(), new Class<?>[]{IRadioDataResponse.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return peer.binder;
            name=m.getName(); info=a!=null && a.length>0 && a[0] instanceof RadioResponseInfo ? (RadioResponseInfo)a[0] : null;
            value=a!=null && a.length>1 ? a[1] : null; return null;
        });
    }
    private static IRadioDataIndication indication(Peer peer) {
        return (IRadioDataIndication) Proxy.newProxyInstance(IRadioDataIndication.class.getClassLoader(), new Class<?>[]{IRadioDataIndication.class}, (p,m,a) -> {
            if (m.getName().equals("asBinder")) return peer.binder;
            throw new UnsupportedOperationException(m.getName());
        });
    }
    private static Object argument(Class<?> type, int value) {
        if(type==int.class) return value; if(type==long.class) return 0L; if(type==byte.class) return (byte)0;
        if(type==boolean.class) return false; if(type.isArray()) return Array.newInstance(type.getComponentType(),0); return null;
    }
    public static void main(String[] args) throws Exception {
        try { new RadioDataService("slot2"); throw new AssertionError("slot2 accepted"); } catch (IllegalArgumentException expected) { }
        RadioDataService service = new RadioDataService("slot1");
        check(service.getInterfaceVersion()==IRadioData.VERSION && service.getInterfaceHash().equals(IRadioData.HASH),"wrong ABI");
        try { service.getDataCallList(1); throw new AssertionError("unbound request accepted"); } catch (RemoteException expected) { }
        Response first = new Response(); Peer ind = new Peer(); service.setResponseFunctions(first.proxy,indication(ind));
        int serial=100;
        for(Method method:IRadioData.class.getDeclaredMethods()) {
            String name=method.getName();
            if(name.equals("setResponseFunctions") || name.equals("responseAcknowledgement") || name.equals("getInterfaceVersion") || name.equals("getInterfaceHash")) continue;
            Class<?>[] types=method.getParameterTypes(); check(types.length>0 && types[0]==int.class,"unexpected request "+name);
            Object[] arguments=new Object[types.length]; for(int j=0;j<types.length;j++) arguments[j]=argument(types[j],j==0?serial:0);
            first.name=null; first.info=null;
            try { method.invoke(service,arguments); } catch(InvocationTargetException error) { throw new AssertionError(name,error.getCause()); }
            check((name+"Response").equals(first.name),"wrong callback for "+name+": "+first.name);
            check(first.info!=null && first.info.serial==serial && first.info.error==RadioError.REQUEST_NOT_SUPPORTED,"wrong error or serial for "+name);
            serial++;
        }
        IBinder.DeathRecipient old=first.peer.deaths.get(0);
        Response next=new Response(); Peer nextInd=new Peer(); service.setResponseFunctions(next.proxy,indication(nextInd));
        old.binderDied(); service.getDataCallList(999); check(next.info!=null && next.info.serial==999,"old death cleared replacement");
        next.peer.die();
        try { service.getDataCallList(1000); throw new AssertionError("dead callback accepted"); } catch(RemoteException expected) { }
        check(nextInd.deaths.isEmpty(),"indication death recipient leaked");
        System.out.println("RADIO_DATA_CONTRACT_OK requests="+(serial-100));
    }
}
